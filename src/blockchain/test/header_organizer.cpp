// Copyright (c) 2016-2025 Knuth Project developers.
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
        auto hash = hdr.hash();

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
        prev = hdr.hash();
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
        prev = hdr.hash();
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
        prev = hdr.hash();
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
