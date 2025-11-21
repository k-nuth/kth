// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_MINING_PARTIALLY_INDEXED_HPP_
#define KTH_BLOCKCHAIN_MINING_PARTIALLY_INDEXED_HPP_

#include <list>
#include <print>
#include <vector>

#include <kth/mining/common.hpp>
#include <kth/mining/partially_indexed_node.hpp>

#include <kth/domain.hpp>


namespace kth {
namespace mining {

using main_index_t = size_t;

template <typename T, typename Cmp, typename State>
    // requires(Regular<T>)
class partially_indexed {
public:
    using value_type = T;
    using indexes_container_t = std::list<main_index_t>;
    using candidate_index_t = indexes_container_t::iterator;
    using candidate_index_const_t = indexes_container_t::const_iterator;
    using internal_value_type = partially_indexed_node<candidate_index_t, T>;
    using main_container_t = std::vector<internal_value_type>;

    partially_indexed(Cmp cmp, State& state)
        : null_index_(std::end(candidate_elements_))
        , sorted_(false)
        , cmp_(cmp)
        , state_(state) {}

    void reserve(size_t all) {
        all_elements_.reserve(all);
        // candidate_elements_.reserve(candidates);
    }

    bool insert(T const& x) {
        auto res = insert_internal(x);
#ifndef NDEBUG
        check_invariant();
#endif
        return res;
    }

    bool insert(T&& x) {
        auto res =  insert_internal(std::move(x));
#ifndef NDEBUG
        check_invariant();
#endif
        return res;
    }

    template <typename... Args>
    bool emplace(Args&&... args) {
        auto res =  insert_internal(std::forward<Args>(args)...);
#ifndef NDEBUG
        check_invariant();
#endif
        return res;
    }

    size_t size() const {
        return all_elements_.size();
    }

    size_t candidates_size() const {
        return candidate_elements_.size();
    }

    bool empty() const {
        return all_elements_.empty();
    }

    bool sorted() const {
        return sorted_;
    }

    bool is_candidate(main_index_t i) const {
        auto const& node = all_elements_[i];
        return node.index() != null_index_;
    }

    size_t candidate_rank(main_index_t i) const {
        //precondition: is_candidate(i)
        auto const& node = all_elements_[i];
        return std::distance(std::begin(candidate_elements_), candidate_index_const_t(node.index()));
    }

    value_type const& operator[](std::size_t i) const {
        return all_elements_[i].element();
    }

    value_type& operator[](std::size_t i) {
        return all_elements_[i].element();
    }

    //TODO(fernando): private
    void erase_index(std::size_t i) {
        all_elements_.erase(std::next(std::begin(all_elements_), i));
    }

    void clear_candidates() {
        candidate_elements_.clear();
        sorted_ = false;
        for (size_t i = 0; i < all_elements_.size(); ++i) {
            auto& elem = all_elements_[i];
            elem.set_index(null_index_);
            state_.reset_element(elem.element());
        }

#ifndef NDEBUG
        check_invariant();
#endif
    }

    void re_construct_candidates() {
        for (size_t i = 0; i < all_elements_.size(); ++i) {
            re_add_node(i);
        }
#ifndef NDEBUG
        check_invariant();
#endif
    }

    template <typename F>
    void for_each(F f) {
        for (auto& node : all_elements_) {
            if ( !  f(node.element())) {
                break;
            }
        }
    }

    template <typename F>
    void for_each(F f) const {
        for (auto const& node : all_elements_) {
            if ( !  f(node.element())) {
                break;
            }
        }
    }

    auto internal_data() const {
        return std::make_tuple(candidate_elements_, all_elements_, sorted_);
    }

// ----------------------------------------------------------------------------------------
//  Invariant Checks
// ----------------------------------------------------------------------------------------
#ifndef NDEBUG

    void check_invariant_partial() const {

        BOOST_ASSERT(candidate_elements_.size() <= all_elements_.size());

        {
            for (auto i : candidate_elements_) {
                if (i >= all_elements_.size()) {
                    BOOST_ASSERT(false);
                }
            }
        }

        // {
        //     for (auto const& node : all_elements_) {
        //         if (node.index() != null_index_ && node.index() >= candidate_elements_.size()) {
        //             BOOST_ASSERT(false);
        //         }
        //     }
        // }

        {
            auto ci_sorted = candidate_elements_;
            ci_sorted.sort();
            auto last = std::unique(std::begin(ci_sorted), std::end(ci_sorted));
            BOOST_ASSERT(std::distance(std::begin(ci_sorted), last) == ci_sorted.size());
        }

        {
            std::vector<main_index_t> all_sorted;
            for (auto const& node : all_elements_) {
                if (node.index() != null_index_) {
                    all_sorted.push_back(*(node.index()));
                }
            }
            std::sort(std::begin(all_sorted), std::end(all_sorted));
            auto last = std::unique(std::begin(all_sorted), std::end(all_sorted));
            BOOST_ASSERT(std::distance(std::begin(all_sorted), last) == all_sorted.size());
        }

        {
            auto it = std::begin(candidate_elements_);
            auto end = std::end(candidate_elements_);
            while (it != end) {
                auto const& node = all_elements_[*it];
                BOOST_ASSERT(it == node.index());
                ++it;
            }
        }

        {
            size_t i = 0;
            size_t non_indexed = 0;
            for (auto const& node : all_elements_) {
                if (node.index() != null_index_) {
                    BOOST_ASSERT(*(node.index()) == i);
                } else {
                    ++non_indexed;
                }
                ++i;
            }

            BOOST_ASSERT(candidate_elements_.size() + non_indexed == all_elements_.size());
        }
    }

    void check_invariant() const {
        std::println("Checking invariants...");
        check_invariant_partial();

        {
            auto g = getter_const();
            auto b = bounds_ok();
            // size_t ci = 0;
            for (auto i : candidate_elements_) {
                auto const& node = all_elements_[i];
                // check_children_accum(i);
                BOOST_ASSERT(state_.check_node(i, g, b));
            }
        }

        {
            auto g = getter_const();
            auto b = bounds_ok();
            size_t i = 0;
            for (auto const& node : all_elements_) {
                // check_children_accum(i);
                BOOST_ASSERT(state_.check_node(i, g, b));
                ++i;
            }
        }

        {
            if (sorted_) {
                auto res = std::is_sorted(std::begin(candidate_elements_), std::end(candidate_elements_), candidate_cmp());
                BOOST_ASSERT(res);

            }
        }
    }
#endif // NDEBUG

// ----------------------------------------------------------------------------------------
//  Invariant Checks (End)
// ----------------------------------------------------------------------------------------

private:
    struct nested_t {
        explicit
        nested_t(partially_indexed& x)
            : outer_(x)
        {}

        partially_indexed& outer() {
            return outer_;
        }

        partially_indexed const& outer() const {
            return outer_;
        }

    private:
        partially_indexed& outer_;
    };

    struct nested_const_t {
        explicit
        nested_const_t(partially_indexed const& x)
            : outer_(x)
        {}

        partially_indexed const& outer() const {
            return outer_;
        }

    private:
        partially_indexed const& outer_;
    };

    struct candidate_cmp_t : nested_const_t {
        using nested_const_t::outer;
        using nested_const_t::nested_const_t;

        bool operator()(main_index_t a, main_index_t b) const {
            auto const& elem_a = outer().all_elements_[a].element();
            auto const& elem_b = outer().all_elements_[b].element();
            return outer().cmp_(elem_a, elem_b);
        }
    };

    struct remover_t : nested_t {
        using nested_t::outer;
        using nested_t::nested_t;

        void operator()(main_index_t i) {
            auto& node = outer().all_elements_[i];
            outer().candidate_elements_.erase(node.index());
            node.set_index(outer().null_index_);
        }
    };

    struct inserter_t : nested_t {
        using nested_t::outer;
        using nested_t::nested_t;

        void operator()(main_index_t i) {
            auto& node = outer().all_elements_[i];

            // candidate_elements_.push_back(i);
            // auto new_cand_index = std::prev(std::end(candidate_elements_));
            // node.set_index(new_cand_index);

            // auto start = std::chrono::high_resolution_clock::now();
            auto it = std::upper_bound(std::begin(outer().candidate_elements_), std::end(outer().candidate_elements_), i, outer().candidate_cmp());
            // auto end = std::chrono::high_resolution_clock::now();
            // auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            // binary_search_time += time_ns;

            if (it == std::end(outer().candidate_elements_)) {
                outer().candidate_elements_.push_back(i);
                auto cand_index = std::prev(std::end(outer().candidate_elements_));
                node.set_index(cand_index);
            } else {
                auto cand_index = outer().candidate_elements_.insert(it, i);
                node.set_index(cand_index);
            }
        }
    };

    struct getter_t : nested_t {
        using nested_t::outer;
        using nested_t::nested_t;

        std::pair<bool, T&> operator()(main_index_t i) {
            auto& node = outer().all_elements_[i];
            return {node.index() != outer().null_index_, node.element()};
        }
    };

    struct getter_const_t : nested_const_t {
        using nested_const_t::outer;
        using nested_const_t::nested_const_t;

        std::pair<bool, T const&> operator()(main_index_t i) const {
            auto& node = outer().all_elements_[i];
            return {node.index() != outer().null_index_, node.element()};
        }
    };

    struct bounds_ok_t : nested_const_t {
        using nested_const_t::outer;
        using nested_const_t::nested_const_t;

        bool operator()(main_index_t i) const {
            return i < outer().all_elements_.size();
        }
    };

    struct sorter_t : nested_t {
        using nested_t::outer;
        using nested_t::nested_t;

        void operator()(main_index_t index, internal_value_type const& x, candidate_index_t from, candidate_index_t to) {
            auto new_pos = std::upper_bound(from, to, index, outer().candidate_cmp());
            outer().candidate_elements_.splice(new_pos, outer().candidate_elements_, x.index());
        }
    };

    struct re_sort_left_t : nested_t {
        using nested_t::outer;
        using nested_t::nested_t;

        void operator()(main_index_t index) {
            auto const& node = outer().all_elements_[index];
            auto from = std::begin(outer().candidate_elements_);
            auto to = node.index();
            outer().sorter()(index, node, from, to);
        }
    };

    struct re_sort_right_t : nested_t {
        using nested_t::outer;
        using nested_t::nested_t;

        void operator()(main_index_t index) {
            auto const& node = outer().all_elements_[index];
            auto from = std::next(node.index());
            auto to = std::end(outer().candidate_elements_);
            outer().sorter()(index, node, from, to);
        }
    };

    struct re_sort_to_end_t : nested_t {
        using nested_t::outer;
        using nested_t::nested_t;

        void operator()(main_index_t from_index, main_index_t find_index) {
            auto const& find_node = outer().all_elements_[find_index];
            auto const& from_node = outer().all_elements_[from_index];

            auto from = std::next(from_node.index());
            auto to = std::end(outer().candidate_elements_);
            outer().sorter()(find_index, find_node, from, to);
        }
    };

    struct re_sort_from_begin_t : nested_t {
        using nested_t::outer;
        using nested_t::nested_t;

        void operator()(main_index_t to_index, main_index_t find_index) {
            auto const& find_node = outer().all_elements_[find_index];
            auto const& to_node = outer().all_elements_[to_index];

            auto from = std::begin(outer().candidate_elements_);
            auto to = to_node.index();
            outer().sorter()(find_index, find_node, from, to);
        }
    };

    struct re_sort_t : nested_t {
        using nested_t::outer;
        using nested_t::nested_t;

        void operator()(main_index_t from_index, main_index_t to_index, main_index_t find_index) {
            auto const& find_node = outer().all_elements_[find_index];
            auto const& from_node = outer().all_elements_[from_index];
            auto const& to_node = outer().all_elements_[to_index];

            auto from = std::next(from_node.index());
            auto to = to_node.index();
            outer().sorter()(find_index, find_node, from, to);
        }
    };

    template <typename I>
    struct reverser_t : nested_t {
        using nested_t::outer;

        reverser_t(partially_indexed& x, I f, I l)
            : nested_t(x)
            , f(f)
            , l(l)
        {}

        bool has_next() const {
            return f != l;
        }

        std::pair<main_index_t, T&> next() {
            --l;
            return {*l, outer().all_elements_[*l].element()};
        }

    private:
        I const f;
        I l;
    };

    template <typename I>
    reverser_t<I> reverser(partially_indexed& x, I f, I l) {
        return reverser_t<I>(x, f, l);
    }

    reverser_t<candidate_index_t> reverser() {
        return reverser(*this, std::begin(candidate_elements_), std::end(candidate_elements_));
    }

    candidate_cmp_t candidate_cmp() const {
        return candidate_cmp_t{*this};
    }

    sorter_t sorter() {
        return sorter_t{*this};
    }

    getter_t getter() {
        return getter_t{*this};
    }

    remover_t remover() {
        return remover_t{*this};
    }

    inserter_t inserter() {
        return inserter_t{*this};
    }

    re_sort_left_t re_sort_left() {
        return re_sort_left_t{*this};
    }

    re_sort_right_t re_sort_right() {
        return re_sort_right_t{*this};
    }

    re_sort_to_end_t re_sort_to_end() {
        return re_sort_to_end_t{*this};
    }

    re_sort_t re_sort() {
        return re_sort_t{*this};
    }

    re_sort_from_begin_t re_sort_from_begin() {
        return re_sort_from_begin_t{*this};
    }

    getter_const_t getter_const() const{
        return getter_const_t{*this};
    }

    bounds_ok_t bounds_ok() const{
        return bounds_ok_t{*this};
    }

    internal_value_type& insert_main_element(candidate_index_t index, T const& x) {
        all_elements_.emplace_back(index, x);
        return all_elements_.back();
    }

    internal_value_type& insert_main_element(candidate_index_t index, T&& x) {
        all_elements_.emplace_back(index, std::move(x));
        return all_elements_.back();
    }

    template <typename... Args>
    internal_value_type& insert_main_element(candidate_index_t index, Args&&... args) {
        all_elements_.emplace_back(index, std::forward<Args>(args)...);
        return all_elements_.back();
    }

    void re_add_node(main_index_t index) {
        auto& elem = all_elements_[index];

        if (state_.re_add_node(index, elem.element())) {
            insert_candidate(index, elem);
        } else {
            BOOST_ASSERT(false);
        }
    }


    // std::cout << all_elements_[*cand_index].element().fee() << "\n";
    // for (auto mi : candidate_elements_) {
    //     auto ci = all_elements_[mi].index();
    //     std::cout << all_elements_[mi].element().fee() << "\n";
    //     if (mi == *ci) {
    //         std::cout << "OK\n";
    //     } else {
    //         std::cout << "Error\n";
    //     }
    // }


    template <typename... Args>
    bool insert_internal(Args&&... args) {
        auto const main_index = all_elements_.size();
        auto& inserted = insert_main_element(null_index_, std::forward<Args>(args)...);
        return insert_candidate(main_index, inserted);
    }

    bool insert_candidate(main_index_t main_index, internal_value_type& inserted) {
        //TODO(fernando): See if we could use an efficient algorithms if the non-sorted elements are few...

        if ( ! sorted_) {
            if (state_.has_room_for(inserted.element()) ) {
                candidate_elements_.push_back(main_index);
                auto cand_index = std::prev(std::end(candidate_elements_));
                inserted.set_index(cand_index);
                state_.accumulate(inserted.element(), getter());
                return true;
            }
            candidate_elements_.sort(candidate_cmp());
            sorted_ = true;
            return state_.remove_insert_one(inserted.element(), main_index, reverser(), remover(), getter(), inserter(), re_sort_left(), re_sort_right(), re_sort_to_end(), re_sort(), re_sort_from_begin());
        }

        return state_.remove_insert_several(inserted.element(), main_index, reverser(), remover(), getter(), inserter(), re_sort_left(), re_sort_right(), re_sort_to_end(), re_sort(), re_sort_from_begin());
    }

private:
    indexes_container_t candidate_elements_;
    main_container_t all_elements_;
    candidate_index_t const null_index_;    //TODO(fernando): const iterator??
    bool sorted_;
    Cmp cmp_;
    State& state_;
};

}  // namespace mining
}  // namespace kth

#endif  //KTH_BLOCKCHAIN_MINING_PARTIALLY_INDEXED_HPP_
