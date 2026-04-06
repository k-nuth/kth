// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/block_indexes.h>

#include <vector>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// Global converters (used by other generated code)
std::vector<kth_size_t> const& kth_chain_block_indexes_const_cpp(kth_block_indexes_const_t l) {
    return *static_cast<std::vector<kth_size_t> const*>(l);
}

std::vector<kth_size_t>& kth_chain_block_indexes_cpp(kth_block_indexes_mut_t l) {
    return *static_cast<std::vector<kth_size_t>*>(l);
}

// Construct from C++ (returns opaque pointer to existing vector)
kth_block_indexes_mut_t kth_chain_block_indexes_construct_from_cpp(std::vector<kth_size_t>& l) {
    return &l;
}

void const* kth_chain_block_indexes_construct_from_cpp(std::vector<kth_size_t> const& l) {
    return &l;
}

// ---------------------------------------------------------------------------
extern "C" {

kth_block_indexes_mut_t kth_chain_block_indexes_construct_default() {
    return new std::vector<kth_size_t>();
}

void kth_chain_block_indexes_push_back(kth_block_indexes_mut_t list, kth_size_t elem) {
    kth_chain_block_indexes_cpp(list).push_back(elem);
}

void kth_chain_block_indexes_destruct(kth_block_indexes_mut_t list) {
    if (list == nullptr) return;
    delete &kth_chain_block_indexes_cpp(list);
}

kth_size_t kth_chain_block_indexes_count(kth_block_indexes_const_t list) {
    return kth_chain_block_indexes_const_cpp(list).size();
}

kth_size_t kth_chain_block_indexes_nth(kth_block_indexes_const_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_chain_block_indexes_const_cpp(list).size());
    return kth_chain_block_indexes_const_cpp(list)[index];
}

void kth_chain_block_indexes_assign_at(kth_block_indexes_mut_t list, kth_size_t index, kth_size_t elem) {
    KTH_PRECONDITION(index < kth_chain_block_indexes_cpp(list).size());
    kth_chain_block_indexes_cpp(list)[index] = elem;
}

void kth_chain_block_indexes_erase(kth_block_indexes_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_chain_block_indexes_cpp(list).size());
    auto& v = kth_chain_block_indexes_cpp(list);
    v.erase(std::next(v.begin(), index));
}

} // extern "C"
