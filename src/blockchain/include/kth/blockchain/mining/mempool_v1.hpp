// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_MINING_MEMPOOL_V1_HPP_
#define KTH_BLOCKCHAIN_MINING_MEMPOOL_V1_HPP_

#include <algorithm>
#include <chrono>
#include <print>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef NDEBUG
#include <iomanip>
#endif

// #include <boost/bimap.hpp>

#include <kth/mining/common.hpp>
#include <kth/mining/node_v1.hpp>
#include <kth/mining/prioritizer.hpp>

#include <kth/domain.hpp>


template <typename F>
auto scope_guard(F&& f) {
    // return std::unique_ptr<void, typename std::decay<F>::type>{(void*)1, std::forward<F>(f)};
    return std::unique_ptr<void, typename std::decay<F>::type>{reinterpret_cast<void*>(1), std::forward<F>(f)};
}

namespace kth {
namespace mining {

// inline
// node make_node(domain::chain::transaction const& tx) {
//     return node(transaction_element(tx));
// }

inline
node make_node(domain::chain::transaction const& tx) {
    return node(
                transaction_element(tx.hash()
#if ! defined(KTH_CURRENCY_BCH)
                                  , tx.hash(true)
#endif
                                  , tx.to_data(true, KTH_WITNESS_DEFAULT)
                                  , tx.fees()
                                  , tx.signature_operations()
                                  , tx.outputs().size())
                        );
}

#ifdef KTH_MINING_STATISTICS_ENABLED
template <typename F>
void measure(F f, measurements_t& t) {
    auto start = std::chrono::high_resolution_clock::now();
    f();
    auto end = std::chrono::high_resolution_clock::now();
    t += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    // auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    // t += double(time_ns);
}
#else
template <typename F>
void measure(F f, measurements_t& /*unused*/) {
    f();
}
#endif


template <typename F, typename Container, typename Cmp = std::greater<typename Container::value_type>>
std::set<typename Container::value_type, Cmp> to_ordered_set(F f, Container const& to_remove) {
    std::set<typename Container::value_type, Cmp> ordered;
    for (auto const& i : to_remove) {
        ordered.insert(f(i));
    }
    return ordered;
}

using all_transactions_t = std::vector<node>;


#if defined(KTH_CURRENCY_BCH)
inline
void sort_ctor(all_transactions_t& all, std::vector<size_t>& candidates) {
    auto const cmp = [&all](index_t ia, index_t ib) {
        auto const& a = all[ia];
        auto const& b = all[ib];
        return std::lexicographical_compare(a.txid().rbegin(), a.txid().rend(),
                                            b.txid().rbegin(), b.txid().rend());
    };
    // candidates.sort(cmp);
    std::sort(std::begin(candidates), std::end(candidates), cmp);
}

#else

inline
void sort_ltor(bool sorted, all_transactions_t& all, std::vector<size_t>& candidates) {

    if ( ! sorted) {
        auto const cmp = [&all](index_t ia, index_t ib) {
            auto const& a = all[ia];
            auto const& b = all[ib];
            // return fee_per_size_cmp{}(a, b);
            auto const value_a = static_cast<double>(a.children_fees()) / a.children_size();
            auto const value_b = static_cast<double>(b.children_fees()) / b.children_size();
            return value_b < value_a;
        };
        // candidates.sort(cmp);
        std::sort(std::begin(candidates), std::end(candidates), cmp);
    }

    auto last_organized = std::begin(candidates);

    while (last_organized != std::end(candidates)) {
        auto selected_to_move = last_organized++;

        auto most_left_child = std::find_if(std::begin(candidates), selected_to_move,
            [&](index_t s) {
                auto const& parent_node = all[*selected_to_move];
                auto found = std::find(std::begin(parent_node.children()),
                                       std::end(parent_node.children()),
                                       s);
                return found != std::end(parent_node.children());
        });

        if (most_left_child != selected_to_move) {
            //TODO(fernando): swap elements
            auto removed = *selected_to_move;
            candidates.erase(selected_to_move);
            candidates.insert(most_left_child, removed);

        }
    }
}
#endif // defined(KTH_CURRENCY_BCH)




// static
// void sort_ltor( std::vector<kth::mining::node>& all, kth::mining::indexes_t& candidates ){
//     auto last_organized = candidates.begin();

//     while (last_organized != candidates.end()){
//         auto selected_to_move = last_organized;
//         ++last_organized;

//         auto most_left_child = std::find_if(candidates.begin(), selected_to_move, [&] (index_t s) -> bool {
//                     auto const& parent_node = all[*selected_to_move];
//                     auto found = std::find( parent_node.children().begin(), parent_node.children().end(), s);
//                     return found != parent_node.children().end();}
//         );

//         if( most_left_child != selected_to_move ){
//             //move to the left of most_left_child
//             //candidates.splice(most_left_child, candidates, selected_to_move);

//             //TODO (rama): replace with list implementation
//             auto removed = *selected_to_move;
//             candidates.erase(selected_to_move);
//             candidates.insert(most_left_child, removed);
//         }
//     }
// }


class mempool {
public:

#ifndef NDEBUG
    void print_candidates() const {
        std::println("candidate_transactions_.size() {}", candidate_transactions_.size());
        std::println("all_transactions_.size()       {}", all_transactions_.size());

        {
            size_t index = 0;
            for (auto mi : candidate_transactions_) {
                std::print("{:02}, ", index);
                ++index;
            }
            std::println("");
        }

        for (auto mi : candidate_transactions_) {
            std::print("{:02}, ", mi.index());
        }
        std::println("");

        {
            size_t index = 0;
            for (auto const& mi : all_transactions_) {
                std::print("{:02}, ", index);
                ++index;
            }
            std::println("");
        }

        for (auto const& e : all_transactions_) {
            if (e.candidate_index() == null_index) {
                std::print("XX, ");
            } else {
                std::print("{:02}, ", e.candidate_index());
            }
        }
        std::println("");
    }
#endif // NDEBUG

    // class candidate_index_t;
    // friend candidate_index_t;

    class candidate_index_t {
    public:
        explicit
        candidate_index_t(size_t index)
            : index_(index)
        {}

        candidate_index_t(candidate_index_t const& x) = default;
        candidate_index_t(candidate_index_t&& x) = default;

        candidate_index_t& operator=(candidate_index_t&& x) noexcept {
            using std::swap;
            auto xci = all_transactions()[x.index_].candidate_index();
            auto tci = all_transactions()[index_].candidate_index();
            all_transactions()[x.index_].set_candidate_index(tci);
            all_transactions()[index_].set_candidate_index(xci);
            swap(index_, x.index_);

            return *this;
        }

        explicit
        operator size_t() const {
            return index_;
        }

        size_t index() const {
            return index_;
        }

        friend
        bool operator<(candidate_index_t& a, candidate_index_t& b) {
            return a.index_ < b.index_;
        }

        friend
        bool operator>(candidate_index_t& a, candidate_index_t& b) {
            return b < a;
        }

        friend
        bool operator<=(candidate_index_t& a, candidate_index_t& b) {
            return !(b < a);
        }

        friend
        bool operator>=(candidate_index_t& a, candidate_index_t& b) {
            return !(a < b);
        }

        friend
        void swap(candidate_index_t& a, candidate_index_t& b) {
            using std::swap;
            auto tmp = all_transactions()[a.index()].candidate_index();
            all_transactions()[a.index()].set_candidate_index(all_transactions()[b.index()].candidate_index());
            all_transactions()[b.index()].set_candidate_index(tmp);
            swap(a.index_, b.index_);
        }

        friend mempool;
    private:

        static
        all_transactions_t& all_transactions() {
             return parent_->all_transactions_;
        }

        static mempool* parent_;
        size_t index_;
    };


    // using indexes_t = std::vector<index_t>;
    using candidate_indexes_t = std::vector<candidate_index_t>;


    using to_insert_t = std::tuple<indexes_t, uint64_t, size_t, size_t>;
    // using to_insert_t = std::tuple<indexes_t, uint64_t, size_t, size_t, indexes_t, uint64_t, size_t, size_t>;
    using accum_t = std::tuple<uint64_t, size_t, size_t>;
    using internal_utxo_set_t = std::unordered_map<domain::chain::point, domain::chain::output>;
    using previous_outputs_t = std::unordered_map<domain::chain::point, index_t>;
    using hash_index_t = std::unordered_map<hash_digest, std::pair<index_t, domain::chain::transaction>>;

    // using mutex_t = boost::shared_mutex;
    // using shared_lock_t = boost::shared_lock<mutex_t>;
    // using unique_lock_t = boost::unique_lock<mutex_t>;
    // using upgrade_lock_t = boost::upgrade_lock<mutex_t>;

    static constexpr size_t max_template_size_default = get_max_block_weight() - coinbase_reserved_size;

#if defined(KTH_CURRENCY_BCH)
    static constexpr size_t mempool_size_multiplier_default = 10;
#elif defined(KTH_CURRENCY_BTC)
    static constexpr size_t mempool_size_multiplier_default = 10;
#else
    static constexpr size_t mempool_size_multiplier_default = 10;
#endif

    explicit
    mempool(size_t max_template_size = max_template_size_default, size_t mempool_size_multiplier = mempool_size_multiplier_default)
        : max_template_size_(max_template_size)
        // , mempool_size_multiplier_(mempool_size_multiplier)
        , mempool_total_size_(get_max_block_weight() * mempool_size_multiplier)
        // , sorted_(false) {
        BOOST_ASSERT(max_template_size <= get_max_block_weight()); //TODO(fernando): what happend in BTC with SegWit.

        size_t const candidates_capacity = max_template_size_ / min_transaction_size_for_capacity;
        size_t const all_capacity = mempool_total_size_ / min_transaction_size_for_capacity;

        candidate_transactions_.reserve(candidates_capacity);
        all_transactions_.reserve(all_capacity);

        // candidate_index_t::parent_ = *this;
        mempool::candidate_index_t::parent_ = this;
    }

    bool sorted() const {
        return sorted_;
    }

    void increment_time(std::chrono::time_point<std::chrono::high_resolution_clock> const& start, std::chrono::time_point<std::chrono::high_resolution_clock> const& end, double& accum) {
        auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        accum += double(time_ns);
    }

    double make_node_time = 0.0;
    double process_utxo_and_graph_time = 0.0;
    double all_transactions_push_back_time = 0.0;
    double insert_candidate_time = 0.0;
    double hash_index_find_time = 0.0;
    double check_double_spend_time = 0.0;
    double insert_outputs_in_utxo_time = 0.0;
    double hash_index_emplace_time = 0.0;

    double relatives_management_time = 0.0;
    double relatives_management_part_1_time = 0.0;
    double relatives_management_part_2_time = 0.0;
    double relatives_management_part_2_vector_copy_time = 0.0;
    double relatives_management_part_2_first_loop_time = 0.0;
    double relatives_management_part_2_remove_duplicates_time = 0.0;
    double relatives_management_part_2_new_node_add_parents_time = 0.0;
    double relatives_management_part_2_second_loop_time = 0.0;
    double relatives_management_part_2_second_loop_parent_add_child_time = 0.0;




    error::error_code_t add(domain::chain::transaction const& tx) {
        //precondition: tx.validation.state != nullptr
        //              tx is fully validated: check() && accept() && connect()
        //              ! tx.is_coinbase()

        // std::cout << encode_base16(tx.to_data(true, KTH_WITNESS_DEFAULT)) << std::endl;

        return prioritizer_.low_job([this, &tx]{
            auto const index = all_transactions_.size();

            auto start = std::chrono::high_resolution_clock::now();
            auto temp_node = make_node(tx);
            auto end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, make_node_time);

            start = std::chrono::high_resolution_clock::now();
            auto res = process_utxo_and_graph(tx, index, temp_node);
            end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, process_utxo_and_graph_time);

            if (res != error::success) {
                return res;
            }

            start = std::chrono::high_resolution_clock::now();
            all_transactions_.push_back(std::move(temp_node));
            end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, all_transactions_push_back_time);

            // res = add_node(index);
            node& inserted = all_transactions_.back();

            start = std::chrono::high_resolution_clock::now();
            res = insert_candidate(index, inserted);
            end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, insert_candidate_time);


    #ifndef NDEBUG
            check_invariant();
    #endif
            return res;
        });



//         auto const index = all_transactions_.size();

//         auto start = std::chrono::high_resolution_clock::now();
//         auto temp_node = make_node(tx);
//         auto end = std::chrono::high_resolution_clock::now();
//         increment_time(start, end, make_node_time);

//         start = std::chrono::high_resolution_clock::now();
//         auto res = process_utxo_and_graph(tx, index, temp_node);
//         end = std::chrono::high_resolution_clock::now();
//         increment_time(start, end, process_utxo_and_graph_time);

//         if (res != error::success) {
//             return res;
//         }

//         start = std::chrono::high_resolution_clock::now();
//         all_transactions_.push_back(std::move(temp_node));
//         end = std::chrono::high_resolution_clock::now();
//         increment_time(start, end, all_transactions_push_back_time);

//         // res = add_node(index);
//         node& inserted = all_transactions_.back();

//         start = std::chrono::high_resolution_clock::now();
//         res = insert_candidate(index, inserted);
//         end = std::chrono::high_resolution_clock::now();
//         increment_time(start, end, insert_candidate_time);


// #ifndef NDEBUG
//         check_invariant();
// #endif
//         return res;

    }

    // private
    void accumulate_non_sorted(node const& x) {
        for (auto pi : x.parents()) {
            auto& parent = all_transactions_[pi];
            if (parent.candidate_index() != null_index) {
                parent.increment_values(x.fee(), x.size(), x.sigops());
            }
        }

        accum_fees_ += x.fee();
        accum_size_ += x.size();
        accum_sigops_ += x.sigops();
    }

    //private
    error::error_code_t insert_candidate(index_t main_index, node& inserted) {
        if ( ! sorted_) {
            if (has_room_for(inserted.size(), inserted.sigops())) {
                candidate_transactions_.push_back(candidate_index_t{main_index});
                auto cand_index = candidate_transactions_.size() - 1;
                inserted.set_candidate_index(cand_index);
                accumulate_non_sorted(inserted);
                return error::success;
            }

            std::println("************************** FIRST ITEM DOESNT FIT **************************");


            auto const cmp = [this](candidate_index_t a, candidate_index_t b) {
                return fee_per_size_cmp(a.index(), b.index());
            };

// #ifndef NDEBUG
//             check_invariant();
// #endif

            //TODO(fernando): measure sort and save stats
            std::sort(std::begin(candidate_transactions_), std::end(candidate_transactions_), cmp);
            // std::cout << "after sort (V1)" << std::endl;


// #ifndef NDEBUG
//             check_invariant();
// #endif


            sorted_ = true;
            // return state_.remove_insert_one(inserted.element(), main_index, reverser(), remover(), getter(), inserter(), re_sort_left(), re_sort_right(), re_sort_to_end(), re_sort(), re_sort_from_begin());
        }
        auto res = add_node(main_index);
        return res;

        // return state_.remove_insert_several(inserted.element(), main_index, reverser(), remover(), getter(), inserter(), re_sort_left(), re_sort_right(), re_sort_to_end(), re_sort(), re_sort_from_begin());
    }


    // TODO(review-Dario): This method is too long and complex, can it be simplified?
    template <typename I>
    error::error_code_t remove(I f, I l, size_t non_coinbase_input_count = 0) {
        // precondition: [f, l) is a valid non-empty range
        //               there are no coinbase transactions in the range

        if (all_transactions_.empty()) {
            return error::success;
        }

        // std::cout << "Arrive Block -------------------------------------------------------------------" << std::endl;
        // std::cout << encode_base16(tx.to_data(true, KTH_WITNESS_DEFAULT)) << std::endl;


        processing_block_ = true;
        auto unique = scope_guard([&](void*){ processing_block_ = false; });

        std::set<index_t, std::greater<>> to_remove;
        std::vector<domain::chain::point> outs;
        if (non_coinbase_input_count > 0) {
            outs.reserve(non_coinbase_input_count);   //TODO(fernando): unnecesary extra space
        }

        return prioritizer_.high_job([&f, l, &to_remove, &outs, this]{

            //TODO(fernando): temp code, remove
// #ifndef NDEBUG
//             auto old_transactions = all_transactions_;
// #endif

            while (f != l) {
                auto const& tx = *f;
                auto it = hash_index_.find(tx.hash());
                if (it != hash_index_.end()) {
                    auto const index = it->second.first;
                    auto& node = all_transactions_[index];

                    //TODO(fernando): check if children() is the complete descendence of node or if it just the inmediate parents.
                    for (auto ci : node.children()) {
                        auto& child = all_transactions_[ci];
                        child.remove_parent(index);
                    }
                    to_remove.insert(index);
                } else {
                    for (auto const& i : tx.inputs()) {
                        outs.push_back(i.previous_output());
                    }
                }
                ++f;
            }

            find_double_spend_issues(to_remove, outs);


            //TODO(fernando): process batches of adjacent elements
            for (auto i : to_remove) {
                auto it = std::next(all_transactions_.begin(), i);
                hash_index_.erase(it->txid());
                remove_from_utxo(it->txid(), it->output_count());

                if (i < all_transactions_.size() - 1) {
                    reindex_relatives(i + 1);
                }

                all_transactions_.erase(it);
            }

            BOOST_ASSERT(all_transactions_.size() == hash_index_.size());

// #ifndef NDEBUG
//             auto diff = old_transactions.size() - all_transactions_.size();

//             for (size_t i = 0; i < all_transactions_.size(); ++i) {
//                 auto& node = all_transactions_[i];
//                 auto& node_old = old_transactions[i + diff];

//                 indexes_t old_children;
//                 for (auto x : node_old.children()) {
//                     if (x >= diff) {
//                         old_children.push_back(x - diff);
//                     }
//                 }

//                 assert(node_old.children().size() >= node.children().size());
//                 assert(old_children.size() == node.children().size());

//                 for (size_t j = 0; j < old_children.size(); ++j) {
//                     assert(old_children[j] == node.children()[j]);
//                 }

//                 std::cout << std::endl;

//                 // if (node_old.parents().size() > 0) {
//                 //     std::cout << std::endl;
//                 // }


//                 indexes_t old_parents;
//                 for (auto x : node_old.parents()) {
//                     if (x >= diff) {
//                         old_parents.push_back(x - diff);
//                     }
//                 }

//                 assert(node_old.parents().size() >= node.parents().size());
//                 assert(old_parents.size() == node.parents().size());

//                 for (size_t j = 0; j < old_parents.size(); ++j) {
//                     assert(old_parents[j] == node.parents()[j]);
//                 }

//             }
// #endif


            sorted_ = false;
            candidate_transactions_.clear();
            previous_outputs_.clear();

            accum_fees_ = 0;
            accum_size_ = 0;
            accum_sigops_ = 0;

            // for (size_t i = 0; i < all_transactions_.size(); ++i) {
            //     all_transactions_[i].set_candidate_index(null_index);
            //     all_transactions_[i].reset_children_values();
            // }

            for (auto& atx : all_transactions_) {
                atx.set_candidate_index(null_index);
                atx.reset_children_values();
            }

// #ifndef NDEBUG
//             check_invariant();
// #endif

            for (size_t i = 0; i < all_transactions_.size(); ++i) {

                auto it = hash_index_.find(all_transactions_[i].txid());
                if (it != hash_index_.end()) {
                    // if (it->second.first != i) {
                    //     std::cout << "pepe\n";
                    // }
                    it->second.first = i;
                }

                re_add_node(i);
// #ifndef NDEBUG
//                 check_invariant();
// #endif
            }

#ifndef NDEBUG
            check_invariant();
#endif

            return error::success;
        });
    }

    size_t capacity() const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([this]{
            return max_template_size_;
        });
    }

    size_t all_transactions() const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([this]{
            return all_transactions_.size();
        });
    }

    size_t candidate_transactions() const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([this]{
            return candidate_transactions_.size();
        });
    }

    size_t candidate_bytes() const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([this]{
            return accum_size_;
        });
    }

    size_t candidate_sigops() const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([this]{
            return accum_sigops_;
        });
    }

    uint64_t candidate_fees() const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([this]{
            return accum_fees_;
        });
    }

    //TODO(fernando):
    bool contains(hash_digest const& txid) const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([&txid, this]{
            auto it = hash_index_.find(txid);
            return it != hash_index_.end();
        });
    }

    hash_index_t get_validated_txs_high() const {
        return prioritizer_.high_job([this]{
            return hash_index_;
        });
    }

    hash_index_t get_validated_txs_low() const {
        return prioritizer_.low_job([this]{
            return hash_index_;
        });
    }

    bool is_candidate(domain::chain::transaction const& tx) const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([&tx, this]{
            auto it = hash_index_.find(tx.hash());
            if (it == hash_index_.end()) {
                return false;
            }

            return all_transactions_[it->second.first].candidate_index() != null_index;
        });
    }

    index_t candidate_rank(domain::chain::transaction const& tx) const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([&tx, this]{
            auto it = hash_index_.find(tx.hash());
            if (it == hash_index_.end()) {
                return null_index;
            }

            return all_transactions_[it->second.first].candidate_index();
        });
    }

    std::pair<std::vector<transaction_element>, uint64_t> get_block_template() const {
        //TODO(fernando): implement a cache, outside

        if (processing_block_) {
            return {};
        }

        auto copied_data = prioritizer_.high_job([this] {
            std::vector<size_t> candidates;
            candidates.reserve(candidate_transactions_.size());
            std::transform(std::begin(candidate_transactions_), std::end(candidate_transactions_), std::back_inserter(candidates),
                   [](candidate_index_t const& x) {
                       return x.index();
                    }
            );

            return make_tuple(std::move(candidates), all_transactions_, accum_fees_, sorted_);
            // return make_tuple(candidate_transactions_, all_transactions_, accum_fees_);
        });

        auto& candidates = std::get<0>(copied_data);
        auto& all = std::get<1>(copied_data);
        auto accum_fees = std::get<2>(copied_data);
        auto sorted = std::get<3>(copied_data);

// #if defined(KTH_CURRENCY_BCH)

//         auto const cmp = [this](size_t a, size_t b) {
//             return ctor_cmp(a, b);
//         };

//         // start = chrono::high_resolution_clock::now();
//         std::sort(std::begin(candidates), std::end(candidates), cmp);
//         // end = chrono::high_resolution_clock::now();
//         // auto sort_time = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
// #else
//         sort_ltor(all, candidates);
// #endif

#if defined(KTH_CURRENCY_BCH)
        sort_ctor(all, candidates);
#else
        sort_ltor(sorted, all, candidates);
#endif


        std::vector<transaction_element> res;
        res.reserve(candidates.size());

        for (auto i : candidates) {
            res.push_back(std::move(all[i].element()));
        }

        return {std::move(res), accum_fees};
    }

    domain::chain::output get_utxo(domain::chain::point const& point) const {
        // shared_lock_t lock(mutex_);
        return prioritizer_.low_job([&point, this]{
            auto it = internal_utxo_set_.find(point);
            if (it != internal_utxo_set_.end()) {
                return it->second;
            }

            return domain::chain::output{};
        });
    }


// ----------------------------------------------------------------------------------------
//  Invariant Checks
// ----------------------------------------------------------------------------------------
#ifndef NDEBUG

    void check_children_accum(index_t node_index) const {
        removal_list_t out_removed;
        auto res = out_removed.insert(node_index);
        if ( ! res.second) {
            return;
        }

        auto const& node = all_transactions_[node_index];
        auto fee = node.fee();
        auto size = node.size();
        auto sigops = node.sigops();

        if (node.candidate_index() != null_index) {
            for (auto child_index : node.children()) {
                auto const& child = all_transactions_[child_index];

                //To verify that the node is inside of the candidate list.
                if (child.candidate_index() != null_index) {
                    auto res = out_removed.insert(child_index);
                    if (res.second) {
                        fee += child.fee();
                        size += child.size();
                        sigops += child.sigops();
                    }
                }
            }
        }

        // if (node.children_fees() != fee) {
        //     std::cout << "node_index:           " << node_index << std::endl;
        //     std::cout << "node.children_fees(): " << node.children_fees() << std::endl;
        //     std::cout << "fee:                  " << fee << std::endl;

        //     std::cout << "Removed:  ";
        //     for (auto i : out_removed) {
        //         std::cout << i << ", ";
        //     }
        //     std::cout << std::endl;
        // }

        // if (node.children_size() != size) {
        //     std::cout << "node_index:           " << node_index << std::endl;
        //     std::cout << "node.children_size(): " << node.children_size() << std::endl;
        //     std::cout << "size:                 " << size << std::endl;

        //     std::cout << "Removed:  ";
        //     for (auto i : out_removed) {
        //         std::cout << i << ", ";
        //     }
        //     std::cout << std::endl;
        // }

        // if (node.children_sigops() != sigops) {
        //     std::cout << "node_index:             " << node_index << std::endl;
        //     std::cout << "node.children_sigops(): " << node.children_sigops() << std::endl;
        //     std::cout << "sigops:                 " << sigops << std::endl;


        //     std::cout << "Removed:  ";
        //     for (auto i : out_removed) {
        //         std::cout << i << ", ";
        //     }
        //     std::cout << std::endl;
        // }

        KTH_ASSERT(node.children_fees() == fee);
        KTH_ASSERT(node.children_size() == size);
        KTH_ASSERT(node.children_sigops() == sigops);
    }


    void check_invariant() const {
        // std::cout << "**********************************" << std::endl;
        // print_candidates();
        // std::cout << "**********************************" << std::endl;

        check_invariant_partial();

        //TODO(fernando): replicate this invariant test in V2
        {
            for (size_t i = 0; i < all_transactions_.size(); ++i) {
                auto it = hash_index_.find(all_transactions_[i].txid());
                BOOST_ASSERT(it != hash_index_.end());
                BOOST_ASSERT(it->second.first == i);
            }
        }

        {
            size_t i = 0;
            for (auto const& node : all_transactions_) {
                if (node.candidate_index() != null_index) {
                    for (auto pi : node.parents()) {
                        auto const& parent = all_transactions_[pi];
                        BOOST_ASSERT(parent.candidate_index() != null_index);
                    }
                }
                ++i;
            }
        }

        {
            // size_t ci = 0;
            for (auto i : candidate_transactions_) {
                auto const& node = all_transactions_[i.index()];
                // BOOST_ASSERT(ci == node.candidate_index());
                // ++ci;
                check_children_accum(i.index());
            }
        }

        {
            size_t i = 0;
            for (auto const& node : all_transactions_) {
                check_children_accum(i);
                ++i;
            }
        }


        {
            if (sorted_) {
                auto const cmp = [this](candidate_index_t a, candidate_index_t b) {
                    return fee_per_size_cmp(a.index(), b.index());
                };

                auto res = std::is_sorted(std::begin(candidate_transactions_), std::end(candidate_transactions_), cmp);

                // if ( !  res) {
                    // auto res2 = std::is_sorted(candidate_transactions_.begin(), candidate_transactions_.end(), cmp);
                    // std::cout << res2;
                // }

                BOOST_ASSERT(res);
            }
        }

        // **FER**
        {
            for (auto const& p : hash_index_) {
                auto const& tx_cached = p.second.second;
                for (size_t i = 0; i < tx_cached.inputs().size(); ++i) {
                    auto const& output_cache = tx_cached.inputs()[i].previous_output().validation.cache;
                    if ( ! output_cache.is_valid()) {
                        BOOST_ASSERT(false);
                    }
                }

            }
        }




    }

    void check_invariant_consistency_full() const {
        check_invariant_consistency_partial();

        {
            std::vector<size_t> all_sorted;
            for (auto const& node : all_transactions_) {
                if (node.candidate_index() != null_index) {
                    all_sorted.push_back(node.candidate_index());
                }
            }
            std::sort(all_sorted.begin(), all_sorted.end());
            auto last = std::unique(all_sorted.begin(), all_sorted.end());
            BOOST_ASSERT(std::distance(all_sorted.begin(), last) == all_sorted.size());
        }

        {
            size_t ci = 0;
            for (auto i : candidate_transactions_) {
                auto const& node = all_transactions_[i.index()];
                BOOST_ASSERT(ci == node.candidate_index());
                ++ci;
            }
        }
    }

    void check_invariant_consistency_partial() const {

        {
            for (auto const& node : all_transactions_) {
                if (node.candidate_index() != null_index && node.candidate_index() >= candidate_transactions_.size()) {
                    BOOST_ASSERT(false);
                }
            }
        }

        {
            std::vector<size_t> ci_sorted;

            for (auto ci : candidate_transactions_) {
                ci_sorted.push_back(ci.index());
            }

            std::sort(ci_sorted.begin(), ci_sorted.end());
            auto last = std::unique(ci_sorted.begin(), ci_sorted.end());
            BOOST_ASSERT(std::distance(ci_sorted.begin(), last) == ci_sorted.size());
        }

        // {
        //     std::vector<size_t> all_sorted;
        //     for (auto const& node : all_transactions_) {
        //         if (node.candidate_index() != null_index) {
        //             all_sorted.push_back(node.candidate_index());
        //         }
        //     }
        //     std::sort(all_sorted.begin(), all_sorted.end());
        //     auto last = std::unique(all_sorted.begin(), all_sorted.end());
        //     BOOST_ASSERT(std::distance(all_sorted.begin(), last) == all_sorted.size());
        // }

        {
            size_t ci = 0;
            for (auto i : candidate_transactions_) {
                auto const& node = all_transactions_[i.index()];
                BOOST_ASSERT(node.candidate_index() == null_index || ci == node.candidate_index());
                ++ci;
            }
        }

        // {
        //     // size_t ci = 0;
        //     for (auto i : candidate_transactions_) {
        //         auto const& node = all_transactions_[i.index()];
        //         // BOOST_ASSERT(ci == node.candidate_index());
        //         // ++ci;
        //         check_children_accum(i.index());
        //     }
        // }

    }

    // TODO(review-Dario): This method is too long, wouldn't it be more readable breaking it into inlined submethods?
    void check_invariant_partial() const {

        BOOST_ASSERT(candidate_transactions_.size() <= all_transactions_.size());


        // {
        //     for (auto i : candidate_transactions_) {
        //         auto const& node = all_transactions_[i];

        //         if (node.candidate_index() != null_index && node.candidate_index() >= all_transactions_.size()) {
        //             BOOST_ASSERT(false);
        //         }
        //     }
        // }

        {
            for (auto i : candidate_transactions_) {
                if (i.index() >= all_transactions_.size()) {
                    BOOST_ASSERT(false);
                }
            }
        }

        {
            for (auto i : candidate_transactions_) {
                auto const& node = all_transactions_[i.index()];

                for (auto ci : node.children()) {
                    if (ci >= all_transactions_.size()) {
                        BOOST_ASSERT(false);
                    }
                }
            }
        }

        {
            for (auto i : candidate_transactions_) {
                auto const& node = all_transactions_[i.index()];

                for (auto pi : node.parents()) {
                    if (pi >= all_transactions_.size()) {
                        BOOST_ASSERT(false);
                    }
                }
            }
        }

        {
            for (auto const& node : all_transactions_) {
                if (node.candidate_index() != null_index && node.candidate_index() >= candidate_transactions_.size()) {
                    BOOST_ASSERT(false);
                }
            }
        }

        // {
        //     std::vector<size_t> ci_sorted;

        //     for (auto ci : candidate_transactions_) {
        //         ci_sorted.push_back(ci);
        //     }


        //     std::sort(ci_sorted.begin(), ci_sorted.end());
        //     auto last = std::unique(ci_sorted.begin(), ci_sorted.end());
        //     BOOST_ASSERT(std::distance(ci_sorted.begin(), last) == ci_sorted.size());
        // }

        // {
        //     std::vector<size_t> all_sorted;
        //     for (auto const& node : all_transactions_) {
        //         if (node.candidate_index() != null_index) {
        //             all_sorted.push_back(node.candidate_index());
        //         }
        //     }
        //     std::sort(all_sorted.begin(), all_sorted.end());
        //     auto last = std::unique(all_sorted.begin(), all_sorted.end());
        //     BOOST_ASSERT(std::distance(all_sorted.begin(), last) == all_sorted.size());
        // }

        // {
        //     size_t ci = 0;
        //     for (auto i : candidate_transactions_) {
        //         auto const& node = all_transactions_[i.index()];
        //         BOOST_ASSERT(ci == node.candidate_index());
        //         ++ci;
        //     }
        // }

        check_invariant_consistency_full();

        {
            size_t i = 0;
            size_t non_indexed = 0;
            for (auto const& node : all_transactions_) {
                if (node.candidate_index() != null_index) {
                    BOOST_ASSERT(candidate_transactions_[node.candidate_index()].index() == i);
                    BOOST_ASSERT(node.candidate_index() < candidate_transactions_.size());
                } else {
                    ++non_indexed;
                }
                ++i;
            }

            BOOST_ASSERT(candidate_transactions_.size() + non_indexed == all_transactions_.size());
        }
    }

#endif // NDEBUG

// ----------------------------------------------------------------------------------------
//  Invariant Checks (End)
// ----------------------------------------------------------------------------------------

private:

    void reindex_relatives(size_t index) {

        for (auto& node : all_transactions_) {

            for (auto& ci : node.children()) {
                if (ci >= index) {
                    --ci;
                }
            }

            for (auto& pi : node.parents()) {
                if (pi >= index) {
                    --pi;
                }
            }
        }
    }

    void re_add_node(index_t index) {
        auto& elem = all_transactions_[index];

        auto it = hash_index_.find(elem.txid());
        if (it != hash_index_.end()) {

            it->second.first = index;
            auto const& tx = it->second.second;

            for (auto const& i : tx.inputs()) {
                previous_outputs_.insert({i.previous_output(), index});
            }
            // add_node(index);
            insert_candidate(index, elem);
        } else {
            //No debería pasar por aca
            BOOST_ASSERT(false);
        }
    }

    error::error_code_t add_node(index_t index) {
        //TODO(fernando): what_to_insert_time
        auto to_insert = what_to_insert(index);

        // if (candidate_transactions_.size() > 0 && ! has_room_for(std::get<2>(to_insert), std::get<3>(to_insert))) {
        if (  ! candidate_transactions_.empty() && ! has_room_for(std::get<2>(to_insert), std::get<3>(to_insert))) {
            //TODO(fernando): what_to_remove_time
            auto to_remove = what_to_remove(index, std::get<1>(to_insert), std::get<2>(to_insert), std::get<3>(to_insert));

#ifndef NDEBUG
            for (auto const& wtr : to_remove) {
                for (auto const& wti : std::get<0>(to_insert)) {
                    if (wtr == wti) {
                        BOOST_ASSERT(false);
                    }
                }
            }
#endif
            if (to_remove.empty()) {
                // ++low_benefit_tx_counter;
                return error::low_benefit_transaction;
            }

            do_candidate_removal(to_remove);
        }

        do_candidates_insertion(to_insert);

        return error::success;
    }

    void clean_parents(mining::node const& node, index_t index) {
        for (auto pi : node.parents()) {
            auto& parent = all_transactions_[pi];
            parent.remove_child(index);
        }
    }

    void find_double_spend_issues(std::set<index_t, std::greater<>>& to_remove, std::vector<domain::chain::point> const& outs) {

        for (auto const& po : outs) {
            auto it = previous_outputs_.find(po);
            if (it != previous_outputs_.end()) {
                index_t index = it->second;
                auto const& node = all_transactions_[index];

                to_remove.insert(index);
                clean_parents(node, index);

                for (auto ci : node.children()) {
                    auto& child = all_transactions_[ci];
                    to_remove.insert(ci);
                    clean_parents(child, ci);
                }
            }
        }
    }

    void remove_from_utxo(hash_digest const& txid, uint32_t output_count) {
        for (uint32_t i = 0; i < output_count; ++i) {
            internal_utxo_set_.erase(domain::chain::point{txid, i});
        }

        //TODO(fernando): Do I have to insert the prevouts removed before??
    }

    bool fee_per_size_cmp(index_t a, index_t b) const {
        auto const& node_a = all_transactions_[a];
        auto const& node_b = all_transactions_[b];

        auto const value_a = static_cast<double>(node_a.children_fees()) / node_a.children_size();
        auto const value_b = static_cast<double>(node_b.children_fees()) / node_b.children_size();

        return value_b < value_a;
    }

#if defined(KTH_CURRENCY_BCH)
    bool ctor_cmp(index_t a, index_t b) const {
        auto const& node_a = all_transactions_[a];
        auto const& node_b = all_transactions_[b];
        return std::lexicographical_compare(node_a.txid().rbegin(), node_a.txid().rend(),
                                            node_b.txid().rbegin(), node_b.txid().rend());
    }
#endif

    accum_t get_accum(removal_list_t& out_removed, index_t node_index) const {
        auto res = out_removed.insert(node_index);
        if ( ! res.second) {
            return {0, 0, 0};
        }

        auto const& node = all_transactions_[node_index];
        auto fee = node.fee();
        auto size = node.size();
        auto sigops = node.sigops();

        for (auto child_index : node.children()) {
            auto const& child = all_transactions_[child_index];

            //To verify that the node is inside of the candidate list.
            if (child.candidate_index() != null_index) {
                auto res = out_removed.insert(child_index);
                if (res.second) {
                    fee += child.fee();
                    size += child.size();
                    sigops += child.sigops();
                }
            }
        }

        return {fee, size, sigops};
    }

    to_insert_t what_to_insert(index_t node_index) const {
        indexes_t to_insert_no_inserted;

        auto const& node = all_transactions_[node_index];
        auto fees = node.fee();
        auto size = node.size();
        auto sigops = node.sigops();

        to_insert_no_inserted.push_back(node_index);

        for (auto pi : node.parents()) {
            auto const& parent = all_transactions_[pi];
            if (parent.candidate_index() == null_index) {
                fees += parent.fee();
                size += parent.size();
                sigops += parent.sigops();
                to_insert_no_inserted.push_back(pi);
            }
        }

        return {std::move(to_insert_no_inserted), fees, size, sigops};
    }

    bool shares_parents(mining::node const& to_insert_node, index_t remove_candidate_index) const {
        auto const& parents = to_insert_node.parents();
        auto it = std::find(parents.begin(), parents.end(), remove_candidate_index);
        return it != parents.end();
    }

    removal_list_t what_to_remove(index_t to_insert_index, uint64_t fees, size_t size, size_t sigops) const {
        //precondition: candidate_transactions_.size() > 0

        auto pack_benefit = static_cast<double>(fees) / size;

        auto it = candidate_transactions_.end() - 1;

        uint64_t fee_accum = 0;
        size_t size_accum = 0;

        auto next_size = accum_size_;
        auto next_sigops = accum_sigops_;

        removal_list_t removed;

        while (true) {
            auto elem_index = *it;
            auto const& elem = all_transactions_[elem_index.index()];
            auto const& to_insert_elem = all_transactions_[to_insert_index];

            //TODO(fernando): Do I have to check if elem_idex is any of the to_insert elements
            bool shares = shares_parents(to_insert_elem, elem_index.index());

            if ( ! shares) {
                auto res = get_accum(removed, elem_index.index());
                if (std::get<1>(res) != 0) {
                    fee_accum += std::get<0>(res);
                    size_accum += std::get<1>(res);

                    auto to_remove_benefit = static_cast<double>(fee_accum) / size_accum;
                    if (pack_benefit <= to_remove_benefit) {
                        return {};
                    }

                    next_size -= std::get<1>(res);
                    next_sigops -= std::get<2>(res);

                    if (next_size + size <= max_template_size_) {
                        auto const sigops_limit = get_allowed_sigops(next_size);
                        if (next_sigops + sigops <= sigops_limit) {
                            return removed;
                        }
                    }
                }
            }

            if (it == candidate_transactions_.begin()) break;
            --it;
        }

        return removed;
    }

    void do_candidate_removal(removal_list_t const& to_remove) {
        // removed_tx_counter += to_remove.size();

        //TODO(fernando): remove_time
        remove_nodes(to_remove);

        //TODO(fernando): reindex_parents_for_removal_time
        reindex_parents_for_removal(to_remove);


    }

    void do_candidates_insertion(to_insert_t const& to_insert) {

        for (auto i : std::get<0>(to_insert)) {
            insert_in_candidate(i, std::get<0>(to_insert));

// #ifndef NDEBUG
//             check_invariant_partial();
// #endif
        }

        accum_fees_ += std::get<1>(to_insert);
        accum_size_ += std::get<2>(to_insert);
        accum_sigops_ += std::get<3>(to_insert);
    }

    bool has_room_for(size_t size, size_t sigops) const {
        if (accum_size_ > max_template_size_ - size) {
            return false;
        }

        auto const next_size = accum_size_ + size;
        auto const sigops_limit = get_allowed_sigops(next_size);

        // if (accum_sigops_ > sigops_limit - sigops) {
        //     return false;
        // }
        // return true;

        return (accum_sigops_ <= sigops_limit - sigops);
    }

    template <typename Container>
    void remove_duplicates(Container& cont) {
        std::sort(std::begin(cont), std::end(cont), std::greater<>{});
        cont.erase(std::unique(std::begin(cont), std::end(cont)), std::end(cont));
    }

    void relatives_management_part_2(indexes_t const& parents, index_t node_index, node& new_node) {
        if ( ! parents.empty()) {
            // std::set<index_t, std::greater<>> parents_temp(parents.begin(), parents.end());
            // for (auto pi : parents) {
            //     auto const& parent = all_transactions_[pi];
            //     parents_temp.insert(parent.parents().begin(), parent.parents().end());
            // }

            auto start = std::chrono::high_resolution_clock::now();
            auto parents_temp = parents;
            auto end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, relatives_management_part_2_vector_copy_time);

            start = std::chrono::high_resolution_clock::now();
            for (auto pi : parents) {
                auto const& parent = all_transactions_[pi];
                parents_temp.insert(std::end(parents_temp), std::begin(parent.parents()), std::end(parent.parents()));
            }
            end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, relatives_management_part_2_first_loop_time);


            start = std::chrono::high_resolution_clock::now();
            remove_duplicates(parents_temp);
            end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, relatives_management_part_2_remove_duplicates_time);

            start = std::chrono::high_resolution_clock::now();
            new_node.add_parents(std::begin(parents_temp), std::end(parents_temp));
            end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, relatives_management_part_2_new_node_add_parents_time);

            start = std::chrono::high_resolution_clock::now();
            // size_t temp_counter = 0;
            for (auto pi : new_node.parents()) {
                auto& parent = all_transactions_[pi];
                // parent.add_child(node_index, new_node.fee(), new_node.size(), new_node.sigops());

                // ++temp_counter;
                auto start_2 = std::chrono::high_resolution_clock::now();
                parent.add_child(node_index);
                auto end_2 = std::chrono::high_resolution_clock::now();
                increment_time(start_2, end_2, relatives_management_part_2_second_loop_parent_add_child_time);

            }
            end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, relatives_management_part_2_second_loop_time);

            // std::cout << "temp_counter: " << temp_counter << '\n';
        }
    }


    void relatives_management(domain::chain::transaction const& tx, index_t node_index, node& new_node) {

        auto start = std::chrono::high_resolution_clock::now();

        indexes_t parents;

        for (auto const& i : tx.inputs()) {
            if (i.previous_output().validation.from_mempool) {
                // Spend the UTXO
                internal_utxo_set_.erase(i.previous_output());

                auto it = hash_index_.find(i.previous_output().hash());
                index_t parent_index = it->second.first;
                parents.push_back(parent_index);
            }

            previous_outputs_.insert({i.previous_output(), node_index});
        }

        auto end = std::chrono::high_resolution_clock::now();
        increment_time(start, end, relatives_management_part_1_time);

        start = std::chrono::high_resolution_clock::now();
        relatives_management_part_2(parents, node_index, new_node);
        end = std::chrono::high_resolution_clock::now();
        increment_time(start, end, relatives_management_part_2_time);

    }

    error::error_code_t process_utxo_and_graph(domain::chain::transaction const& tx, index_t node_index, node& new_node) {
        //TODO(fernando): evitar tratar de borrar en el UTXO Local, si el UTXO fue encontrado en la DB


        auto start = std::chrono::high_resolution_clock::now();
        auto it = hash_index_.find(tx.hash());
        if (it != hash_index_.end()) {
            auto end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, hash_index_find_time);
            return error::duplicate_transaction;
        }
        auto end = std::chrono::high_resolution_clock::now();
        increment_time(start, end, hash_index_find_time);

        start = std::chrono::high_resolution_clock::now();
        auto res = check_double_spend(tx);
        if (res != error::success) {
            end = std::chrono::high_resolution_clock::now();
            increment_time(start, end, check_double_spend_time);
            return res;
        }
        end = std::chrono::high_resolution_clock::now();
        increment_time(start, end, check_double_spend_time);

        //--------------------------------------------------
        // Mutate the state

        start = std::chrono::high_resolution_clock::now();
        insert_outputs_in_utxo(tx);
        end = std::chrono::high_resolution_clock::now();
        increment_time(start, end, insert_outputs_in_utxo_time);


        start = std::chrono::high_resolution_clock::now();
        hash_index_.emplace(tx.hash(), std::make_pair(node_index, tx));
        end = std::chrono::high_resolution_clock::now();
        increment_time(start, end, hash_index_emplace_time);


        start = std::chrono::high_resolution_clock::now();
        relatives_management(tx, node_index, new_node);
        end = std::chrono::high_resolution_clock::now();
        increment_time(start, end, relatives_management_time);

        return error::success;
    }

    error::error_code_t check_double_spend(domain::chain::transaction const& tx) {
        for (auto const& i : tx.inputs()) {
            if (i.previous_output().validation.from_mempool) {
                auto it = internal_utxo_set_.find(i.previous_output());
                if (it == internal_utxo_set_.end()) {
                    return error::double_spend_mempool;
                }
            } else {
                auto it = previous_outputs_.find(i.previous_output());
                if (it != previous_outputs_.end()) {
                    return error::double_spend_blockchain;
                }
            }
        }
        return error::success;
    }

    void insert_outputs_in_utxo(domain::chain::transaction const& tx) {
        //precondition: there are no duplicates outputs between tx.outputs() and internal_utxo_set_
        uint32_t index = 0;
        for (auto const& o : tx.outputs()) {
            internal_utxo_set_.emplace(domain::chain::point{tx.hash(), index}, o);
            ++index;
        }
    }

    template <typename I>
    void reindex_decrement(I f, I l) {
        //precondition: f != l
        std::for_each(f, l, [this](candidate_index_t i) {
            auto& n = all_transactions_[i.index()];
            n.set_candidate_index(n.candidate_index() - 1);
        });
    }

    template <typename I>
    void reindex_increment(I f, I l) {
        //precondition: f != l
        std::for_each(f, l, [this](candidate_index_t i) {
            auto& n = all_transactions_[i.index()];
            n.set_candidate_index(n.candidate_index() + 1);
        });
    }

    void remove_and_reindex(index_t i) {
        //precondition: TODO?

        if (i == candidate_transactions_.size() - 1) {
            auto it = std::next(std::begin(candidate_transactions_), i);
            auto ci = candidate_transactions_.back().index();

            auto& node = all_transactions_[ci];
            node.set_candidate_index(null_index);
            node.reset_children_values();

            accum_size_ -= node.size();
            accum_sigops_ -= node.sigops();
            accum_fees_ -= node.fee();

            // std::cout << "++++++++++++++++++++++++++++++++++" << std::endl;
            // print_candidates();

#ifndef NDEBUG
            check_invariant_consistency_partial();
#endif

            candidate_transactions_.pop_back();

            // std::cout << "++++++++++++++++++++++++++++++++++" << std::endl;
            // print_candidates();

#ifndef NDEBUG
            check_invariant_consistency_partial();
#endif
            return;
        }

        auto it = std::next(std::begin(candidate_transactions_), i);
        auto ci = it->index();

        auto& node = all_transactions_[ci];
        // node.set_candidate_index(null_index);
        // node.reset_children_values();

        accum_size_ -= node.size();
        accum_sigops_ -= node.sigops();
        accum_fees_ -= node.fee();

        // std::cout << "++++++++++++++++++++++++++++++++++" << std::endl;
        // print_candidates();
#ifndef NDEBUG
        check_invariant_consistency_partial();
#endif

        // reindex_decrement(std::next(it), std::end(candidate_transactions_));
        // std::cout << "++++++++++++++++++++++++++++++++++" << std::endl;
        // print_candidates();
        // check_invariant_consistency_partial();

        candidate_transactions_.erase(it);

        // std::cout << "++++++++++++++++++++++++++++++++++" << std::endl;
        // print_candidates();

        all_transactions_[ci].set_candidate_index(null_index);
        all_transactions_[ci].reset_children_values();

        // std::cout << "++++++++++++++++++++++++++++++++++" << std::endl;
        // print_candidates();

#ifndef NDEBUG
        check_invariant_consistency_partial();
#endif
    }

    template <typename RO, typename RU, typename F>
    void remove_nodes_gen(removal_list_t const& to_remove, RO remove_ordered_fun, RU remove_unordered_fun, F f) {
        constexpr auto thresold = 1; //??
        if (to_remove.size() <= thresold) {
            remove_unordered_fun(to_remove);
        } else {
            auto ordered = to_ordered_set(f, to_remove);
            remove_ordered_fun(ordered);
        }
    }

    index_t get_candidate_index(index_t index) const {
        auto const& node = all_transactions_[index];
        return node.candidate_index();
    }

    void remove_nodes(removal_list_t const& to_remove) {
        auto const remove_ordered = [this](auto s){
            std::for_each(std::begin(s), std::end(s), [this](index_t i) {
                remove_and_reindex(i);
            });
        };

        auto const remove_unordered = [this](auto s){
            std::for_each(std::begin(s), std::end(s), [this](index_t i){
                remove_and_reindex(get_candidate_index(i));
            });
        };

        remove_nodes_gen(to_remove, remove_ordered, remove_unordered, [this](index_t i){
            return get_candidate_index(i);
        });
    }

    void reindex_parent_for_removal(mining::node const& node, mining::node& parent, index_t parent_index) {
        // cout << "reindex_parent_quitar\n";
        auto node_benefit = static_cast<double>(node.fee()) / node.size();
        auto accum_benefit = static_cast<double>(parent.children_fees()) / parent.children_size();

        // reduce_values(node, parent);
        parent.decrement_values(node.fee(), node.size(), node.sigops());

#ifndef NDEBUG
        auto accum_benefit_new = static_cast<double>(parent.children_fees()) / parent.children_size();
#endif

        if (node_benefit == accum_benefit) {
            return;
        }

        auto it = std::next(std::begin(candidate_transactions_), parent.candidate_index());
        // auto child_it = std::next(std::begin(candidate_transactions_), node.candidate_index());

        auto const cmp = [this](candidate_index_t a, candidate_index_t b) {
            return fee_per_size_cmp(a.index(), b.index());
        };

        if (node_benefit > accum_benefit) {
            BOOST_ASSERT(accum_benefit_new < accum_benefit);

            auto from = it + 1;
            auto to = std::end(candidate_transactions_);

            auto it2 = std::upper_bound(from, to, candidate_index_t{parent_index}, cmp);
            // reindex_decrement(from, it2);
#ifndef NDEBUG
            check_invariant_consistency_partial();
#endif
            it = std::rotate(it, it + 1, it2);
#ifndef NDEBUG
            check_invariant_consistency_partial();
#endif
            parent.set_candidate_index(std::distance(std::begin(candidate_transactions_), it));
#ifndef NDEBUG
            check_invariant_consistency_partial();
#endif

        } else {
            BOOST_ASSERT(accum_benefit_new > accum_benefit);

            auto from = std::begin(candidate_transactions_);
            auto to = it;

            auto it2 = std::upper_bound(from, to, candidate_index_t{parent_index}, cmp);
            if (it2 != it) {

                // std::cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%" << std::endl;
                // print_candidates();
                // std::cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%" << std::endl;
                // // check_invariant();
                // check_invariant_consistency_partial();

                // std::cout << "it2: " << it2->index() << std::endl;
                // std::cout << "it:  " << it->index() << std::endl;

                // reindex_increment(it2, it);

                // std::cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%" << std::endl;
                // print_candidates();
                // std::cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%" << std::endl;
                // check_invariant_consistency_partial();

#ifndef NDEBUG
                check_invariant_consistency_partial();
#endif
                std::rotate(it2, it, it + 1);
#ifndef NDEBUG
                check_invariant_consistency_partial();
#endif

                // std::cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%" << std::endl;
                // print_candidates();
                // std::cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%" << std::endl;
                // check_invariant();

                parent.set_candidate_index(std::distance(std::begin(candidate_transactions_), it2));
#ifndef NDEBUG
                check_invariant_consistency_partial();
#endif
                // std::cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%" << std::endl;
                // print_candidates();
                // std::cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%" << std::endl;
                // check_invariant();

            }
        }
    }

    void reindex_parents_for_removal(removal_list_t const& removed_elements) {
        for (auto i : removed_elements) {
            auto const& node = all_transactions_[i];
            for (auto pi : node.parents()) {
                auto& parent = all_transactions_[pi];
                if (parent.candidate_index() != null_index) {
                    reindex_parent_for_removal(node, parent, pi);
                }
            }
        }
    }

    //TODO(review-Dario): This method is very hard to follow
    void reindex_parent_from_insertion(mining::node const& node, mining::node& parent, index_t parent_index) {
        auto node_benefit = static_cast<double>(node.fee()) / node.size();                          //a
        auto accum_benefit = static_cast<double>(parent.children_fees()) / parent.children_size();  //b
        auto node_accum_benefit = static_cast<double>(node.children_fees()) / node.children_size(); //c
        auto old_accum_benefit = static_cast<double>(parent.children_fees() - node.fee()) / (parent.children_size() - node.size());  //d?

        // std::cout << "node_benefit:       " << node_benefit << "\n";
        // std::cout << "accum_benefit:      " << accum_benefit << "\n";
        // std::cout << "node_accum_benefit: " << node_accum_benefit << "\n";
        // std::cout << "old_accum_benefit:  " << old_accum_benefit << "\n";

        if (node_benefit == accum_benefit) {
            return;
        }

        if (old_accum_benefit == accum_benefit) {
            return;
        }

        auto it = std::next(std::begin(candidate_transactions_), parent.candidate_index());
        auto child_it = std::next(std::begin(candidate_transactions_), node.candidate_index());

        auto const cmp = [this](candidate_index_t a, candidate_index_t b) {
            return fee_per_size_cmp(a.index(), b.index());
        };


        if (old_accum_benefit > accum_benefit) {
            //  P               P'
            if (old_accum_benefit < node_accum_benefit) {
                // C    P               P'
                auto from = it + 1;
                auto to = std::end(candidate_transactions_);

                auto it2 = std::upper_bound(from, to, candidate_index_t{parent_index}, cmp);

                // reindex_decrement(it + 1, it2);
#ifndef NDEBUG
                check_invariant_consistency_partial();
#endif
                it = std::rotate(it, it + 1, it2);
#ifndef NDEBUG
                check_invariant_consistency_partial();
#endif
                parent.set_candidate_index(std::distance(std::begin(candidate_transactions_), it));
#ifndef NDEBUG
                check_invariant_consistency_partial();
#endif
            } else {
                if (accum_benefit < node_accum_benefit) {
                    //  P        C     P'
                    auto from = child_it + 1;
                    auto to = std::end(candidate_transactions_);;

                    auto it2 = std::upper_bound(from, to, candidate_index_t{parent_index}, cmp);

                    // reindex_decrement(it + 1, it2);
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif
                    it = std::rotate(it, it + 1, it2);
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif
                    parent.set_candidate_index(std::distance(std::begin(candidate_transactions_), it));
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif
                } else {
                //  P              P'      C
                    auto from = it + 1;
                    auto to = child_it;

                    auto it2 = std::upper_bound(from, to, candidate_index_t{parent_index}, cmp);
                    // reindex_decrement(it + 1, it2);
                    // check_invariant_consistency_partial();
                    it = std::rotate(it, it + 1, it2);
                    // check_invariant_consistency_partial();
                    parent.set_candidate_index(std::distance(std::begin(candidate_transactions_), it));
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif
                }
            }
        }  else {
                //  P'              P
            if (accum_benefit < node_accum_benefit) {
            // C    P'              P
                auto from = child_it + 1;
                auto to = it;

                auto it2 = std::upper_bound(from, to, candidate_index_t{parent_index}, cmp);

                if (it2 != it) {
                    // reindex_increment(it2, it);
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif
                    std::rotate(it2, it, it + 1);
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif
                    parent.set_candidate_index(std::distance(std::begin(candidate_transactions_), it2));
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif
                }
            } else {
                if (old_accum_benefit < node_accum_benefit) {
            //  P'        C       P
                    auto from = std::begin(candidate_transactions_);
                    auto to = child_it;

                    auto it2 = std::upper_bound(from, to, candidate_index_t{parent_index}, cmp);

                    // reindex_increment(it2, it);
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif
                    std::rotate(it2, it, it + 1);
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif
                    parent.set_candidate_index(std::distance(std::begin(candidate_transactions_), it2));
#ifndef NDEBUG
                    check_invariant_consistency_partial();
#endif

                } else {
                    //  P'        P         C
                    auto from = std::begin(candidate_transactions_);
                    auto to = it;
                    auto it2 = std::upper_bound(from, to, candidate_index_t{parent_index}, cmp);

                    if (it2 != it) {
                        // reindex_increment(it2, it);
#ifndef NDEBUG
                        check_invariant_consistency_partial();
#endif
                        std::rotate(it2, it, it + 1);
#ifndef NDEBUG
                        check_invariant_consistency_partial();
#endif
                        parent.set_candidate_index(std::distance(std::begin(candidate_transactions_), it2));
#ifndef NDEBUG
                        check_invariant_consistency_partial();
#endif
                    }
                }
            }
        }
    }


    // void print_candidates() {
    //     for (auto mi : candidate_transactions_) {
    //         std::cout << mi << ", ";
    //     }
    //     std::cout << std::endl;
    //     for (auto mi : candidate_transactions_) {
    //         auto& temp_node = all_transactions_[mi];
    //         auto benefit = static_cast<double>(temp_node.children_fees()) / temp_node.children_size();
    //         std::cout << benefit << ", ";
    //     }
    //     std::cout << std::endl;
    // }

    void reindex_parents_from_insertion(mining::node const& node, indexes_t const& to_insert) {
        //precondition: candidate_transactions_.size() > 0

        // for (auto pi : node.parents()) {
        //     auto& parent = all_transactions_[pi];
        //     auto old = parent;
        //     if (parent.candidate_index() != null_index) {
        //         reindex_parent_from_insertion(node, parent, pi);
        //     }
        // }


        // std::cout << "Node " << candidate_transactions_[node.candidate_index()] << " parents: ";
        // for (auto pi : node.parents()) {
        //     std::cout << pi << ", ";
        // }
        // std::cout << std::endl;

        for (auto pi : node.parents()) {
            auto& parent = all_transactions_[pi];
            // auto old = parent;

            // parent.increment_values(node.fee(), node.size(), node.sigops());

            if (parent.candidate_index() != null_index) {

                // auto parent_benefit = static_cast<double>(parent.children_fees()) / parent.children_size();
                // std::cout << "Parent stage0 benefit " << parent_benefit << "\n";
                parent.increment_values(node.fee(), node.size(), node.sigops());
                // parent_benefit = static_cast<double>(parent.children_fees()) / parent.children_size();
                // std::cout << "Parent stage1 benefit " << parent_benefit << "\n";

                reindex_parent_from_insertion(node, parent, pi);

                // std::cout << "--------------------------------------------------\n";
                // std::cout << "After re-sorting " << pi << "\n";
                // print_candidates();
                // std::cout << std::endl;
                // std::cout << "--------------------------------------------------\n";

            } else {
                auto it = std::find(std::begin(to_insert), std::end(to_insert), pi);
                if (it != std::end(to_insert)) {
                    parent.increment_values(node.fee(), node.size(), node.sigops());
                }
            }
        }
    }

    void insert_in_candidate(index_t node_index, indexes_t const& to_insert) {
        auto& node = all_transactions_[node_index];

        // std::cout << "--------------------------------------------------\n";
        // auto node_benefit = static_cast<double>(node.children_fees()) / node.children_size();
        // std::cout << "Before insert " << node_index << "\n";
        // std::cout << "New node benefit " << node_benefit << "\n";
        // print_candidates();
        // std::cout << std::endl;
        // std::cout << "--------------------------------------------------\n";




        // auto start = std::chrono::high_resolution_clock::now();

        auto const cmp = [this](candidate_index_t a, candidate_index_t b) {
            return fee_per_size_cmp(a.index(), b.index());
        };

        auto it = std::upper_bound(std::begin(candidate_transactions_), std::end(candidate_transactions_), candidate_index_t{node_index}, cmp);

        // auto end = std::chrono::high_resolution_clock::now();
        // auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        // binary_search_time += time_ns;


        if (it == std::end(candidate_transactions_)) {
            node.set_candidate_index(candidate_transactions_.size());
            candidate_transactions_.push_back(candidate_index_t{node_index});
#ifndef NDEBUG
            check_invariant_consistency_partial();
#endif
        } else {
#ifndef NDEBUG
            check_invariant_consistency_partial();
#endif
            // auto xxx = distance(std::begin(candidate_transactions_), it);
            auto xxx = candidate_transactions_.size();
            node.set_candidate_index(xxx);


            // std::cout << "++++++++++++++++++++++++++++++++++" << std::endl;
            // print_candidates();

            // check_invariant_consistency_partial();


            // start = std::chrono::high_resolution_clock::now();
            // reindex_increment(it, std::end(candidate_transactions_));
            // end = std::chrono::high_resolution_clock::now();
            // time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            // insert_reindex_time += time_ns;

            // std::cout << "++++++++++++++++++++++++++++++++++" << std::endl;
            // print_candidates();

            // start = std::chrono::high_resolution_clock::now();
            candidate_transactions_.insert(it, candidate_index_t{node_index});
            // end = std::chrono::high_resolution_clock::now();
            // time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            // insert_time += time_ns;

            // std::cout << "++++++++++++++++++++++++++++++++++" << std::endl;
            // print_candidates();

#ifndef NDEBUG
            check_invariant_consistency_partial();
#endif

        }



        // std::cout << "--------------------------------------------------\n";
        // std::cout << "After insert " << node_index << "\n";
        // print_candidates();
        // std::cout << std::endl;
        // std::cout << "--------------------------------------------------\n";

        reindex_parents_from_insertion(node, to_insert);
#ifndef NDEBUG
        check_invariant_consistency_partial();
#endif
    }


    size_t const max_template_size_;
    size_t const mempool_total_size_;
    size_t accum_size_ = 0;
    size_t accum_sigops_ = 0;
    uint64_t accum_fees_ = 0;

    //TODO(fernando): chequear el anidamiento de TX con su máximo (25??) y si es regla de consenso.

    internal_utxo_set_t internal_utxo_set_;
    all_transactions_t all_transactions_;
    hash_index_t hash_index_;
    candidate_indexes_t candidate_transactions_;
    bool sorted_ {false};

    previous_outputs_t previous_outputs_;
    // mutable mutex_t mutex_;
    prioritizer prioritizer_;
    std::atomic<bool> processing_block_{false};
};

}  // namespace mining
}  // namespace kth

#endif  //KTH_BLOCKCHAIN_MINING_MEMPOOL_V1_HPP_
