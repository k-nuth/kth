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

// -----------------------------------------------------------------------------
// Through the production constructor
// -----------------------------------------------------------------------------
//
// The cases above build a block delta by hand, which is how the properties get
// stated -- but it is also how a fix can look complete while production never
// licenses anything. These go through process_compact_block_utxos, the same
// function utxo_build_task calls, so what is asserted is what the node builds.

namespace {

// One coinbase output and one ordinary output, so "coinbase only" is observable.
utxo_compact_block two_output_block(utxoz::raw_outpoint const& coinbase_key,
                                    utxoz::raw_outpoint const& other_key,
                                    std::span<uint8_t const> raw) {
    utxo_compact_block block;
    block.outputs.push_back({coinbase_key, raw, /*coinbase*/ true, 0});
    block.outputs.push_back({other_key, raw, /*coinbase*/ false, 0});
    return block;
}

} // namespace

TEST_CASE("bip30: the production constructor licenses the exception's coinbase only",
          "[bip30][production]") {
    auto const coinbase_key = outpoint_filled(0xC0, 0);
    auto const other_key = outpoint_filled(0xC1, 0);

    // A plausible output body; the shape is what matters, not the content.
    std::vector<uint8_t> raw(16, 0x01);
    auto const block = two_output_block(coinbase_key, other_key, raw);

    auto const built = process_compact_block_utxos(
        block, hash_91880(), height_91880, /*mtp*/ 0u,
        domain::config::network::mainnet, /*file*/ int16_t{0}, /*data_pos*/ 0u, nullptr);
    REQUIRE(built.has_value());

    // Both outputs are created...
    CHECK(built->inserts.contains(coinbase_key));
    CHECK(built->inserts.contains(other_key));

    // ...and exactly one is licensed. The exception is about a duplicated
    // coinbase transaction, so an ordinary output of the same block gets nothing.
    CHECK(built->authorized_replacements.contains(coinbase_key));
    CHECK_FALSE(built->authorized_replacements.contains(other_key));
    CHECK(built->authorized_replacements.size() == 1u);
}

TEST_CASE("bip30: the production constructor licenses nothing off the exception",
          "[bip30][production]") {
    auto const coinbase_key = outpoint_filled(0xC2, 0);
    auto const other_key = outpoint_filled(0xC3, 0);
    std::vector<uint8_t> raw(16, 0x01);
    auto const block = two_output_block(coinbase_key, other_key, raw);

    // The right hash one block too high: not the pair, so not the exception.
    auto const wrong_height = process_compact_block_utxos(
        block, hash_91880(), height_91880 + 1, /*mtp*/ 0u,
        domain::config::network::mainnet, int16_t{0}, 0u, nullptr);
    REQUIRE(wrong_height.has_value());
    CHECK(wrong_height->authorized_replacements.empty());

    // The right height with another block's hash: likewise.
    hash_digest impostor{};
    impostor.fill(0x5A);
    auto const wrong_hash = process_compact_block_utxos(
        block, impostor, height_91880, /*mtp*/ 0u,
        domain::config::network::mainnet, int16_t{0}, 0u, nullptr);
    REQUIRE(wrong_hash.has_value());
    CHECK(wrong_hash->authorized_replacements.empty());

    // And an ordinary block nowhere near either exception.
    auto const ordinary = process_compact_block_utxos(
        block, impostor, 400000u, /*mtp*/ 0u,
        domain::config::network::mainnet, int16_t{0}, 0u, nullptr);
    REQUIRE(ordinary.has_value());
    CHECK(ordinary->authorized_replacements.empty());
}

TEST_CASE("bip30: a licence built by the production constructor reaches the batch",
          "[bip30][production]") {
    auto const coinbase_key = outpoint_filled(0xC4, 0);
    auto const other_key = outpoint_filled(0xC5, 0);
    std::vector<uint8_t> raw(16, 0x01);
    auto const block = two_output_block(coinbase_key, other_key, raw);

    auto built = process_compact_block_utxos(
        block, hash_91880(), height_91880, /*mtp*/ 0u,
        domain::config::network::mainnet, int16_t{0}, 0u, nullptr);
    REQUIRE(built.has_value());

    // The original, from an ordinary earlier block.
    utxo_raw_delta batch;
    REQUIRE(batch.merge(ordinary_block_creating(coinbase_key, entry(0xAA, 91722), 91722))
            == delta_merge_result::ok);

    // The duplicate, exactly as production builds it. Accepted because the
    // licence travelled with it, and the new entry wins.
    REQUIRE(batch.merge(std::move(*built)) == delta_merge_result::ok);
    auto const it = batch.inserts.find(coinbase_key);
    REQUIRE(it != batch.inserts.end());
    CHECK(it->second.height == height_91880);

    // And the batch carries the licence onward, because whatever applies it has
    // to tell a replacement from a plain insert: the store already holds what a
    // replacement displaces.
    CHECK(batch.authorized_replacements.contains(coinbase_key));
}

TEST_CASE("bip30: clearing a batch clears its licences", "[bip30][production]") {
    auto const key = outpoint_filled(0xC6, 0);
    std::vector<uint8_t> raw(16, 0x01);

    utxo_compact_block block;
    block.outputs.push_back({key, raw, /*coinbase*/ true, 0});

    auto built = process_compact_block_utxos(
        block, hash_91842(), height_91842, /*mtp*/ 0u,
        domain::config::network::mainnet, int16_t{0}, 0u, nullptr);
    REQUIRE(built.has_value());

    utxo_raw_delta batch;
    REQUIRE(batch.merge(std::move(*built)) == delta_merge_result::ok);
    REQUIRE(batch.authorized_replacements.contains(key));

    batch.clear();

    // A licence that outlived the batch it was granted for would authorize a
    // replacement in whatever batch reused the object.
    CHECK(batch.authorized_replacements.empty());
    CHECK(batch.inserts.empty());
    CHECK(batch.deletes.empty());

    // Demonstrated, not just asserted: the same collision is now refused.
    REQUIRE(batch.merge(ordinary_block_creating(key, entry(0x11, 91812), 91812))
            == delta_merge_result::ok);
    CHECK(batch.merge(ordinary_block_creating(key, entry(0x22, 91842), 91842))
          == delta_merge_result::unauthorized_duplicate);
}
