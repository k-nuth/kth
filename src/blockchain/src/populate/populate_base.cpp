// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/populate/populate_base.hpp>

#include <algorithm>
#include <cstddef>

#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace kth::database;

#define NAME "populate_base"


// Database access is limited to:
// spend: { spender }
// transaction: { exists, height, position, output }

populate_base::populate_base(dispatcher& dispatch, fast_chain const& chain)
    : dispatch_(dispatch)
    , fast_chain_(chain)
{}

// This is the only necessary file system read in block/tx validation.
void populate_base::populate_duplicate(size_t branch_height, const domain::chain::transaction& tx, bool require_confirmed) const {
    //Knuth: We are not validating tx duplication
    tx.validation.duplicate = false;
}

void populate_base::populate_pooled(const domain::chain::transaction& tx, uint32_t height) const {
    size_t stored_height;
    size_t position;

    if (fast_chain_.get_transaction_position(stored_height, position, tx.hash(), false)

        && (position == position_max)) {

        tx.validation.pooled = true;
        tx.validation.current = (stored_height == height);
        return;
    }

    tx.validation.pooled = false;
    tx.validation.current = false;
}

// Unspent outputs are cached by the store. If the cache is large enough this
// may never hit the file system. However on high RAM systems the file system
// is faster than the cache due to reduced paging of the memory-mapped file.
void populate_base::populate_prevout(size_t branch_height, output_point const& outpoint, bool require_confirmed) const {
    // The previous output will be cached on the input's outpoint.
    auto& prevout = outpoint.validation;

    prevout.spent = false;
    prevout.confirmed = false;
    prevout.cache = domain::chain::output{};
    prevout.from_mempool = false;

    // If the input is a coinbase there is no prevout to populate.
    if (outpoint.is_null()) {
        return;
    }

    //TODO(fernando): check the value of the parameters: branch_height and require_confirmed
    if ( ! fast_chain_.get_utxo(prevout.cache, prevout.height, prevout.median_time_past, prevout.coinbase, outpoint, branch_height)) {
        return;
    }

    // BUGBUG: Spends are not marked as spent by unconfirmed transactions.
    // So tx pool transactions currently have no double spend limitation.
    // The output is spent only if by a spend at or below the branch height.
    auto const spend_height = prevout.cache.validation.spender_height;

    // The previous output has already been spent (double spend).
    if ((spend_height <= branch_height) && (spend_height != output::validation::not_spent)) {
        prevout.spent = true;
        prevout.confirmed = true;
        prevout.cache = domain::chain::output{};
    }
}

} // namespace kth::blockchain
