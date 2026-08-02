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

// =============================================================================
// Fork detection tests (headers-first reorg, detection layer)
// =============================================================================

// Build a side branch off `fork_prev` (the fork ancestor's hash) using header ids
// starting at `id_base` so the hashes never collide with the main chain.
domain::message::header::list make_branch(hash_digest fork_prev, size_t length, uint32_t id_base) {
    domain::message::header::list branch;
    hash_digest prev = fork_prev;
    for (size_t i = 0; i < length; ++i) {
        auto hdr = make_header_with_prev(prev, id_base + uint32_t(i));
        prev = kth::domain::chain::hash(hdr);
        branch.push_back(std::move(hdr));
    }
    return branch;
}

TEST_CASE("header_organizer flags a heavier side branch as a reorg candidate", "[header_organizer][fork]") {
    // Main chain: heights 0..9 (tip at 9). All headers share the same target, so
    // cumulative work is proportional to height.
    header_index index;
    (void)build_chain(index, 10);
    auto const fork_prev = index.get_hash(5);   // fork ancestor at height 5

    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    auto const tip_height_before = organizer.header_height();

    // Branch of 6 blocks off height 5 -> branch head at height 11 with more total
    // work than the height-9 tip (6 branch blocks vs 4 tip blocks above the fork).
    auto branch = make_branch(fork_prev, 6, 200);
    auto result = organizer.add_headers(branch);

    // Detected, but the active tip is NOT switched (execution layer pending).
    REQUIRE(result.reorg_candidate);
    REQUIRE(result.reorg_fork_height == 5);
    REQUIRE(result.error == error::stale_chain);
    REQUIRE(result.headers_added == 0);
    REQUIRE(organizer.header_height() == tip_height_before);   // tip unchanged
    REQUIRE(index.size() == 16);                               // branch stored (10 + 6)
}

TEST_CASE("header_organizer does not flag a lighter side branch", "[header_organizer][fork]") {
    header_index index;
    (void)build_chain(index, 10);              // tip at height 9
    auto const fork_prev = index.get_hash(5);

    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    // Branch of 3 blocks off height 5 -> head at height 8, less work than the tip.
    auto branch = make_branch(fork_prev, 3, 300);
    auto result = organizer.add_headers(branch);

    REQUIRE_FALSE(result.reorg_candidate);
    REQUIRE(result.reorg_fork_height == -1);
    REQUIRE(result.error == error::stale_chain);   // still "keep syncing"
    REQUIRE(result.headers_added == 0);
}

TEST_CASE("header_organizer ignores a fork deeper than the reorg limit", "[header_organizer][fork]") {
    header_index index;
    (void)build_chain(index, 10);              // tip at height 9
    auto const fork_prev = index.get_hash(5);  // depth from tip = 4

    auto settings = make_test_settings();
    settings.reorganization_limit = 2;         // fork depth 4 > 2 -> ignore
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    auto branch = make_branch(fork_prev, 6, 400);
    auto result = organizer.add_headers(branch);

    REQUIRE(result.error == error::stale_chain);
    REQUIRE_FALSE(result.reorg_candidate);
    REQUIRE(index.size() == 10);               // branch NOT stored (bounded growth)
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
