// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/block_indexes.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_block_indexes_mut_t kth_chain_block_indexes_construct_default(void) {
    return new std::vector<size_t>();
}

void kth_chain_block_indexes_push_back(kth_block_indexes_mut_t list, kth_size_t elem) {
    KTH_PRECONDITION(list != nullptr);
    static_cast<std::vector<size_t>*>(list)->push_back(static_cast<size_t>(elem));
}

void kth_chain_block_indexes_destruct(kth_block_indexes_mut_t list) {
    if (list == nullptr) return;
    delete static_cast<std::vector<size_t>*>(list);
}

kth_size_t kth_chain_block_indexes_count(kth_block_indexes_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return static_cast<std::vector<size_t> const*>(list)->size();
}

kth_size_t kth_chain_block_indexes_nth(kth_block_indexes_const_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto const& vec = *static_cast<std::vector<size_t> const*>(list);
    KTH_PRECONDITION(index < vec.size());
    return static_cast<kth_size_t>(vec[index]);
}

void kth_chain_block_indexes_assign_at(kth_block_indexes_mut_t list, kth_size_t index, kth_size_t elem) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<size_t>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec[index] = static_cast<size_t>(elem);
}

void kth_chain_block_indexes_erase(kth_block_indexes_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = *static_cast<std::vector<size_t>*>(list);
    KTH_PRECONDITION(index < vec.size());
    vec.erase(vec.begin() + index);
}

} // extern "C"
