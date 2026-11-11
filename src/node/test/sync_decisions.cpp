// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
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
// utxo_batch_end — where a batch stops
// =============================================================================

TEST_CASE("utxo_batch_end: a validated batch holds at most the configured blocks", "[sync]") {
    // Above the trusted height (checkpoint 0: full validation).
    CHECK(utxo_batch_end(1, 1000, 0, 500) == 500u);
    CHECK(utxo_batch_end(438041, 439040, 0, 500) == 438540u);
    CHECK(utxo_batch_end(438041, 439040, 0, 100) == 438140u);
    CHECK(utxo_batch_end(438041, 439040, 0, 1000) == 439040u);
    // A shorter window is not lengthened.
    CHECK(utxo_batch_end(438041, 438060, 0, 500) == 438060u);
    // Zero is treated as one block, never as an empty batch.
    CHECK(utxo_batch_end(438041, 439040, 0, 0) == 438041u);
}

TEST_CASE("utxo_batch_end: a batch never crosses the trusted height", "[sync]") {
    // Below the checkpoint the cap does not apply: those blocks hold no undo.
    CHECK(utxo_batch_end(1, 1000, 951146, 100) == 1000u);
    CHECK(utxo_batch_end(950501, 951500, 951146, 100) == 951146u);
    // The first batch above it is capped again.
    CHECK(utxo_batch_end(951147, 952146, 951146, 100) == 951246u);
}

TEST_CASE("utxo_batch_len: nothing available -> wait, regardless of regime", "[sync]") {
    CHECK(utxo_batch_len(0, batch_size, /*stale*/ false) == 0u);
    CHECK(utxo_batch_len(0, batch_size, /*stale*/ true)  == 0u);
}
