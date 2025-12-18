// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/blockchain.hpp>

using namespace kth;
using namespace kth::blockchain;
using namespace kth::domain::chain;

// =============================================================================
// Helper functions
// =============================================================================

namespace {

// Create a deterministic hash from an integer
hash_digest make_hash(uint32_t id) {
    hash_digest hash{};
    // Fill with the id value for easy identification
    hash[0] = uint8_t(id & 0xFF);
    hash[1] = uint8_t((id >> 8) & 0xFF);
    hash[2] = uint8_t((id >> 16) & 0xFF);
    hash[3] = uint8_t((id >> 24) & 0xFF);
    return hash;
}

// Create a test header with specific values
header make_header(uint32_t version, hash_digest const& prev_hash,
                   hash_digest const& merkle, uint32_t timestamp,
                   uint32_t bits, uint32_t nonce) {
    return header{version, prev_hash, merkle, timestamp, bits, nonce};
}

// Create a simple header chain
header make_header_at(uint32_t height, hash_digest const& prev_hash) {
    return make_header(1, prev_hash, make_hash(height + 1000), height * 600, 0x1d00ffff, height);
}

} // namespace

// =============================================================================
// Construction tests
// =============================================================================

TEST_CASE("header_index default construction", "[header_index]") {
    header_index index;
    REQUIRE(index.size() == 0);
    REQUIRE(index.capacity() == header_index::default_capacity);
    REQUIRE_FALSE(index.at_capacity_warning());
}

TEST_CASE("header_index custom capacity", "[header_index]") {
    header_index index(1000);
    REQUIRE(index.size() == 0);
    REQUIRE(index.capacity() == 1000);
}

// =============================================================================
// Add/Find/Contains tests
// =============================================================================

TEST_CASE("header_index add single header", "[header_index]") {
    header_index index(100);

    auto const prev_hash = null_hash;
    auto const hdr = make_header_at(0, prev_hash);
    auto const hash = hdr.hash();

    auto const result = index.add(hash, hdr);

    REQUIRE(result.inserted == true);
    REQUIRE(result.index == 0);
    REQUIRE(result.capacity_warning == false);
    REQUIRE(index.size() == 1);
}

TEST_CASE("header_index add duplicate returns existing index", "[header_index]") {
    header_index index(100);

    auto const hdr = make_header_at(0, null_hash);
    auto const hash = hdr.hash();

    auto const result1 = index.add(hash, hdr);
    auto const result2 = index.add(hash, hdr);

    REQUIRE(result1.inserted == true);
    REQUIRE(result2.inserted == false);
    REQUIRE(result1.index == result2.index);
    REQUIRE(index.size() == 1);
}

TEST_CASE("header_index find existing header", "[header_index]") {
    header_index index(100);

    auto const hdr = make_header_at(0, null_hash);
    auto const hash = hdr.hash();

    (void)index.add(hash, hdr);
    auto const found_idx = index.find(hash);

    REQUIRE(found_idx != header_index::null_index);
    REQUIRE(found_idx == 0);
}

TEST_CASE("header_index find non-existing header returns null_index", "[header_index]") {
    header_index index(100);

    auto const found_idx = index.find(make_hash(999));
    REQUIRE(found_idx == header_index::null_index);
}

TEST_CASE("header_index contains", "[header_index]") {
    header_index index(100);

    auto const hdr = make_header_at(0, null_hash);
    auto const hash = hdr.hash();

    REQUIRE_FALSE(index.contains(hash));
    (void)index.add(hash, hdr);
    REQUIRE(index.contains(hash));
}

// =============================================================================
// Field accessor tests
// =============================================================================

TEST_CASE("header_index field accessors", "[header_index]") {
    header_index index(100);

    auto const prev_hash = make_hash(42);
    auto const merkle = make_hash(100);
    uint32_t const version = 2;
    uint32_t const timestamp = 1234567890;
    uint32_t const bits = 0x1d00ffff;
    uint32_t const nonce = 12345;

    auto const hdr = make_header(version, prev_hash, merkle, timestamp, bits, nonce);
    auto const hash = hdr.hash();

    auto const result = index.add(hash, hdr);
    auto const idx = result.index;

    REQUIRE(index.get_prev_block_hash(idx) == prev_hash);
    REQUIRE(index.get_version(idx) == version);
    REQUIRE(index.get_merkle(idx) == merkle);
    REQUIRE(index.get_timestamp(idx) == timestamp);
    REQUIRE(index.get_bits(idx) == bits);
    REQUIRE(index.get_nonce(idx) == nonce);
}

TEST_CASE("header_index get_entry returns all fields", "[header_index]") {
    header_index index(100);

    auto const prev_hash = make_hash(42);
    auto const merkle = make_hash(100);
    uint32_t const version = 2;
    uint32_t const timestamp = 1234567890;
    uint32_t const bits = 0x1d00ffff;
    uint32_t const nonce = 12345;

    auto const hdr = make_header(version, prev_hash, merkle, timestamp, bits, nonce);
    auto const hash = hdr.hash();

    auto const result = index.add(hash, hdr);
    auto const entry = index.get_entry(result.index);

    REQUIRE(entry.header.previous_block_hash() == prev_hash);
    REQUIRE(entry.header.version() == version);
    REQUIRE(entry.header.merkle() == merkle);
    REQUIRE(entry.header.timestamp() == timestamp);
    REQUIRE(entry.header.bits() == bits);
    REQUIRE(entry.header.nonce() == nonce);
    REQUIRE(entry.height == 0);  // Genesis has no parent
    REQUIRE(entry.status == header_status::none);
}

// =============================================================================
// Parent/height tracking tests
// =============================================================================

TEST_CASE("header_index parent linkage", "[header_index]") {
    header_index index(100);

    // Add genesis (no parent)
    auto const genesis = make_header_at(0, null_hash);
    auto const genesis_hash = genesis.hash();
    auto const genesis_result = index.add(genesis_hash, genesis);

    REQUIRE(index.get_parent_index(genesis_result.index) == header_index::null_index);
    REQUIRE(index.get_height(genesis_result.index) == 0);

    // Add block 1 (parent = genesis)
    auto const block1 = make_header_at(1, genesis_hash);
    auto const block1_hash = block1.hash();
    auto const block1_result = index.add(block1_hash, block1);

    REQUIRE(index.get_parent_index(block1_result.index) == genesis_result.index);
    REQUIRE(index.get_height(block1_result.index) == 1);

    // Add block 2 (parent = block1)
    auto const block2 = make_header_at(2, block1_hash);
    auto const block2_hash = block2.hash();
    auto const block2_result = index.add(block2_hash, block2);

    REQUIRE(index.get_parent_index(block2_result.index) == block1_result.index);
    REQUIRE(index.get_height(block2_result.index) == 2);
}

// =============================================================================
// Status management tests
// =============================================================================

TEST_CASE("header_index status management", "[header_index]") {
    header_index index(100);

    auto const hdr = make_header_at(0, null_hash);
    auto const hash = hdr.hash();
    auto const result = index.add(hash, hdr);
    auto const idx = result.index;

    // Initial status is none
    REQUIRE(index.get_status(idx) == header_status::none);
    REQUIRE_FALSE(index.has_status(idx, header_status::valid_header));

    // Set status
    index.set_status(idx, header_status::valid_header);
    REQUIRE(index.has_status(idx, header_status::valid_header));
    REQUIRE_FALSE(index.has_status(idx, header_status::have_data));

    // Add status (OR)
    index.add_status(idx, header_status::have_data);
    REQUIRE(index.has_status(idx, header_status::valid_header));
    REQUIRE(index.has_status(idx, header_status::have_data));

    // Check combined flags
    auto const expected = header_status::valid_header | header_status::have_data;
    REQUIRE(index.get_status(idx) == expected);
}

TEST_CASE("header_index status has_flag helper", "[header_index]") {
    auto const status = header_status::valid_header | header_status::have_data;

    REQUIRE(has_flag(status, header_status::valid_header));
    REQUIRE(has_flag(status, header_status::have_data));
    REQUIRE_FALSE(has_flag(status, header_status::have_undo));
    REQUIRE_FALSE(has_flag(status, header_status::failed_valid));
}

// =============================================================================
// Chain work tests
// =============================================================================

TEST_CASE("header_index chain work", "[header_index]") {
    header_index index(100);

    auto const hdr = make_header_at(0, null_hash);
    auto const hash = hdr.hash();
    auto const result = index.add(hash, hdr);
    auto const idx = result.index;

    // Initial chain work (set by add based on parent)
    auto const initial_work = index.get_chain_work(idx);

    // Update chain work
    index.set_chain_work(idx, 12345678);
    REQUIRE(index.get_chain_work(idx) == 12345678);
}

// =============================================================================
// Traversal tests
// =============================================================================

TEST_CASE("header_index traverse_back", "[header_index]") {
    header_index index(100);

    // Build a chain of 5 blocks
    hash_digest prev_hash = null_hash;
    std::vector<header_index::index_t> indices;

    for (uint32_t i = 0; i < 5; ++i) {
        auto const hdr = make_header_at(i, prev_hash);
        auto const hash = hdr.hash();
        auto const result = index.add(hash, hdr);
        indices.push_back(result.index);
        prev_hash = hash;
    }

    // Traverse from tip
    std::vector<header_index::index_t> visited;
    index.traverse_back(indices.back(), 0, [&](header_index::index_t idx) {
        visited.push_back(idx);
    });

    REQUIRE(visited.size() == 5);
    REQUIRE(visited[0] == indices[4]);
    REQUIRE(visited[1] == indices[3]);
    REQUIRE(visited[2] == indices[2]);
    REQUIRE(visited[3] == indices[1]);
    REQUIRE(visited[4] == indices[0]);
}

TEST_CASE("header_index traverse_back with max_steps", "[header_index]") {
    header_index index(100);

    // Build a chain of 5 blocks
    hash_digest prev_hash = null_hash;
    std::vector<header_index::index_t> indices;

    for (uint32_t i = 0; i < 5; ++i) {
        auto const hdr = make_header_at(i, prev_hash);
        auto const hash = hdr.hash();
        auto const result = index.add(hash, hdr);
        indices.push_back(result.index);
        prev_hash = hash;
    }

    // Traverse only 2 steps
    std::vector<header_index::index_t> visited;
    index.traverse_back(indices.back(), 2, [&](header_index::index_t idx) {
        visited.push_back(idx);
    });

    REQUIRE(visited.size() == 2);
    REQUIRE(visited[0] == indices[4]);
    REQUIRE(visited[1] == indices[3]);
}

TEST_CASE("header_index get_ancestor_linear", "[header_index]") {
    header_index index(100);

    // Build a chain of 10 blocks
    hash_digest prev_hash = null_hash;
    std::vector<header_index::index_t> indices;

    for (uint32_t i = 0; i < 10; ++i) {
        auto const hdr = make_header_at(i, prev_hash);
        auto const hash = hdr.hash();
        auto const result = index.add(hash, hdr);
        indices.push_back(result.index);
        prev_hash = hash;
    }

    // Get ancestor at height 5 from tip (height 9)
    auto const ancestor = index.get_ancestor_linear(indices.back(), 5);
    REQUIRE(ancestor == indices[5]);

    // Get ancestor at height 0 (genesis)
    auto const genesis = index.get_ancestor_linear(indices.back(), 0);
    REQUIRE(genesis == indices[0]);

    // Get ancestor at same height
    auto const same = index.get_ancestor_linear(indices[5], 5);
    REQUIRE(same == indices[5]);

    // Get ancestor at higher height (invalid)
    auto const invalid = index.get_ancestor_linear(indices[5], 7);
    REQUIRE(invalid == header_index::null_index);
}

TEST_CASE("header_index get_ancestor with skip pointers", "[header_index]") {
    header_index index(100);

    // Build a longer chain to test skip pointers
    hash_digest prev_hash = null_hash;
    std::vector<header_index::index_t> indices;

    for (uint32_t i = 0; i < 100; ++i) {
        auto const hdr = make_header_at(i, prev_hash);
        auto const hash = hdr.hash();
        auto const result = index.add(hash, hdr);
        indices.push_back(result.index);
        prev_hash = hash;
    }

    // Test various ancestor lookups
    REQUIRE(index.get_ancestor(indices[99], 50) == indices[50]);
    REQUIRE(index.get_ancestor(indices[99], 0) == indices[0]);
    REQUIRE(index.get_ancestor(indices[50], 25) == indices[25]);
    REQUIRE(index.get_ancestor(indices[99], 99) == indices[99]);
}

// =============================================================================
// Fork finding tests
// =============================================================================

TEST_CASE("header_index find_fork same chain", "[header_index]") {
    header_index index(100);

    // Build a chain of 10 blocks
    hash_digest prev_hash = null_hash;
    std::vector<header_index::index_t> indices;

    for (uint32_t i = 0; i < 10; ++i) {
        auto const hdr = make_header_at(i, prev_hash);
        auto const hash = hdr.hash();
        auto const result = index.add(hash, hdr);
        indices.push_back(result.index);
        prev_hash = hash;
    }

    // Fork of block 5 and block 9 should be block 5
    auto const fork = index.find_fork(indices[5], indices[9]);
    REQUIRE(fork == indices[5]);

    // Fork of same block is itself
    auto const same = index.find_fork(indices[5], indices[5]);
    REQUIRE(same == indices[5]);
}

TEST_CASE("header_index find_fork different branches", "[header_index]") {
    header_index index(100);

    // Build main chain: 0 -> 1 -> 2 -> 3 -> 4
    hash_digest prev_hash = null_hash;
    std::vector<header_index::index_t> main_chain;

    for (uint32_t i = 0; i < 5; ++i) {
        auto const hdr = make_header_at(i, prev_hash);
        auto const hash = hdr.hash();
        auto const result = index.add(hash, hdr);
        main_chain.push_back(result.index);
        prev_hash = hash;
    }

    // Build fork from block 2: 2 -> 3' -> 4'
    prev_hash = index.get_prev_block_hash(main_chain[3]);  // Get hash of block 2
    // Actually we need the hash of block 2, not prev_block_hash of block 3
    // Let me fix this - we need to compute the hash

    // For simplicity, let's create a fork by using a different merkle root
    auto const fork_base_hash = make_header_at(2, index.get_prev_block_hash(main_chain[2])).hash();

    // Build fork: use block 2's hash as parent for fork
    // We need to get hash of block 2 from the main chain
    // Since we don't store the hash, let's rebuild it
    auto hdr2 = make_header_at(2, index.get_prev_block_hash(main_chain[2]));
    auto hash2 = hdr2.hash();

    // Create fork block 3' with different merkle
    auto fork3 = make_header(1, hash2, make_hash(9999), 3 * 600, 0x1d00ffff, 999);
    auto fork3_hash = fork3.hash();
    auto fork3_result = index.add(fork3_hash, fork3);

    // Create fork block 4'
    auto fork4 = make_header(1, fork3_hash, make_hash(9998), 4 * 600, 0x1d00ffff, 998);
    auto fork4_hash = fork4.hash();
    auto fork4_result = index.add(fork4_hash, fork4);

    // Find fork point between main_chain[4] and fork4
    auto const fork_point = index.find_fork(main_chain[4], fork4_result.index);
    REQUIRE(fork_point == main_chain[2]);
}

// =============================================================================
// Capacity tests
// =============================================================================

TEST_CASE("header_index capacity warning", "[header_index]") {
    header_index index(100);  // 95% threshold = 95

    // Add 95 headers (indices 0-94), just below threshold
    hash_digest prev_hash = null_hash;
    for (uint32_t i = 0; i < 95; ++i) {
        auto const hdr = make_header_at(i, prev_hash);
        auto const hash = hdr.hash();
        auto const result = index.add(hash, hdr);
        prev_hash = hash;
        REQUIRE_FALSE(result.capacity_warning);
        REQUIRE_FALSE(index.at_capacity_warning());
    }

    // Add 96th header (index 95) - triggers warning
    auto const hdr = make_header_at(95, prev_hash);
    auto const result = index.add(hdr.hash(), hdr);
    REQUIRE(result.capacity_warning);
    REQUIRE(index.at_capacity_warning());
}

TEST_CASE("header_index memory_usage", "[header_index]") {
    header_index index(1000);

    REQUIRE(index.memory_usage() > 0);

    // Add some headers
    hash_digest prev_hash = null_hash;
    for (uint32_t i = 0; i < 10; ++i) {
        auto const hdr = make_header_at(i, prev_hash);
        auto const hash = hdr.hash();
        (void)index.add(hash, hdr);
        prev_hash = hash;
    }

    // Memory usage should still be based on capacity, not size
    REQUIRE(index.memory_usage() > 0);
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("header_index null_index traversal", "[header_index]") {
    header_index index(100);

    std::vector<header_index::index_t> visited;
    index.traverse_back(header_index::null_index, 0, [&](header_index::index_t idx) {
        visited.push_back(idx);
    });

    REQUIRE(visited.empty());
}

TEST_CASE("header_index get_ancestor from null_index", "[header_index]") {
    header_index index(100);

    auto const result = index.get_ancestor(header_index::null_index, 0);
    REQUIRE(result == header_index::null_index);
}

TEST_CASE("header_index orphan header (parent not found)", "[header_index]") {
    header_index index(100);

    // Add a header whose parent doesn't exist
    auto const orphan = make_header_at(5, make_hash(999));
    auto const orphan_hash = orphan.hash();
    auto const result = index.add(orphan_hash, orphan);

    REQUIRE(result.inserted == true);
    REQUIRE(index.get_parent_index(result.index) == header_index::null_index);
    REQUIRE(index.get_height(result.index) == 0);  // Height is 0 because no parent found
}
