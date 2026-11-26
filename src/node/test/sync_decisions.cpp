// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <set>

#include <kth/node/detail/body_range.hpp>
#include <kth/node/sync/block_tasks.hpp>

using namespace kth::node::sync;

// =============================================================================
// trim_held_suffix — peers are not asked for bodies this node holds itself
// =============================================================================

TEST_CASE("trim_held_suffix: a held suffix is cut off, down to the bodies", "[sync][local_admission]") {
    std::set<uint32_t> const held{9, 10};
    auto const is_held = [&](uint32_t h) { return held.contains(h); };
    // Bodies at 5, headers at 10, the last two local: peers may be asked to 8.
    CHECK(trim_held_suffix(5, 10, is_held) == 8u);
    // Nothing held: the end is the header tip.
    CHECK(trim_held_suffix(5, 8, is_held) == 8u);
}

TEST_CASE("trim_held_suffix: a held height below an unheld one stays in the range", "[sync][local_admission]") {
    // A peer that extended past 7 has it; asking again is only a copy.
    std::set<uint32_t> const held{7};
    auto const is_held = [&](uint32_t h) { return held.contains(h); };
    CHECK(trim_held_suffix(5, 10, is_held) == 10u);
}

TEST_CASE("trim_held_suffix: every owed body held leaves nothing to ask for", "[sync][local_admission]") {
    std::set<uint32_t> const held{6, 7};
    auto const is_held = [&](uint32_t h) { return held.contains(h); };
    // The end lands on the bodies themselves: no range (end <= bodies).
    CHECK(trim_held_suffix(5, 7, is_held) == 5u);
    // Never below the bodies, whatever is held.
    CHECK(trim_held_suffix(5, 5, is_held) == 5u);
}

// =============================================================================
// resume_utxo_built_height — the persisted utxo-built height is authoritative
// =============================================================================

TEST_CASE("resume_utxo_built_height: saved wins even when below start_height", "[sync]") {
    // The regression: block-sync ran ahead (start_height = 962191) while only
    // 962146 was actually built. Resuming must floor at the built height, not at
    // start_height - 1, or blocks (962146, 962190] get marked built without their
    // outputs ever entering UTXO-Z.
    CHECK(resume_utxo_built_height(std::optional<uint32_t>{962146}, 962191) == 962146u);
}

TEST_CASE("resume_utxo_built_height: saved wins when equal to start_height", "[sync]") {
    CHECK(resume_utxo_built_height(std::optional<uint32_t>{1000}, 1000) == 1000u);
}

TEST_CASE("resume_utxo_built_height: saved wins when above start_height", "[sync]") {
    CHECK(resume_utxo_built_height(std::optional<uint32_t>{2000}, 1500) == 2000u);
}

TEST_CASE("resume_utxo_built_height: saved zero is honoured, not treated as absent", "[sync]") {
    CHECK(resume_utxo_built_height(std::optional<uint32_t>{0}, 5000) == 0u);
}

TEST_CASE("resume_utxo_built_height: no saved progress falls back to start_height - 1", "[sync]") {
    CHECK(resume_utxo_built_height(std::nullopt, 1000) == 999u);
    CHECK(resume_utxo_built_height(std::nullopt, 1) == 0u);
    CHECK(resume_utxo_built_height(std::nullopt, 0) == 0u);
}

// =============================================================================
// utxo_batch_len — batch sizing switches on the sync regime
// =============================================================================

constexpr uint32_t batch_size = 1000;

TEST_CASE("utxo_batch_len: a full window is processed as batch_size", "[sync]") {
    CHECK(utxo_batch_len(5000, batch_size, /*stale*/ true)  == batch_size);
    CHECK(utxo_batch_len(5000, batch_size, /*stale*/ false) == batch_size);
    CHECK(utxo_batch_len(batch_size, batch_size, true)      == batch_size);
}

TEST_CASE("utxo_batch_len: IBD waits for a full window (short window -> 0)", "[sync]") {
    CHECK(utxo_batch_len(999, batch_size, /*stale*/ true) == 0u);
    CHECK(utxo_batch_len(1,   batch_size, /*stale*/ true) == 0u);
}

TEST_CASE("utxo_batch_len: at the tip a short window is drained down to one block", "[sync]") {
    CHECK(utxo_batch_len(44, batch_size, /*stale*/ false) == 44u);
    CHECK(utxo_batch_len(1,  batch_size, /*stale*/ false) == 1u);
    CHECK(utxo_batch_len(999, batch_size, /*stale*/ false) == 999u);
}

// =============================================================================
// utxo_fast_batch_reach — how far a batch below the checkpoint may reach
// =============================================================================

TEST_CASE("utxo_fast_batch_reach: below the checkpoint a batch reaches what is stored", "[sync]") {
    // 1,000 chosen, 7,500 stored: all of them, since the limit is 20,000.
    CHECK(utxo_fast_batch_reach(1, 1000, 7500, 900000, 20000) == 7500u);
    CHECK(utxo_fast_batch_reach(300001, 301000, 7500, 900000, 20000) == 307500u);
}

TEST_CASE("utxo_fast_batch_reach: never more blocks than the limit", "[sync]") {
    CHECK(utxo_fast_batch_reach(1, 1000, 700000, 900000, 20000) == 20000u);
    CHECK(utxo_fast_batch_reach(300001, 301000, 700000, 900000, 20000) == 320000u);
}

TEST_CASE("utxo_fast_batch_reach: never past the checkpoint", "[sync]") {
    CHECK(utxo_fast_batch_reach(899001, 900000, 50000, 900000, 20000) == 900000u);
    CHECK(utxo_fast_batch_reach(895001, 896000, 50000, 900000, 20000) == 900000u);
}

TEST_CASE("utxo_fast_batch_reach: a batch is never shortened", "[sync]") {
    // A limit under the batch already chosen does not cut it, and with nothing
    // counted as stored the batch is left alone.
    CHECK(utxo_fast_batch_reach(1, 1000, 1000, 900000, 500) == 1000u);
    CHECK(utxo_fast_batch_reach(1, 44, 44, 900000, 20000) == 44u);
    CHECK(utxo_fast_batch_reach(1, 1000, 0, 900000, 20000) == 1000u);
    CHECK(utxo_fast_batch_reach(1, 1000, 5000, 900000, 0) == 1000u);
}

TEST_CASE("utxo_fast_batch_reach: a batch above the checkpoint is left as it came", "[sync]") {
    CHECK(utxo_fast_batch_reach(900001, 900500, 50000, 900000, 20000) == 900500u);
    // Full validation: the checkpoint grants nothing, so no batch is below it.
    CHECK(utxo_fast_batch_reach(1, 500, 50000, 0, 20000) == 500u);
}

TEST_CASE("utxo_fast_batch_reach: heights near the top of the range do not wrap", "[sync]") {
    auto const top = std::numeric_limits<uint32_t>::max();
    CHECK(utxo_fast_batch_reach(top - 10, top - 5, 20000, top, 20000) == top);
}

// =============================================================================
// utxo_batch_end — where a batch stops
// =============================================================================

TEST_CASE("utxo_validated_batch_reach: a validated batch reaches what is stored, up to the limit", "[sync]") {
    // The bytes cut it when it is read; the count is only a limit.
    CHECK(utxo_validated_batch_reach(1, 7500, 20000) == 7500u);
    CHECK(utxo_validated_batch_reach(438041, 7500, 20000) == 445540u);
    CHECK(utxo_validated_batch_reach(438041, 700000, 20000) == 458040u);
    CHECK(utxo_validated_batch_reach(438041, 1000, 100) == 438140u);
    // One block at the least, whatever it is asked.
    CHECK(utxo_validated_batch_reach(438041, 0, 500) == 438041u);
    CHECK(utxo_validated_batch_reach(438041, 1000, 0) == 438041u);
    // And never past the last height there can be.
    CHECK(utxo_validated_batch_reach(4294967290u, 1000, 20000) == 4294967295u);
}

TEST_CASE("utxo_batch_end: a batch never crosses the trusted height", "[sync]") {
    // Wholly below it, and wholly above it: as it came.
    CHECK(utxo_batch_end(1, 1000, 951146) == 1000u);
    CHECK(utxo_batch_end(951147, 952146, 951146) == 952146u);
    // Across it: the blocks at or below it are their own batch.
    CHECK(utxo_batch_end(950501, 951500, 951146) == 951146u);
    CHECK(utxo_batch_end(951146, 951500, 951146) == 951146u);
    // With no trusted height nothing is cut.
    CHECK(utxo_batch_end(1, 1000, 0) == 1000u);
}

TEST_CASE("utxo_batch_len: nothing available -> wait, regardless of regime", "[sync]") {
    CHECK(utxo_batch_len(0, batch_size, /*stale*/ false) == 0u);
    CHECK(utxo_batch_len(0, batch_size, /*stale*/ true)  == 0u);
}
