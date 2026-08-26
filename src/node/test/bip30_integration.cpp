// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// BIP30 replacements against the real store (#695)
// =============================================================================
//
// The delta-level controls in blockchain/test/bip30_duplicate_coinbase.cpp state
// the properties and exercise apply_utxo_delta against a double. What a double
// cannot answer is whether UTXO-Z behaves the way the double pretends: that it
// refuses to write over a live key, that a withdrawal actually removes one, and
// that what a later read returns is the new entry rather than the old.
//
// These run the same helper the build task runs, against a real chain_fixture --
// a real UTXO-Z on disk, in whichever mode this build was configured for. Both
// modes are covered by building the suite twice; the payload is eight bytes so
// one shape serves both.
//
// The fixture is regtest and is_bip30_exception is mainnet-only, deliberately
// untouched here. That is why the delta is licensed explicitly: this layer is
// about what the store does with an authorized replacement, and the layer that
// decides WHO gets authorized is tested where it lives, over the real function.

namespace {

utxoz::raw_outpoint key_filled(uint8_t seed, uint32_t index = 0) {
    hash_digest txid{};
    txid.fill(seed);
    return utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, index);
}

blockchain::utxo_raw_value value_of(uint8_t byte, uint32_t height) {
    return blockchain::utxo_raw_value{std::vector<uint8_t>(8, byte), height};
}

blockchain::utxo_raw_delta plain_delta(utxoz::raw_outpoint const& key,
                                       blockchain::utxo_raw_value value) {
    blockchain::utxo_raw_delta delta;
    delta.inserts.emplace(key, std::move(value));
    return delta;
}

blockchain::utxo_raw_delta licensed_delta(utxoz::raw_outpoint const& key,
                                          blockchain::utxo_raw_value value) {
    auto delta = plain_delta(key, std::move(value));
    delta.authorized_replacements.insert(key);
    return delta;
}

// Apply one delta through the same helper utxo_build_task calls, in its own
// window, the way one batch does.
blockchain::delta_apply_result apply_batch(blockchain::block_chain& chain,
                                           blockchain::utxo_raw_delta const& delta,
                                           uint32_t height) {
    auto window = chain.begin_utxo_write();
    REQUIRE(window);
    window->mark_mutating();
    auto const result = blockchain::apply_utxo_delta(chain, *window, delta, height);
    if (result.ok()) {
        window->complete();
    }
    return result;
}

// What the set holds for a key, read back through the store rather than inferred.
struct stored_entry {
    bool present{false};
    uint32_t height{0};
    uint8_t first_byte{0};
};

stored_entry read_back(blockchain::block_chain& chain, utxoz::raw_outpoint const& key,
                       uint32_t at_height) {
    auto found = chain.find_utxo_raw(key, at_height);
    if ( ! found) {
        return {};
    }
    stored_entry out;
    out.present = true;
    out.height = found->height;
    out.first_byte = found->value.empty() ? 0 : found->value.front();
    return out;
}

// A trunk of `len` empty blocks connected through the node's own path, so the
// state below is one the node actually built.
std::vector<domain::chain::block> connect_trunk(chain_fixture& fixture, uint32_t len) {
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (len + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= len; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }

    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);
    return trunk;
}

} // namespace

// -----------------------------------------------------------------------------
// Integration
// -----------------------------------------------------------------------------

TEST_CASE("bip30 store: cross-batch replacement leaves the new entry",
          "[node][bip30][store]") {
    chain_fixture fixture("bip30_cross_batch");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const key = key_filled(0xE3);

    // Batch one commits the original, exactly as an earlier batch would.
    REQUIRE(apply_batch(chain, plain_delta(key, value_of(0xAA, 91722)), 91722).ok());

    // The store really does hold it -- without this the case below could pass
    // against a store that silently dropped the first insert.
    auto const before = read_back(chain, key, 91722);
    REQUIRE(before.present);
    REQUIRE(before.height == 91722u);
    REQUIRE(before.first_byte == 0xAA);

    // Batch two carries the licensed replacement. This is the partition that
    // used to end in duplicated_key and a stopped node.
    auto const applied = apply_batch(chain, licensed_delta(key, value_of(0xBB, 91880)), 91880);
    REQUIRE(applied.ok());
    CHECK(applied.erased == 1u);
    CHECK(applied.absent == 0u);

    // Read back from the store: the new entry, with the new height.
    auto const after = read_back(chain, key, 91880);
    REQUIRE(after.present);
    CHECK(after.height == 91880u);
    CHECK(after.first_byte == 0xBB);
}

TEST_CASE("bip30 store: an authorized insert with nothing to displace goes in",
          "[node][bip30][store]") {
    chain_fixture fixture("bip30_authorized_absent");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const key = key_filled(0xE4);

    // Licensed, and the set holds nothing to overwrite -- what the second
    // exception looks like after a rewind past the first.
    auto const applied = apply_batch(chain, licensed_delta(key, value_of(0xBB, 91880)), 91880);
    REQUIRE(applied.ok());
    CHECK(applied.erased == 0u);
    CHECK(applied.absent == 1u);

    auto const after = read_back(chain, key, 91880);
    REQUIRE(after.present);
    CHECK(after.height == 91880u);
    CHECK(after.first_byte == 0xBB);
}

TEST_CASE("bip30 store: an unlicensed duplicate is refused by the store",
          "[node][bip30][store]") {
    chain_fixture fixture("bip30_unlicensed");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const key = key_filled(0xE6);
    REQUIRE(apply_batch(chain, plain_delta(key, value_of(0xAA, 400000)), 400000).ok());

    // No licence, so nothing is withdrawn on its behalf and UTXO-Z refuses to
    // write over the live key. This is the behaviour the double imitates, and
    // this is where it is confirmed against the real store.
    auto window = chain.begin_utxo_write();
    REQUIRE(window);
    window->mark_mutating();
    auto const applied = blockchain::apply_utxo_delta(
        chain, *window, plain_delta(key, value_of(0xBB, 400001)), 400001);

    CHECK(applied.status == blockchain::delta_apply_status::insert_failed);
    REQUIRE(applied.insert_error.has_value());
    CHECK(*applied.insert_error == database::result_code::duplicated_key);
    CHECK(applied.erased == 0u);
}

// -----------------------------------------------------------------------------
// Partition independence
// -----------------------------------------------------------------------------
//
// The same two blocks, cut into batches four different ways. Every one of them
// has to end with the same entry in the store, because where a batch boundary
// falls is decided by download progress and nothing else.

namespace {

// Returns what the store holds for `key` after applying the original at 91722
// and the licensed duplicate at 91880, cut at `partition`.
stored_entry run_partition(char const* tag, int partition) {
    chain_fixture fixture(tag);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const key = key_filled(0xE3);
    auto const original = plain_delta(key, value_of(0xAA, 91722));
    auto duplicate = licensed_delta(key, value_of(0xBB, 91880));

    switch (partition) {
        case 0: {
            // Both in one batch: the merge resolves the replacement and the
            // store never sees the older entry.
            blockchain::utxo_raw_delta batch;
            REQUIRE(batch.merge(blockchain::utxo_raw_delta{original})
                    == blockchain::delta_merge_result::ok);
            REQUIRE(batch.merge(std::move(duplicate))
                    == blockchain::delta_merge_result::ok);
            REQUIRE(apply_batch(chain, batch, 91880).ok());
            break;
        }
        case 1: {
            // A batch before the original, so the store is already open and
            // written to when the pair arrives together.
            REQUIRE(apply_batch(chain, plain_delta(key_filled(0x01), value_of(0x01, 91000)),
                                91000).ok());
            blockchain::utxo_raw_delta batch;
            REQUIRE(batch.merge(blockchain::utxo_raw_delta{original})
                    == blockchain::delta_merge_result::ok);
            REQUIRE(batch.merge(std::move(duplicate))
                    == blockchain::delta_merge_result::ok);
            REQUIRE(apply_batch(chain, batch, 91880).ok());
            break;
        }
        case 2: {
            // The cut between them: the original is committed, the duplicate
            // arrives in its own batch and must withdraw it.
            REQUIRE(apply_batch(chain, original, 91722).ok());
            REQUIRE(apply_batch(chain, duplicate, 91880).ok());
            break;
        }
        default: {
            // A batch that ends immediately before the duplicate, with the
            // original committed two batches back.
            REQUIRE(apply_batch(chain, original, 91722).ok());
            REQUIRE(apply_batch(chain, plain_delta(key_filled(0x02), value_of(0x02, 91800)),
                                91800).ok());
            REQUIRE(apply_batch(chain, duplicate, 91880).ok());
            break;
        }
    }

    return read_back(chain, key, 91880);
}

} // namespace

TEST_CASE("bip30 store: every batch partition ends with the same entry",
          "[node][bip30][partition]") {
    char const* tags[] = {
        "bip30_part_same", "bip30_part_before", "bip30_part_between", "bip30_part_after"};

    for (int p = 0; p < 4; ++p) {
        CAPTURE(p);
        auto const final_entry = run_partition(tags[p], p);
        REQUIRE(final_entry.present);
        CHECK(final_entry.height == 91880u);
        CHECK(final_entry.first_byte == 0xBB);
    }
}

// -----------------------------------------------------------------------------
// Atomicity, end to end
// -----------------------------------------------------------------------------

TEST_CASE("bip30 store: a failure after the withdrawal leaves the record and poisons the gate",
          "[node][bip30][atomicity]") {
    chain_fixture fixture("bip30_withdraw_then_fail");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const key = key_filled(0xF3);
    REQUIRE(apply_batch(chain, plain_delta(key, value_of(0xAA, 91722)), 91722).ok());
    REQUIRE(read_back(chain, key, 91722).present);

    // The record first, and durable, exactly as the build task writes it.
    REQUIRE(chain.begin_transition_record(database::utxo_transition_record{
                .format_version = database::utxo_transition_record::current_format_version,
                .type = database::transition_type::connect_batch,
                .operation_id = database::make_operation_id(),
                .first_height = 91873,
                .intended_last_height = 91880,
                .state = database::transition_state::in_progress})
            == database::result_code::success);
    REQUIRE(chain.env_sync() == database::result_code::success);

    // The delta withdraws the live entry and then fails on the insert. The
    // second key is what makes it fail: it is already there, so UTXO-Z refuses
    // the batch AFTER the withdrawal has already happened. That is the window
    // between the two mutations, reached without a storage fault.
    auto const clash = key_filled(0xF4);
    REQUIRE(apply_batch(chain, plain_delta(clash, value_of(0xCC, 91800)), 91800).ok());

    auto delta = licensed_delta(key, value_of(0xBB, 91880));
    delta.inserts.emplace(clash, value_of(0xDD, 91880));

    {
        auto window = chain.begin_utxo_write();
        REQUIRE(window);
        window->mark_mutating();
        auto const applied = blockchain::apply_utxo_delta(chain, *window, delta, 91880);

        // Reported, not hidden, and the withdrawal did happen.
        CHECK(applied.status == blockchain::delta_apply_status::insert_failed);
        CHECK(applied.erased == 1u);

        // complete() is deliberately NOT called: the window leaves through its
        // destructor, which is what latches the gate.
    }

    // The record is still there, and it says recovery is required. A build that
    // mutated before recording would leave nothing here.
    auto const check = chain.read_transition_record();
    REQUIRE(check.status == database::transition_status::recovery_required);
    REQUIRE(check.record.has_value());
    CHECK(check.record->first_height == 91873u);

    // The gate is poisoned: no further write is granted.
    CHECK_FALSE(chain.begin_utxo_write().has_value());

    // And the next start refuses rather than resuming past a batch that did not
    // finish -- the whole point of the record.
    CHECK_FALSE(fixture.restart());
}

// -----------------------------------------------------------------------------
// Undo and reorg
// -----------------------------------------------------------------------------
//
// Disconnecting the duplicate has to put the ORIGINAL entry back, not remove the
// key. The two sets disconnect_block builds overlap for exactly this shape -- the
// block created the outpoint, and the undo carries a previous value for it -- and
// nothing else produces that overlap, so no marker was added to the undo format.
//
// The block here is a real regtest block whose coinbase output is genuinely in
// the set, and the undo record is written the way the build task writes one. What
// makes it the BIP30 shape is the record carrying a previous value for a key the
// block creates; that is what disconnect_block reads.

TEST_CASE("bip30 store: disconnecting a replacement restores the previous entry",
          "[node][bip30][reorg]") {
    chain_fixture fixture("bip30_disconnect_restores");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const trunk = connect_trunk(fixture, 2);
    auto const& blk = trunk.back();
    auto const idx = database::header_index::index_t{2};

    // The key the block created, and which the set therefore holds at height 2.
    auto const& coinbase = blk.transactions().front();
    auto const txid = coinbase.hash();
    auto const key = utxoz::make_outpoint(
        std::span<uint8_t const, 32>{txid.data(), txid.size()}, 0);

    auto const connected = read_back(chain, key, 2);
    REQUIRE(connected.present);
    REQUIRE(connected.height == 2u);

    // An undo record carrying a PREVIOUS value for that same key: the shape a
    // BIP30 replacement leaves behind. Its payload and height are deliberately
    // different from what the block created, so which one survives is visible.
    database::block_undo undo;
    undo.spent.push_back(database::spent_output{key, std::vector<uint8_t>(8, 0x7E), 1u});
    REQUIRE(chain.store_block_undo(idx, undo, chain.headers().get_prev_block_hash(idx))
            .has_value());

    boost::unordered_flat_map<utxoz::raw_outpoint, bool> tolerated;
    std::vector<utxoz::deferred_deletion_entry> pending;
    {
        auto window = chain.begin_utxo_write();
        REQUIRE(window);
        window->mark_mutating();
        REQUIRE(chain.disconnect_block(2, *window, tolerated, pending)
                == database::disconnect_result::ok);
        // The rewind owns the deletions and applies them at the end. Applying
        // them here is what would expose the bug this guards: if the key were in
        // this batch, the restore above would be undone.
        if ( ! pending.empty()) {
            auto const progress = chain.utxo_apply_deletes(*window, pending);
            REQUIRE(progress.unresolved.empty());
        }
        window->complete();
    }

    // The key is NOT in the deletion batch: it was restored, not removed.
    for (auto const& entry : pending) {
        CHECK(entry.key != key);
    }

    // And the set holds the previous entry, with the previous height.
    auto const restored = read_back(chain, key, 2);
    REQUIRE(restored.present);
    CHECK(restored.height == 1u);
    CHECK(restored.first_byte == 0x7E);
}

TEST_CASE("bip30 store: reconnecting after a disconnect installs the new entry again",
          "[node][bip30][reorg]") {
    chain_fixture fixture("bip30_reconnect");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const trunk = connect_trunk(fixture, 2);
    auto const& blk = trunk.back();
    auto const idx = database::header_index::index_t{2};

    auto const& coinbase = blk.transactions().front();
    auto const txid = coinbase.hash();
    auto const key = utxoz::make_outpoint(
        std::span<uint8_t const, 32>{txid.data(), txid.size()}, 0);

    database::block_undo undo;
    undo.spent.push_back(database::spent_output{key, std::vector<uint8_t>(8, 0x7E), 1u});
    REQUIRE(chain.store_block_undo(idx, undo, chain.headers().get_prev_block_hash(idx))
            .has_value());

    boost::unordered_flat_map<utxoz::raw_outpoint, bool> tolerated;
    std::vector<utxoz::deferred_deletion_entry> pending;
    {
        auto window = chain.begin_utxo_write();
        REQUIRE(window);
        window->mark_mutating();
        REQUIRE(chain.disconnect_block(2, *window, tolerated, pending)
                == database::disconnect_result::ok);
        window->complete();
    }
    REQUIRE(read_back(chain, key, 2).height == 1u);

    // Reconnecting is the licensed replacement again: the previous entry is live,
    // so it is withdrawn and the new one installed. The round trip has to land
    // back exactly where the connect left it.
    auto const applied = apply_batch(chain, licensed_delta(key, value_of(0x99, 2)), 2);
    REQUIRE(applied.ok());
    CHECK(applied.erased == 1u);

    auto const again = read_back(chain, key, 2);
    REQUIRE(again.present);
    CHECK(again.height == 2u);
    CHECK(again.first_byte == 0x99);
}
