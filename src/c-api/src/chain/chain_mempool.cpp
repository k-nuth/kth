// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_mempool.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/blockchain/interface/block_chain.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::blockchain::block_chain;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Getters

kth_hash_list_mut_t kth_chain_get_mempool_txids(kth_chain_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_list<kth::hash_digest>(kth::cpp_ref<cpp_t>(self).get_mempool_txids());
}

kth_mempool_totals_t kth_chain_get_mempool_info(kth_chain_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_c_struct<kth_mempool_totals_t>(kth::cpp_ref<cpp_t>(self).get_mempool_info());
}


// Operations

kth_bool_t kth_chain_get_mempool_entry(kth_chain_t self, kth_hash_t const* txid, kth_mempool_entry_info_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid->hash);
    auto const result = kth::cpp_ref<cpp_t>(self).get_mempool_entry(txid_cpp);
    if ( ! result) return kth::bool_to_int(false);
    *out = kth::to_c_struct<kth_mempool_entry_info_t>(*result);
    return kth::bool_to_int(true);
}

kth_bool_t kth_chain_get_mempool_entry_unsafe(kth_chain_t self, uint8_t const* txid, kth_mempool_entry_info_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid);
    auto const result = kth::cpp_ref<cpp_t>(self).get_mempool_entry(txid_cpp);
    if ( ! result) return kth::bool_to_int(false);
    *out = kth::to_c_struct<kth_mempool_entry_info_t>(*result);
    return kth::bool_to_int(true);
}

kth_hash_list_mut_t kth_chain_get_mempool_depends(kth_chain_t self, kth_hash_t const* txid) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid->hash);
    return kth::leak_list<kth::hash_digest>(kth::cpp_ref<cpp_t>(self).get_mempool_depends(txid_cpp));
}

kth_hash_list_mut_t kth_chain_get_mempool_depends_unsafe(kth_chain_t self, uint8_t const* txid) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid);
    return kth::leak_list<kth::hash_digest>(kth::cpp_ref<cpp_t>(self).get_mempool_depends(txid_cpp));
}

kth_hash_list_mut_t kth_chain_get_mempool_spentby(kth_chain_t self, kth_hash_t const* txid) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid->hash);
    return kth::leak_list<kth::hash_digest>(kth::cpp_ref<cpp_t>(self).get_mempool_spentby(txid_cpp));
}

kth_hash_list_mut_t kth_chain_get_mempool_spentby_unsafe(kth_chain_t self, uint8_t const* txid) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid);
    return kth::leak_list<kth::hash_digest>(kth::cpp_ref<cpp_t>(self).get_mempool_spentby(txid_cpp));
}

kth_hash_list_mut_t kth_chain_get_mempool_ancestors(kth_chain_t self, kth_hash_t const* txid) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid->hash);
    return kth::leak_list<kth::hash_digest>(kth::cpp_ref<cpp_t>(self).get_mempool_ancestors(txid_cpp));
}

kth_hash_list_mut_t kth_chain_get_mempool_ancestors_unsafe(kth_chain_t self, uint8_t const* txid) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid);
    return kth::leak_list<kth::hash_digest>(kth::cpp_ref<cpp_t>(self).get_mempool_ancestors(txid_cpp));
}

kth_hash_list_mut_t kth_chain_get_mempool_descendants(kth_chain_t self, kth_hash_t const* txid) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid->hash);
    return kth::leak_list<kth::hash_digest>(kth::cpp_ref<cpp_t>(self).get_mempool_descendants(txid_cpp));
}

kth_hash_list_mut_t kth_chain_get_mempool_descendants_unsafe(kth_chain_t self, uint8_t const* txid) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(txid != nullptr);
    auto const txid_cpp = kth::hash_to_cpp(txid);
    return kth::leak_list<kth::hash_digest>(kth::cpp_ref<cpp_t>(self).get_mempool_descendants(txid_cpp));
}

} // extern "C"
