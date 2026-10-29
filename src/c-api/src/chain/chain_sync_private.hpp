// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_SRC_CHAIN_CHAIN_SYNC_PRIVATE_HPP_
#define KTH_CAPI_SRC_CHAIN_CHAIN_SYNC_PRIVATE_HPP_

// Private to the C-API sources, not installed. Helpers shared by
// `chain_sync.cpp` (hand-written) and `chain_sync_generated.cpp` (generated).

#include <kth/capi/chain/chain_sync.h>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/capi/detail/sync_wait.hpp>

namespace {

inline
kth::blockchain::block_chain& safe_chain(kth_chain_t chain) {
    return *static_cast<kth::blockchain::block_chain*>(chain);
}

inline
kth::domain::message::transaction::const_ptr tx_shared(kth_transaction_const_t tx) {
    auto const& tx_ref = *static_cast<kth::domain::chain::transaction const*>(tx);
    auto* tx_new = new kth::domain::message::transaction(tx_ref);
    return kth::domain::message::transaction::const_ptr(tx_new);
}

inline
kth::domain::message::block::const_ptr block_shared(kth_block_const_t block) {
    auto const& block_ref = *static_cast<kth::domain::chain::block const*>(block);
    auto* block_new = new kth::domain::message::block(block_ref);
    return kth::domain::message::block::const_ptr(block_new);
}

// Run an awaitable on the chain's executor and block until it completes.
using kth::capi::sync_wait;

} /* end of anonymous namespace */

#endif // KTH_CAPI_SRC_CHAIN_CHAIN_SYNC_PRIVATE_HPP_
