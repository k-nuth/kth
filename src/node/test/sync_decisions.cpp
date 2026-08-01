// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

#include <kth/node/sync/block_tasks.hpp>

using namespace kth::node::sync;

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

TEST_CASE("utxo_batch_len: nothing available -> wait, regardless of regime", "[sync]") {
    CHECK(utxo_batch_len(0, batch_size, /*stale*/ false) == 0u);
    CHECK(utxo_batch_len(0, batch_size, /*stale*/ true)  == 0u);
}
