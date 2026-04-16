// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/mempool_transaction.h>

#include <tuple>

#include <kth/blockchain/interface/safe_chain.hpp>

#include <kth/capi/helpers.hpp>



// ---------------------------------------------------------------------------
extern "C" {

char const* kth_chain_mempool_transaction_address(kth_mempool_transaction_t tx) {
    auto tx_address_str = kth::cpp_ref<kth::blockchain::mempool_transaction_summary>(tx).address();
    return kth::create_c_str(tx_address_str);
}

char const* kth_chain_mempool_transaction_hash(kth_mempool_transaction_t tx) {
    auto tx_hash_str = kth::cpp_ref<kth::blockchain::mempool_transaction_summary>(tx).hash();
    return kth::create_c_str(tx_hash_str);
}

uint64_t kth_chain_mempool_transaction_index(kth_mempool_transaction_t tx) {
    return kth::cpp_ref<kth::blockchain::mempool_transaction_summary>(tx).index();
}

char const* kth_chain_mempool_transaction_satoshis(kth_mempool_transaction_t tx) {
    auto tx_satoshis_str = kth::cpp_ref<kth::blockchain::mempool_transaction_summary>(tx).satoshis();
    return kth::create_c_str(tx_satoshis_str);
}

uint64_t kth_chain_kth_mempool_transaction_timestamp(kth_mempool_transaction_t tx) {
    return kth::cpp_ref<kth::blockchain::mempool_transaction_summary>(tx).timestamp();
}

char const* kth_chain_mempool_transaction_prev_output_id(kth_mempool_transaction_t tx) {
    auto tx_prev_output_id_str = kth::cpp_ref<kth::blockchain::mempool_transaction_summary>(tx).previous_output_hash();
    return kth::create_c_str(tx_prev_output_id_str);
}

char const* kth_chain_mempool_transaction_prev_output_index(kth_mempool_transaction_t tx) {
    auto tx_prev_output_index_str = kth::cpp_ref<kth::blockchain::mempool_transaction_summary>(tx).previous_output_index();
    return kth::create_c_str(tx_prev_output_index_str);
}

} // extern "C"



