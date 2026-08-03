// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/blockchain.hpp>

using namespace kth;
using namespace kth::blockchain;

// Note: We use explicit kth::domain::chain:: qualification for header types
// to avoid ambiguity with kth::domain::message::header

// =============================================================================
// Helper functions
// =============================================================================

namespace {

// Create a deterministic hash from an integer
hash_digest make_hash(uint32_t id) {
    hash_digest hash{};
    hash[0] = uint8_t(id & 0xFF);
    hash[1] = uint8_t((id >> 8) & 0xFF);
    hash[2] = uint8_t((id >> 16) & 0xFF);
    hash[3] = uint8_t((id >> 24) & 0xFF);
    return hash;
}

// Create a header with specific prev_hash
domain::chain::header make_header_with_prev(hash_digest const& prev_hash, uint32_t height) {
    return domain::chain::header{1, prev_hash, make_hash(height + 1000), height * 600, 0x1d00ffff, height};
}

// Build a simple chain of headers in the index
// Returns the hash of the last (tip) header
hash_digest build_chain(header_index& index, size_t length) {
    hash_digest prev_hash = null_hash;

    for (size_t i = 0; i < length; ++i) {
        auto hdr = make_header_with_prev(prev_hash, uint32_t(i));
        auto hash = kth::domain::chain::hash(hdr);

        auto result = index.add(hash, hdr);
        REQUIRE(result.inserted);

        prev_hash = hash;
    }

    return prev_hash;
}

// Create test settings
settings make_test_settings() {
    settings s(domain::config::network::mainnet);
    // Disable checkpoints for testing
    s.checkpoints.clear();
    return s;
}

} // namespace

// =============================================================================
// Stale batch detection tests - Issue #11 fix (2026-02-02)
// =============================================================================

TEST_CASE("header_organizer stale batch returns stale_chain error", "[header_organizer][stale]") {
    // Setup: Create an index with a chain of 10 headers
    header_index index;
    auto tip_hash = build_chain(index, 10);
    REQUIRE(index.size() == 10);

    // Get hash at height 5 (an ancestor of the tip)
    auto ancestor_hash = index.get_hash(5);
    REQUIRE(ancestor_hash != null_hash);

    // Create organizer and sync its tip with the index
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();  // Must sync tip after populating index

    // Create a batch of headers that connects to the ancestor (height 5), not the tip (height 9)
    // This simulates a stale/duplicate retry request
    domain::message::header::list stale_headers;
    hash_digest prev = ancestor_hash;
    for (int i = 0; i < 3; ++i) {
        auto hdr = make_header_with_prev(prev, uint32_t(100 + i));  // Different heights to avoid collision
        prev = kth::domain::chain::hash(hdr);
        stale_headers.push_back(std::move(hdr));
    }

    // Add the stale batch
    auto result = organizer.add_headers(stale_headers);

    // 2026-02-02: Should return stale_chain error, NOT success
    // This prevents the sync coordinator from marking header sync as "complete"
    REQUIRE(result.error == error::stale_chain);
    REQUIRE(result.headers_added == 0);
}

TEST_CASE("header_organizer normal batch returns success", "[header_organizer][stale]") {
    // Setup: Create an index with a chain of 10 headers
    header_index index;
    auto tip_hash = build_chain(index, 10);
    REQUIRE(index.size() == 10);

    // Create organizer and sync its tip with the index
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();  // Must sync tip after populating index

    // Create a batch of headers that connects to the tip (normal case)
    domain::message::header::list new_headers;
    hash_digest prev = tip_hash;
    for (int i = 0; i < 3; ++i) {
        auto hdr = make_header_with_prev(prev, uint32_t(10 + i));
        prev = kth::domain::chain::hash(hdr);
        new_headers.push_back(std::move(hdr));
    }

    // Add the normal batch
    auto result = organizer.add_headers(new_headers);

    // Should succeed and add all headers
    REQUIRE(!result.error);
    REQUIRE(result.headers_added == 3);
    REQUIRE(index.size() == 13);
}

TEST_CASE("header_organizer empty batch returns success with zero count", "[header_organizer][stale]") {
    // Setup: Create an index with a chain of 10 headers
    header_index index;
    (void)build_chain(index, 10);  // tip_hash not used in this test

    // Create organizer and sync its tip with the index
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();  // Must sync tip after populating index

    // Add empty batch
    domain::message::header::list empty_headers;
    auto result = organizer.add_headers(empty_headers);

    // Empty batch should succeed with zero count (this IS a valid "sync complete" signal)
    REQUIRE(!result.error);
    REQUIRE(result.headers_added == 0);
}

TEST_CASE("header_organizer note_block_validated respects the startup window", "[header_organizer][finalization]") {
    // A freshly constructed organizer is within the finalization startup window
    // (uptime < finalization_delay), so no block finalizes even once blocks are
    // validated well past the depth threshold. (The finalization rule itself is
    // covered by the finalization component tests.)
    header_index index;
    (void)build_chain(index, 30);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    REQUIRE(organizer.finalized_height() == -1);
    organizer.note_block_validated(29);
    REQUIRE(organizer.finalized_height() == -1);
}

TEST_CASE("header_organizer duplicate headers in same batch", "[header_organizer][stale]") {
    // Setup
    header_index index;
    auto tip_hash = build_chain(index, 5);

    // Create organizer and sync its tip with the index
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();  // Must sync tip after populating index

    // Create headers that connect to tip
    domain::message::header::list headers;
    hash_digest prev = tip_hash;
    for (int i = 0; i < 3; ++i) {
        auto hdr = make_header_with_prev(prev, uint32_t(5 + i));
        prev = kth::domain::chain::hash(hdr);
        headers.push_back(std::move(hdr));
    }

    // Add first time - should succeed
    auto result1 = organizer.add_headers(headers);
    REQUIRE(!result1.error);
    REQUIRE(result1.headers_added == 3);

    // Add same headers again - should be detected as stale
    auto result2 = organizer.add_headers(headers);
    REQUIRE(result2.error == error::stale_chain);
    REQUIRE(result2.headers_added == 0);
}

// =============================================================================
// Fork detection + finalization enforcement
// =============================================================================

namespace {

// A side branch off `fork_prev` (the fork ancestor's hash), with ids from
// `id_base` so hashes never collide with the main chain.
domain::message::header::list make_branch(hash_digest fork_prev, size_t length, uint32_t id_base) {
    domain::message::header::list branch;
    hash_digest prev = fork_prev;
    for (uint32_t i = 0; i < length; ++i) {
        auto hdr = make_header_with_prev(prev, id_base + i);
        prev = kth::domain::chain::hash(hdr);
        branch.push_back(std::move(hdr));
    }
    return branch;
}

// Test settings with finalization forced to fire immediately (no 2h delay), so
// note_block_validated can finalize deterministically without waiting.
settings make_finalizing_settings() {
    auto s = make_test_settings();
    s.finalization_delay_seconds = 0;   // headers eligible the instant they're deep enough
    // max_reorg_depth stays at its default (10).
    return s;
}

} // namespace

TEST_CASE("header_organizer flags a heavier side branch as a reorg candidate", "[header_organizer][fork]") {
    header_index index;
    (void)build_chain(index, 10);                 // tip at height 9
    auto const fork_prev = index.get_hash(5);

    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    // 6 blocks off height 5 -> head at height 11, more work than the height-9 tip.
    auto branch = make_branch(fork_prev, 6, 200);
    auto result = organizer.add_headers(branch);

    REQUIRE(result.reorg_candidate);
    REQUIRE(result.reorg_fork_height == 5);
    REQUIRE(result.error == error::stale_chain);      // tip not switched
    REQUIRE(result.headers_added == 0);
    REQUIRE(organizer.header_height() == 9);
    REQUIRE(index.size() == 16);                       // branch stored (10 + 6)
}

TEST_CASE("header_organizer rejects a new branch forking below the finalized block", "[header_organizer][finalization]") {
    header_index index;
    (void)build_chain(index, 30);                 // tip at height 29, received_time 0
    auto settings = make_finalizing_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    organizer.note_block_validated(29);           // finalize 29 - 10 = 19
    REQUIRE(organizer.finalized_height() == 19);

    auto const size_before = index.size();
    // New branch off height 15 (below the finalized height 19) -> rejected + penalize.
    auto branch = make_branch(index.get_hash(15), 8, 500);
    auto result = organizer.add_headers(branch);

    REQUIRE(result.error == error::finalized_header_violation);
    REQUIRE(result.headers_added == 0);
    REQUIRE(index.size() == size_before);          // nothing stored
}

TEST_CASE("header_organizer allows a branch forking above the finalized block", "[header_organizer][finalization]") {
    header_index index;
    (void)build_chain(index, 30);                 // tip at height 29
    auto settings = make_finalizing_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(29);
    REQUIRE(organizer.finalized_height() == 19);

    // 8 blocks off height 22 (above finalized) -> head at height 30, out-works the tip.
    auto branch = make_branch(index.get_hash(22), 8, 600);
    auto result = organizer.add_headers(branch);

    REQUIRE(result.error != error::finalized_header_violation);
    REQUIRE(result.reorg_candidate);
    REQUIRE(result.reorg_fork_height == 22);
}

TEST_CASE("header_organizer does not penalize re-sent known headers below the finalized block", "[header_organizer][finalization]") {
    header_index index;
    (void)build_chain(index, 30);
    auto settings = make_finalizing_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(29);
    REQUIRE(organizer.finalized_height() == 19);

    // Re-send already-known main-chain headers at heights 15..17 (below finalized).
    // This is a stale duplicate, not a violation — must not be penalized.
    domain::message::header::list known;
    known.push_back(index.get_header(15));
    known.push_back(index.get_header(16));
    known.push_back(index.get_header(17));
    auto result = organizer.add_headers(known);

    REQUIRE(result.error == error::stale_chain);   // benign duplicate, not a violation
    REQUIRE(result.headers_added == 0);
}

TEST_CASE("header_organizer rejects a new header after a known one on a sub-finalized branch", "[header_organizer][finalization]") {
    header_index index;
    (void)build_chain(index, 30);                 // tip 29, finalized -> 19
    auto settings = make_finalizing_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(29);
    REQUIRE(organizer.finalized_height() == 19);

    auto const size_before = index.size();
    // getheaders-overlap: a KNOWN main-chain header at height 15 (below the finalized
    // height 19) followed by a NEW header forking off it. The known front must not let
    // the new sub-finalized header bypass the finalization check.
    domain::message::header::list batch;
    batch.push_back(index.get_header(15));                             // known, below finalized
    batch.push_back(make_header_with_prev(index.get_hash(15), 900));   // new, forks below finalized

    auto result = organizer.add_headers(batch);

    REQUIRE(result.error == error::finalized_header_violation);
    REQUIRE(result.headers_added == 0);
    REQUIRE(index.size() == size_before);          // nothing stored
}
