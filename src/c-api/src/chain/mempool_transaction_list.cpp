// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/mempool_transaction_list.h>

#include <kth/blockchain/interface/safe_chain.hpp>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

extern "C" {

kth_mempool_transaction_list_t kth_chain_mempool_transaction_list_construct_default(void) {
    return kth::leak_list<kth::blockchain::mempool_transaction_summary>();
}

void kth_chain_mempool_transaction_list_push_back(kth_mempool_transaction_list_t l, kth_mempool_transaction_t e) {
    KTH_PRECONDITION(l != nullptr);
    KTH_PRECONDITION(e != nullptr);
    kth::cpp_ref<std::vector<kth::blockchain::mempool_transaction_summary>>(l).push_back(
        kth::cpp_ref<kth::blockchain::mempool_transaction_summary>(e));
}

void kth_chain_mempool_transaction_list_destruct(kth_mempool_transaction_list_t l) {
    if (l == nullptr) return;
    delete &kth::cpp_ref<std::vector<kth::blockchain::mempool_transaction_summary>>(l);
}

kth_size_t kth_chain_mempool_transaction_list_count(kth_mempool_transaction_list_t l) {
    KTH_PRECONDITION(l != nullptr);
    return kth::cpp_ref<std::vector<kth::blockchain::mempool_transaction_summary>>(l).size();
}

kth_mempool_transaction_t kth_chain_mempool_transaction_list_nth(kth_mempool_transaction_list_t l, kth_size_t n) {
    KTH_PRECONDITION(l != nullptr);
    auto& vec = kth::cpp_ref<std::vector<kth::blockchain::mempool_transaction_summary>>(l);
    KTH_PRECONDITION(n < vec.size());
    return &vec[n];
}

} // extern "C"
