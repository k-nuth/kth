// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_TEMPLATE_PREVOUTS_HPP
#define KTH_BLOCKCHAIN_POOLS_TEMPLATE_PREVOUTS_HPP

#include <vector>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/mempool.hpp>

namespace kth::blockchain {

// Refresh only the private captured view used for a template. Each retained
// transaction is deep-copied; admission metadata on pooled objects is never
// changed. Confirmed inputs are resolved at the captured connected tip. Inputs
// from parents in this same view have candidate height/MTP and zero relative
// age. A proved-missing input removes its transaction and all descendants from
// the returned view; an unreadable store refuses the whole operation.
//
// The caller supplies one coherent published context and pool capture. After
// awaiting this operation it must compare that publication's generation with
// the current publication ONCE and refuse the template if they differ. No retry
// loop is hidden here: the next tip/pool event or request drives a fresh capture.
//
// Synchronous UTXO-library reads run on block_chain::executor(), away from the
// network executor. co_spawn is joined with use_awaitable: no detached task or
// cross-task callback borrows the view or a temporary. The chain itself must
// outlive its request coroutine, as with its other awaited query operations.
KB_API
awaitable_expected<std::vector<mempool_entry>> refresh_template_prevouts(
    block_chain const& chain, std::vector<mempool_entry> entries,
    block_chain::chain_view_ptr published);

} // namespace kth::blockchain
#endif // KTH_BLOCKCHAIN_POOLS_TEMPLATE_PREVOUTS_HPP
