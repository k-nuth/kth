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

// The two grandfathered blocks, by the identity consensus uses: the exact pair.
// Taken from the constants the rule itself reads, never retyped here.
constexpr uint32_t height_91842 = 91842;
constexpr uint32_t height_91880 = 91880;

hash_digest hash_91842() {
    return kth::mainnet_bip30_exception_checkpoint1.hash();
}

hash_digest hash_91880() {
    return kth::mainnet_bip30_exception_checkpoint2.hash();
}

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

// One block's contribution, built the way the builder has to build it: the
// authorization is derived from the block's OWN {hash, height} through the
// consensus rule, and attached to the operation. Nothing downstream re-decides
// it, and a block that is not one of the two carries no authorization at all --
// which is what makes the negative cases below negative.
utxo_raw_delta block_creating(
    utxoz::raw_outpoint const& key,
    utxo_raw_value value,
    hash_digest const& block_hash,
    uint32_t block_height)
{
    utxo_raw_delta delta;
    delta.inserts.emplace(key, std::move(value));

    if (domain::chain::is_bip30_exception({block_hash, block_height},
                                          domain::config::network::mainnet)) {
        delta.authorized_replacements.insert(key);
    }
    return delta;
}

// A block with no claim to the exception: an ordinary hash at an ordinary height.
utxo_raw_delta ordinary_block_creating(
    utxoz::raw_outpoint const& key, utxo_raw_value value, uint32_t block_height)
{
    hash_digest ordinary{};
    ordinary.fill(0x5A);
    return block_creating(key, std::move(value), ordinary, block_height);
}

} // namespace

// -----------------------------------------------------------------------------

TEST_CASE("bip30: an authorized duplicate in one batch leaves the new entry",
          "[bip30][partition]") {
    auto const key = outpoint_filled(0xE3, 0);

    // The original at 91722 and the duplicate at 91880, merged into one batch --
    // the partition that today collapses them without a word. Only the second
    // block carries an authorization, and it carries it because the rule says so
    // about that block's own hash and height.
    utxo_raw_delta batch;
    REQUIRE(batch.merge(ordinary_block_creating(key, entry(0xAA, 91722), 91722))
            == delta_merge_result::ok);
    auto const second = batch.merge(
        block_creating(key, entry(0xBB, height_91880), hash_91880(), height_91880));

    CHECK(second == delta_merge_result::ok);

    // The batch must carry the NEW entry. This is the assertion the old emplace
    // failed: it kept 91722 and its payload, so a node that survived the block
    // still ended up with a set BCHN would not recognise.
    REQUIRE(batch.inserts.size() == 1u);
    auto const it = batch.inserts.find(key);
    REQUIRE(it != batch.inserts.end());
    CHECK(it->second.height == height_91880);
    REQUIRE(it->second.data.size() == 8u);
    CHECK(it->second.data[0] == 0xBB);
}

TEST_CASE("bip30: an unauthorized duplicate in one batch is refused, not collapsed",
          "[bip30][partition]") {
    auto const key = outpoint_filled(0x77, 0);

    // Two ordinary blocks, no authorization anywhere near them. The old emplace
    // dropped the second insert and reported nothing: the batch was applied, the
    // store accepted it, and a consensus violation left no trace.
    utxo_raw_delta batch;
    REQUIRE(batch.merge(ordinary_block_creating(key, entry(0x11, 400000), 400000))
            == delta_merge_result::ok);
    auto const second = batch.merge(
        ordinary_block_creating(key, entry(0x22, 400001), 400001));

    CHECK(second == delta_merge_result::unauthorized_duplicate);

    // And the refusal left the batch alone. A caller that stops on this result
    // must not be holding a half-merged batch, so the entry that was already
    // there is untouched and nothing of the refused block was folded in.
    REQUIRE(batch.inserts.size() == 1u);
    auto const it = batch.inserts.find(key);
    REQUIRE(it != batch.inserts.end());
    CHECK(it->second.height == 400000u);
    CHECK(it->second.data[0] == 0x11);
}

TEST_CASE("bip30: the right height with the wrong hash is not an exception",
          "[bip30][authorization]") {
    auto const key = outpoint_filled(0x88, 0);

    // 91880 is one of the two heights, and that is worth nothing on its own. The
    // rule matches a pair; a block that merely sits at the right height has no
    // claim to overwrite anything.
    hash_digest impostor{};
    impostor.fill(0x5A);

    utxo_raw_delta batch;
    REQUIRE(batch.merge(ordinary_block_creating(key, entry(0x11, 91722), 91722))
            == delta_merge_result::ok);
    CHECK(batch.merge(block_creating(key, entry(0x22, height_91880), impostor, height_91880))
          == delta_merge_result::unauthorized_duplicate);
}

TEST_CASE("bip30: the right hash at the wrong height is not an exception",
          "[bip30][authorization]") {
    auto const key = outpoint_filled(0x99, 0);

    // And the hash alone is worth nothing either. Matching one half of the pair
    // is what a caller would get from a lookup keyed on the wrong thing.
    utxo_raw_delta batch;
    REQUIRE(batch.merge(ordinary_block_creating(key, entry(0x11, 91722), 91722))
            == delta_merge_result::ok);
    CHECK(batch.merge(block_creating(key, entry(0x22, height_91880), hash_91880(),
                                     height_91880 + 1))
          == delta_merge_result::unauthorized_duplicate);
}

TEST_CASE("bip30: a replacement is authorized per key, not per block",
          "[bip30][authorization]") {
    auto const authorized = outpoint_filled(0xD5, 0);
    auto const other_key = outpoint_filled(0xD6, 0);

    // The exception licenses the duplicated coinbase output, not everything else
    // the block happens to create. A second key colliding in the same block is
    // still a violation.
    utxo_raw_delta batch;
    REQUIRE(batch.merge(ordinary_block_creating(authorized, entry(0x11, 91812), 91812))
            == delta_merge_result::ok);
    REQUIRE(batch.merge(ordinary_block_creating(other_key, entry(0x33, 91813), 91813))
            == delta_merge_result::ok);

    auto duplicate_block = block_creating(authorized, entry(0x22, height_91842),
                                          hash_91842(), height_91842);
    duplicate_block.inserts.emplace(other_key, entry(0x44, height_91842));

    CHECK(batch.merge(std::move(duplicate_block))
          == delta_merge_result::unauthorized_duplicate);
}
