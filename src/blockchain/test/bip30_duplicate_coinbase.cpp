// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include <kth/blockchain/utxo_builder.hpp>
#include <kth/domain/constants.hpp>

using namespace kth;
using namespace kth::blockchain;

// =============================================================================
// BIP30 duplicate coinbases must not depend on the batch partition (#695)
// =============================================================================
//
// Blocks 91842 and 91880 each re-create a coinbase output an earlier block
// created and that nobody ever spent. BIP30 grandfathers exactly those two, and
// BCHN -- vendored in this repository at src/consensus/src/bch-rules/coins.cpp --
// settles what the set must hold afterwards: AddCoins passes possible_overwrite
// for every coinbase, and AddCoin then assigns `it->second.coin = std::move(coin)`.
// The NEW output wins, carrying the NEW height. Anywhere else, adding over a live
// entry is `Adding new coin that replaces non-pruned entry` -- an error, loudly.
//
// KTH does neither. `utxo_raw_delta::merge` accumulates a batch with
// `inserts.emplace(...)`, which is a no-op when the key is already there, so:
//
//   - original and duplicate in one batch -> the OLDER entry silently survives;
//   - original and duplicate in two batches -> the store is handed an insert over
//     a live key, rejects it with duplicated_key, and the node stops.
//
// Neither is the consensus result, they disagree with each other, and which one
// happens is decided by `utxo_batch_len` -- that is, by download progress.
//
// These are the first controls of the fix and they are RED on this tree. They
// state the properties rather than the current behaviour: the merge is where the
// batch's view of a key is decided, so it is where the authorization has to be
// carried and where an unauthorized collision has to be refused.

namespace {

// A distinct outpoint per test, with the payload SHAPE the store requires:
// reference mode stores an 8-byte {file_number, tx_offset} reference and rejects
// anything else, while full mode takes the payload verbatim. The content is what
// distinguishes "old entry" from "new entry" here, and it must differ so that
// which one survives is observable at all.
utxoz::raw_outpoint outpoint_filled(uint8_t byte, uint32_t index) {
    hash_digest txid{};
    txid.fill(byte);
    return utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, index);
}

utxo_raw_value entry(uint8_t payload_byte, uint32_t height) {
    return utxo_raw_value{std::vector<uint8_t>(8, payload_byte), height};
}

// One block's contribution: a single created output.
utxo_raw_delta block_creating(utxoz::raw_outpoint const& key, utxo_raw_value value) {
    utxo_raw_delta delta;
    delta.inserts.emplace(key, std::move(value));
    return delta;
}

} // namespace

// -----------------------------------------------------------------------------

TEST_CASE("bip30: an authorized duplicate in one batch leaves the new entry",
          "[bip30][partition]") {
    auto const key = outpoint_filled(0xE3, 0);

    // The original at 91722 and the duplicate at 91880, merged into one batch --
    // the partition that today collapses them without a word.
    utxo_raw_delta batch;
    REQUIRE(batch.merge(block_creating(key, entry(0xAA, 91722))) == delta_merge_result::ok);
    auto const second = batch.merge(block_creating(key, entry(0xBB, 91880)));

    // Authorized: this is one of the two blocks BIP30 grandfathers, so the merge
    // must accept it rather than treat it as a violation.
    CHECK(second == delta_merge_result::ok);

    // And the batch must carry the NEW entry. This is the assertion the current
    // emplace fails: it keeps 91722 and its payload, so a node that survives the
    // block still ends up with a set BCHN would not recognise.
    REQUIRE(batch.inserts.size() == 1u);
    auto const it = batch.inserts.find(key);
    REQUIRE(it != batch.inserts.end());
    CHECK(it->second.height == 91880u);
    REQUIRE(it->second.data.size() == 8u);
    CHECK(it->second.data[0] == 0xBB);
}

TEST_CASE("bip30: an unauthorized duplicate in one batch is refused, not collapsed",
          "[bip30][partition]") {
    auto const key = outpoint_filled(0x77, 0);

    // Two ordinary blocks, no BIP30 authorization anywhere near them. Today the
    // second insert is dropped by emplace and nothing is reported: the batch is
    // applied, the store accepts it, and a consensus violation leaves no trace.
    utxo_raw_delta batch;
    REQUIRE(batch.merge(block_creating(key, entry(0x11, 400000))) == delta_merge_result::ok);
    auto const second = batch.merge(block_creating(key, entry(0x22, 400001)));

    CHECK(second == delta_merge_result::unauthorized_duplicate);
}

TEST_CASE("bip30: the entry a replacement overwrites is kept for undo",
          "[bip30][undo]") {
    auto const key = outpoint_filled(0xD5, 0);

    // capture_block_undo walks block_delta.deletes, because that is what a block
    // spends. A BIP30 replacement is not a spend -- the original output is never
    // spent, which is exactly why these two blocks are the exceptions -- so today
    // nothing records what is being overwritten.
    //
    // Without that record, disconnecting the duplicate deletes the key it created
    // and has nothing to put back: the outpoint disappears instead of reverting to
    // the original. This is the same-batch shape, where the older entry lives only
    // in the batch delta and was never published to the store, so the batch is the
    // only place it can be taken from.
    utxo_raw_delta batch;
    REQUIRE(batch.merge(block_creating(key, entry(0x33, 91812))) == delta_merge_result::ok);
    REQUIRE(batch.merge(block_creating(key, entry(0x44, 91842))) == delta_merge_result::ok);

    auto const replaced = batch.replaced_entry(key);
    REQUIRE(replaced.has_value());
    CHECK(replaced->height == 91812u);
    REQUIRE(replaced->data.size() == 8u);
    CHECK(replaced->data[0] == 0x33);
}
