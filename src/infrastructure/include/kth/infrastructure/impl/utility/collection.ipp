// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_COLLECTION_IPP
#define KTH_INFRASTRUCTURE_COLLECTION_IPP

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <iterator>
#include <type_traits>
#include <vector>

namespace kth {

template <typename Source, typename Target>
std::vector<Target> cast(std::vector<Source> const& source) {
    std::vector<Target> target(source.size());
    target.assign(source.begin(), source.end());
    return target;
}

template <typename T>
std::vector<T>& distinct(std::vector<T>& list) {
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    list.shrink_to_fit();
    return list;
}

template <typename Pair, typename Key>
int find_pair_position(std::vector<Pair> const& list, const Key& key) {
    auto const predicate = [&](const Pair& pair) {
        return pair.first == key;
    };

    auto it = std::find_if(list.begin(), list.end(), predicate);

    if (it == list.end()) {
        return -1;
    }

    return static_cast<int>(distance(list.begin(), it));
}

template <typename T, typename Container>
int find_position(const Container& list, T const& value) {
    auto const it = std::find(std::begin(list), std::end(list), value);

    if (it == std::end(list)) {
        return -1;
    }

    return static_cast<int>(std::distance(list.begin(), it));
}

template <typename T, typename Predicate>
typename std::vector<T>::iterator insert_sorted(std::vector<T>& list, T& element, Predicate predicate) {
    return list.insert(std::upper_bound(list.begin(), list.end(), element,
        predicate), element);
}

template <typename T>
void move_append(std::vector<T>& target, std::vector<T>& source) {
    target.reserve(target.size() + source.size());
    std::move(source.begin(), source.end(), std::back_inserter(target));
    source.clear();
}

template <typename T>
T pop(std::vector<T>& stack) {
    KTH_ASSERT( ! stack.empty());
    auto const element = stack.back();
    stack.pop_back();
    ////stack.shrink_to_fit();
    return element;
}

////template <typename Collection>
////Collection reverse(const Collection& list)
////{
////    Collection out(list.size());
////    std::reverse_copy(list.begin(), list.end(), out.begin());
////    return out;
////}

} // namespace kth

namespace std {

template <typename T>
std::ostream& operator<<(std::ostream& output, std::vector<T> const& list) {
    size_t current = 0;
    auto const end = list.size();

    for (auto const& element : list) {
        output << element;

        if (++current < end) {
            // output << std::endl;
            output << '\n';
        }
    }

    return output;
}

} // namespace std

#endif
