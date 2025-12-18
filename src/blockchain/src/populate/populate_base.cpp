// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/populate/populate_base.hpp>

#include <algorithm>
#include <cstddef>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace kth::database;

// Database access is limited to:
// spend: { spender }
// transaction: { exists, height, position, output }

populate_base::populate_base(executor_type executor, size_t threads, block_chain const& chain)
    : executor_(std::move(executor))
    , threads_(threads)
    , chain_(chain)
{}

// This is the only necessary file system read in block/tx validation.
void populate_base::populate_duplicate(size_t branch_height, domain::chain::transaction const& tx, bool require_confirmed) const {
    //Knuth: We are not validating tx duplication
    tx.validation.duplicate = false;
}

void populate_base::populate_pooled(domain::chain::transaction const& tx, uint32_t forks) const {
    tx.validation.pooled = false;
    tx.validation.current = false;
    auto const result = chain_.get_transaction_position(tx.hash(), false);
    if ( ! result) {
        return;
    }
    if (result->second != position_max) {
        return;
    }
    tx.validation.pooled = true;
    tx.validation.current = (result->first == forks);
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
    auto const utxo = chain_.get_utxo(outpoint, branch_height);
    if ( ! utxo) {
        return;
    }
    prevout.cache = utxo->output;
    prevout.height = utxo->height;
    prevout.median_time_past = utxo->median_time_past;
    prevout.coinbase = utxo->coinbase;

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
