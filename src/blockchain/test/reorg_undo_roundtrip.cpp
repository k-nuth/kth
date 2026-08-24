// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <vector>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unistd.h>

#include <kth/blockchain/utxo_builder.hpp>
#include <kth/database/databases/utxoz_database.hpp>

using namespace kth;
using namespace kth::blockchain;
using namespace kth::database;

namespace {

std::filesystem::path fresh_dir(char const* tag) {
    auto const p = std::filesystem::temp_directory_path() /
        ("kth_reorg_undo_" + std::string(tag) + "_" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    REQUIRE(std::filesystem::create_directories(p, ec));
    REQUIRE( ! ec);
    return p;
}

utxoz::raw_outpoint make_outpoint(uint8_t seed, uint32_t index) {
    hash_digest txid{};
    txid[0] = seed;
    txid[31] = uint8_t(0xFF - seed);
    return utxoz::make_outpoint(std::span<uint8_t const, 32>(txid.data(), 32), index);
}

// The storage-native payload for one UTXO, matching what utxo_build produces:
// 8 bytes {file_number, tx_offset} in reference mode, a serialized entry otherwise.
std::vector<uint8_t> make_payload(uint32_t file_number, uint32_t tx_offset) {
#ifdef KTH_UTXOZ_REFERENCE_MODE
    std::vector<uint8_t> value(8);
    std::memcpy(value.data(), &file_number, 4);
    std::memcpy(value.data() + 4, &tx_offset, 4);
    return value;
#else
    // Full mode stores a variable-length payload. The exact bytes do not matter
    // here — what is under test is that whatever was stored comes back unchanged,
    // so an opaque payload of a distinctive length is enough.
    std::vector<uint8_t> value(32 + (file_number % 16));
    for (size_t i = 0; i < value.size(); ++i) {
        value[i] = uint8_t(file_number + tx_offset + i);
    }
    return value;
#endif
}

// The two raw lookups capture_block_undo needs, forwarded to a bare database.
// In production block_chain provides these; here it keeps the test on the real
// code path without standing up a whole chain.
struct db_source {
    utxoz_database& db;

    auto find_utxo_raw(utxoz::raw_outpoint const& key, uint32_t height) const {
        return db.find_raw(key, height);
    }
    auto utxo_resolve_raw(std::span<utxoz::lookup_request const> requests) const {
        return db.resolve_raw(requests);
    }
};

// A source that fails every read the way a broken storage layer would, to reach
// the branch a working database cannot produce on demand.
struct failing_source {
    result_code error{result_code::other};
    mutable size_t reads{0};
    mutable size_t sweeps{0};

    std::expected<utxoz_database::raw_stored, result_code> find_utxo_raw(
        utxoz::raw_outpoint const&, uint32_t) const {
        ++reads;
        return std::unexpected(error);
    }

    std::expected<utxoz_database::raw_resolution, result_code>
    utxo_resolve_raw(std::span<utxoz::lookup_request const>) const {
        ++sweeps;
        return std::unexpected(error);
    }
};

} // namespace

// =============================================================================
// Reorg undo: capture -> erase -> restore
// =============================================================================
//
// The correctness claim behind the undo record is that a spent output can be put
// back EXACTLY as it was: same storage payload and, critically, the same ORIGINAL
// creation height. Restoring with the spending block's height instead would
// corrupt both coinbase maturity and (in reference mode) the median-time-past
// window used when the UTXO is later resolved — silently, and only for outputs
// that a reorg touched.
//
// This exercises that round-trip against a real UTXO-Z database, which is the
// piece disconnect_block depends on.

TEST_CASE("a spent UTXO is restored with its original payload and height", "[reorg][undo]") {
    utxoz_database db;
    REQUIRE(db.open(fresh_dir("restore"), true));

    // Three UTXOs created at different heights — the height spread is the point:
    // a restore that used one height for all of them would still pass a naive test.
    struct fixture { utxoz::raw_outpoint key; uint32_t height; std::vector<uint8_t> value; };
    std::vector<fixture> created{
        {make_outpoint(1, 0), 100, make_payload(0, 4096)},
        {make_outpoint(2, 1), 5000, make_payload(3, 123456)},
        {make_outpoint(3, 7), 99999, make_payload(11, 7)},
    };

    boost::unordered_flat_map<utxoz::raw_outpoint, utxo_raw_value, outpoint_fast_hasher> inserts;
    for (auto const& f : created) {
        inserts.emplace(f.key, utxo_raw_value{f.value, f.height});
    }
    boost::unordered_flat_map<utxoz::raw_outpoint, uint32_t, outpoint_fast_hasher> no_deletes;
    REQUIRE(db.apply_inserts_raw(inserts) == result_code::success);

    // --- capture: through the production path, not a hand-rolled equivalent ---
    // The spending block's own delta: it deletes all three, creating nothing.
    utxo_raw_delta block_delta;
    for (auto const& f : created) {
        block_delta.deletes.emplace(f.key, 200000u);   // spending height
    }
    utxo_raw_delta const empty_batch;

    db_source source{db};
    auto captured = capture_block_undo(block_delta, empty_batch, source, 200000u);
    REQUIRE(captured.has_value());
    REQUIRE(captured->spent.size() == created.size());

    // Every record must carry the ORIGINAL creation height, not the spend height
    // it was captured at. Match by key — the record's order is unspecified.
    for (auto const& f : created) {
        auto const it = std::find_if(captured->spent.begin(), captured->spent.end(),
            [&](auto const& e) { return e.key == f.key; });
        REQUIRE(it != captured->spent.end());
        CHECK(it->height == f.height);
        CHECK(it->value == f.value);
        CHECK(it->height != 200000u);
    }

    // --- spend: the block being connected erases them ---
    boost::unordered_flat_map<utxoz::raw_outpoint, utxo_raw_value, outpoint_fast_hasher> no_inserts;
    std::vector<utxoz::deferred_deletion_entry> deletes;
    for (auto const& f : created) {
        deletes.emplace_back(f.key, 200000u);   // spending height, not the creation height
    }
    REQUIRE(db.apply_inserts_raw(no_inserts) == result_code::success);

    // The spend itself. Every one of these keys was just inserted, so the whole
    // batch must land in `erased`: nothing here is entitled to be absent.
    auto const spent_progress = db.apply_deletes(deletes);
    REQUIRE(spent_progress.erased.size() == deletes.size());
    REQUIRE(spent_progress.absent.empty());
    REQUIRE(spent_progress.unresolved.empty());

    for (auto const& f : created) {
        CHECK_FALSE(db.find_raw(f.key, 200001).has_value());
    }

    // --- disconnect: the inverse delta puts them back ---
    boost::unordered_flat_map<utxoz::raw_outpoint, utxo_raw_value, outpoint_fast_hasher> restores;
    for (auto const& u : captured->spent) {
        restores.emplace(u.key, utxo_raw_value{u.value, u.height});
    }
    REQUIRE(db.apply_inserts_raw(restores) == result_code::success);

    // The set must be indistinguishable from before the spend.
    for (auto const& f : created) {
        auto stored = db.find_raw(f.key, 200002);
        REQUIRE(stored.has_value());
        CHECK(stored->value == f.value);
        CHECK(stored->height == f.height);   // ORIGINAL height, not the spend height
    }
}

TEST_CASE("outputs created and spent in the same block need no undo entry", "[reorg][undo]") {
    // A block that creates an output and spends it internally leaves nothing in
    // the UTXO set, so disconnecting it must NOT restore that output — it never
    // existed beforehand. process_compact_block_utxos cancels the pair, which is
    // what keeps it out of the undo record.
    utxoz_database db;
    REQUIRE(db.open(fresh_dir("internal"), true));

    // Build a block that creates one output and spends it in the same block, and
    // run it through the real producer — the cancellation is its behaviour, so a
    // hand-built delta would not test anything.
    hash_digest txid{};
    txid[0] = 9;
    txid[31] = 0xF6;
    auto const internal = utxoz::make_outpoint(std::span<uint8_t const, 32>(txid.data(), 32), 0);

    std::vector<uint8_t> raw_out(16, 0x11);
    utxo_compact_block block;
    block.outputs.push_back({internal, std::span<uint8_t const>(raw_out.data(), raw_out.size()),
                             /*coinbase*/ false, /*tx_start*/ 0u});
    block.inputs.push_back({internal});   // spent by a later tx in the same block

    auto delta_result = process_compact_block_utxos(block, null_hash, /*height*/ 700u, /*mtp*/ 12u,
                                             domain::config::network::mainnet,
                                             /*file*/ 0, /*data_pos*/ 0u, nullptr);
    REQUIRE(delta_result.has_value());
    auto& delta = *delta_result;

    // The producer cancels the pair: nothing to insert, and nothing to delete —
    // which is what keeps the output out of the undo record.
    CHECK(delta.inserts.empty());
    CHECK(delta.deletes.empty());

    db_source source{db};
    utxo_raw_delta const empty_batch;
    auto captured = capture_block_undo(delta, empty_batch, source, 700u);
    REQUIRE(captured.has_value());
    CHECK(captured->spent.empty());   // no entry => disconnect will not restore it

    REQUIRE(db.apply_inserts_raw(delta.inserts) == result_code::success);
    CHECK_FALSE(db.find_raw(internal, 701).has_value());
}

TEST_CASE("find_raw reports a miss for an unknown outpoint", "[reorg][undo]") {
    // Undo capture treats a miss as "carry it into this capture's own batch,
    // resolve that batch, then fail loudly" — it must not silently produce an
    // empty record.
    utxoz_database db;
    REQUIRE(db.open(fresh_dir("miss"), true));

    auto const missed = db.find_raw(make_outpoint(42, 3), 1);
    REQUIRE_FALSE(missed.has_value());
    CHECK(missed.error() == result_code::not_resolved);   // the documented contract
}

// =============================================================================
// Telling a failed read apart from a missing output
// =============================================================================
//
// find_utxo_raw answers not_resolved for "the active versions cannot answer" and
// other codes for a read that failed. Capture used to carry both onward the same
// way, so a storage failure came back out as a missing output — the caller told
// the set does not hold what a block spends, when what happened is that it could
// not be asked.

TEST_CASE("a failed read is reported as itself, not as a missing output", "[reorg][undo]") {
    failing_source source{result_code::other, 0, 0};

    utxo_raw_delta delta;
    delta.deletes.emplace(make_outpoint(7, 0), 900u);
    utxo_raw_delta const empty_batch;

    auto const captured = capture_block_undo(delta, empty_batch, source, 900u);

    REQUIRE_FALSE(captured.has_value());
    CHECK(captured.error() == result_code::other);

    // And it did not go through the batch resolution: carrying a read that failed
    // is what turned it into a missing output in the first place.
    CHECK(source.sweeps == 0);
}

TEST_CASE("an output still missing after the sweep is reported as not found", "[reorg][undo]") {
    // The other half of the split: not_resolved is what the resolution is for, so
    // it is kept by the caller, resolved as a batch, and only then reported.
    failing_source source{result_code::not_resolved, 0, 0};

    utxo_raw_delta delta;
    delta.deletes.emplace(make_outpoint(8, 0), 900u);
    utxo_raw_delta const empty_batch;

    auto const captured = capture_block_undo(delta, empty_batch, source, 900u);

    REQUIRE_FALSE(captured.has_value());
    CHECK(captured.error() == result_code::not_resolved);
    CHECK(source.sweeps == 1);
}

TEST_CASE("an output the same batch created needs no read at all", "[reorg][undo]") {
    // The in-flight parent case, pinned here because it is what keeps a failing
    // source from being consulted: the batch delta answers first.
    failing_source source{result_code::other, 0, 0};

    auto const key = make_outpoint(9, 0);
    utxo_raw_delta delta;
    delta.deletes.emplace(key, 900u);

    utxo_raw_delta batch;
    batch.inserts.emplace(key, utxo_raw_value{data_chunk{0x01, 0x02}, 899u});

    auto const captured = capture_block_undo(delta, batch, source, 900u);

    REQUIRE(captured.has_value());
    REQUIRE(captured->spent.size() == 1);
    CHECK(captured->spent.front().height == 899u);
    CHECK(source.reads == 0);
    CHECK(source.sweeps == 0);
}
