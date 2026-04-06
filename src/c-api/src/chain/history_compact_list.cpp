// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/history_compact_list.h>

#include <vector>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// Global converters (used by other generated code)
std::vector<kth::domain::chain::history_compact> const& kth_chain_history_compact_list_const_cpp(kth_history_compact_list_const_t l) {
    return *static_cast<std::vector<kth::domain::chain::history_compact> const*>(l);
}

std::vector<kth::domain::chain::history_compact>& kth_chain_history_compact_list_cpp(kth_history_compact_list_mut_t l) {
    return *static_cast<std::vector<kth::domain::chain::history_compact>*>(l);
}

// Construct from C++ (returns opaque pointer to existing vector)
kth_history_compact_list_mut_t kth_chain_history_compact_list_construct_from_cpp(std::vector<kth::domain::chain::history_compact>& l) {
    return &l;
}

void const* kth_chain_history_compact_list_construct_from_cpp(std::vector<kth::domain::chain::history_compact> const& l) {
    return &l;
}

// ---------------------------------------------------------------------------
extern "C" {

kth_history_compact_list_mut_t kth_chain_history_compact_list_construct_default() {
    return new std::vector<kth::domain::chain::history_compact>();
}

void kth_chain_history_compact_list_push_back(kth_history_compact_list_mut_t list, kth_history_compact_const_t elem) {
    kth_chain_history_compact_list_cpp(list).push_back(kth_chain_history_compact_const_cpp(elem));
}

void kth_chain_history_compact_list_destruct(kth_history_compact_list_mut_t list) {
    if (list == nullptr) return;
    delete &kth_chain_history_compact_list_cpp(list);
}

kth_size_t kth_chain_history_compact_list_count(kth_history_compact_list_const_t list) {
    return kth_chain_history_compact_list_const_cpp(list).size();
}

kth_history_compact_const_t kth_chain_history_compact_list_nth(kth_history_compact_list_const_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_chain_history_compact_list_const_cpp(list).size());
    return &kth_chain_history_compact_list_const_cpp(list)[index];
}

void kth_chain_history_compact_list_assign_at(kth_history_compact_list_mut_t list, kth_size_t index, kth_history_compact_const_t elem) {
    KTH_PRECONDITION(index < kth_chain_history_compact_list_cpp(list).size());
    kth_chain_history_compact_list_cpp(list)[index] = kth_chain_history_compact_const_cpp(elem);
}

void kth_chain_history_compact_list_erase(kth_history_compact_list_mut_t list, kth_size_t index) {
    KTH_PRECONDITION(index < kth_chain_history_compact_list_cpp(list).size());
    auto& v = kth_chain_history_compact_list_cpp(list);
    v.erase(std::next(v.begin(), index));
}

} // extern "C"
