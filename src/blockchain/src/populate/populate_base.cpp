// Copyright (c) 2016-present Knuth Project developers.
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
// Must be called serially: it writes the tx's entry in the validator-owned
// transaction_validation_store (non-concurrent).
void populate_base::populate_duplicate(size_t branch_height, domain::chain::transaction const& tx, bool require_confirmed) const {
    //Knuth: We are not validating tx duplication
    chain_.transaction_validations().mutate(tx.hash(), [](auto& tv){ tv.duplicate = false; });
}

// Must be called serially: it writes the tx's entry in the validator-owned
// transaction_validation_store (non-concurrent).
void populate_base::populate_pooled(domain::chain::transaction const& tx, uint32_t height) const {
    // The "current" flag marked a tx already confirmed at this height, resolved
    // from the LMDB confirmed-transaction index. That store was removed (blocks
    // live in flat files, the UTXO set in UTXO-Z), so there is no hash->position
    // lookup: a tx reaching validation is treated as not-yet-confirmed. A v1
    // confirmed lookup over the flat-file block store is tracked by issue #491.
    (void)height;
    chain_.transaction_validations().mutate(tx.hash(), [](auto& tv){ tv.current = false; });
}

// Unspent outputs are cached by the store. If the cache is large enough this
// may never hit the file system. However on high RAM systems the file system
// is faster than the cache due to reduced paging of the memory-mapped file.
code populate_base::populate_prevout(size_t branch_height, output_point const& outpoint, bool require_confirmed, uint32_t median_time_past) const {
    // The previous output will be cached on the input's outpoint.
    auto& prevout = outpoint.validation;

    prevout.spent = false;
    prevout.confirmed = false;
    prevout.cache = domain::chain::output{};
    prevout.from_mempool = false;

    // If the input is a coinbase there is no prevout to populate.
    if (outpoint.is_null()) {
        return error::success;
    }

    // branch_height is the validation height; get_utxo bounds the prevout to one
    // created at/below it (used by the spend check below too). require_confirmed
    // = confirmed-only (blocks) vs also-mempool (tx, handled in the miss branch).
    auto utxo = chain_.get_utxo(outpoint, branch_height);

    if ( ! utxo && utxo.error() == database::result_code::not_resolved) {
        // The ACTIVE versions cannot answer. That is not absence, and treating
        // it as one is what made admission impossible (#584): a prevout living
        // in an older version file read as missing, so no transaction whose
        // parent had aged out could ever be admitted.
        //
        // Resolved here, as a batch of one this call owns. One request is the
        // honest size — a single prevout is what this function was asked about
        // — and the store keeps nothing, so this cannot consume or be consumed
        // by a concurrent block validation.
        // Checked, not cast. The request record carries a uint32_t and
        // branch_height is a size_t; truncating it would bound the resolution at
        // a different block and answer confidently about the wrong one.
        auto const store_height = database::to_store_height(branch_height);
        if ( ! store_height) {
            spdlog::error("[populate] branch height {} does not fit the UTXO store's request "
                "height; refusing to resolve {}:{} against a truncated height",
                branch_height, encode_hash(outpoint.hash()), outpoint.index());
            return error::operation_failed;
        }

        auto const ph = outpoint.hash();
        std::array<utxoz::lookup_request, 1> const own{
            utxoz::lookup_request{
                utxoz::make_outpoint(std::span<uint8_t const, 32>{ph.data(), 32},
                                     outpoint.index()),
                *store_height}};

        auto resolved = chain_.utxo_resolve(own);
        if ( ! resolved) {
            // version_unreadable / catalog_unreadable. A local storage fault,
            // never an answer about this prevout.
            spdlog::error("[populate] resolving {}:{} failed; refusing to treat an unreadable "
                "store as a missing prevout",
                encode_hash(outpoint.hash()), outpoint.index());
            return error::operation_failed;
        }

        if (auto const it = resolved->found.find(own[0].key); it != resolved->found.end()) {
            utxo = block_chain::output_info{
                it->second.output(), it->second.height(),
                it->second.median_time_past(), it->second.coinbase()};
        }
        // Otherwise it is PROVEN absent, and falls through to the miss branch
        // below — which is now the only way to reach it.
    }

    if ( ! utxo) {
        // Every remaining code is this node failing to read its own storage, and
        // probing the mempool on that basis would answer a question nobody could
        // answer: a miss would then look exactly like a prevout that does not
        // exist, and the block would be rejected over a disk that did not
        // respond.
        if (utxo.error() != database::result_code::not_resolved) {
            spdlog::error("[populate] the UTXO store failed while reading {}:{}; "
                "refusing to treat an unreadable store as a missing prevout",
                encode_hash(outpoint.hash()), outpoint.index());
            return error::operation_failed;
        }
        // Chained tx: the prevout may be an unconfirmed parent's output in the
        // mempool (tx-validation path only). Model the mempool coin as "confirms
        // next block" (BCHN MEMPOOL_HEIGHT): height = branch_height + 1 and the
        // tip's MTP make is_locked's relative age == 0. spent stays false —
        // first-seen is enforced at admission (spent_by_).
        if ( ! require_confirmed) {
            if (auto out = chain_.mempool_ref().resolve(outpoint)) {
                prevout.cache = std::move(*out);
                prevout.height = branch_height + 1u;
                prevout.median_time_past = median_time_past;
                prevout.coinbase = false;
                prevout.from_mempool = true;
            }
        }
        return error::success;
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
    if ((spend_height <= branch_height) && (spend_height != output::validation_t::not_spent)) {
        prevout.spent = true;
        prevout.confirmed = true;
        prevout.cache = domain::chain::output{};
    }

    return error::success;
}

} // namespace kth::blockchain
