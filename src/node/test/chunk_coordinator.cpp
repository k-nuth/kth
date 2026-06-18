// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include <kth/node/sync/chunk_coordinator.hpp>
#include <kth/blockchain/header_index.hpp>

using namespace kth;
using namespace kth::node::sync;
using namespace kth::blockchain;

// =============================================================================
// Helper: Populate header_index with dummy headers
// =============================================================================
static
void populate_dummy_headers(header_index& index, uint32_t start, uint32_t end) {
    for (uint32_t h = start; h <= end; ++h) {
        domain::chain::header hdr;
        hash_digest hash{};
        auto const le = to_little_endian(h);
        std::copy(le.begin(), le.end(), hash.begin());
        [[maybe_unused]] auto result = index.add(hash, hdr);
    }
}

// =============================================================================
// Helper: Simulate a peer downloading chunks
// =============================================================================
struct peer_result {
    size_t chunks_completed{0};
    size_t blocked_count{0};
    uint64_t total_work_ms{0};
    uint64_t total_blocked_ms{0};
};

static
peer_result simulate_peer(
    chunk_coordinator& coord,
    std::chrono::milliseconds download_time,
    std::chrono::milliseconds retry_interval)
{
    peer_result result;
    while (!coord.is_complete() && !coord.is_stopped()) {
        auto chunk = coord.claim_chunk();
        if (!chunk) {
            ++result.blocked_count;
            auto wait_start = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(retry_interval);
            result.total_blocked_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - wait_start).count();
            continue;
        }

        std::this_thread::sleep_for(download_time);
        result.total_work_ms += download_time.count();

        coord.chunk_completed(*chunk);
        ++result.chunks_completed;
    }
    return result;
}

// =============================================================================
// Basic tests
// =============================================================================

TEST_CASE("chunk_coordinator basic claim and complete with rounds", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 100);

    // 4 slots, 10 blocks per chunk -> 10 chunks -> 3 rounds (4+4+2)
    chunk_coordinator_config cfg{.chunk_size = 10, .slots = 4, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 100, cfg);

    // Round 0: claim all 4 slots -> chunk_ids 0,1,2,3
    auto c0 = coord.claim_chunk();
    auto c1 = coord.claim_chunk();
    auto c2 = coord.claim_chunk();
    auto c3 = coord.claim_chunk();

    REQUIRE(c0.has_value());
    REQUIRE(c1.has_value());
    REQUIRE(c2.has_value());
    REQUIRE(c3.has_value());
    CHECK(*c0 == 0);
    CHECK(*c3 == 3);

    // 5th claim should fail (all slots in use)
    REQUIRE_FALSE(coord.claim_chunk().has_value());

    // Complete all 4 -> next claim triggers round advance
    coord.chunk_completed(*c0);
    coord.chunk_completed(*c1);
    coord.chunk_completed(*c2);
    coord.chunk_completed(*c3);

    REQUIRE(coord.get_progress().chunks_completed == 4);

    // Round 1: claim triggers try_advance_round -> chunk_ids 4,5,6,7
    auto c4 = coord.claim_chunk();
    REQUIRE(c4.has_value());
    CHECK(*c4 == 4);
    CHECK(coord.get_progress().current_round == 1);

    auto c5 = coord.claim_chunk();
    auto c6 = coord.claim_chunk();
    auto c7 = coord.claim_chunk();
    REQUIRE(c5.has_value());
    REQUIRE(c6.has_value());
    REQUIRE(c7.has_value());

    coord.chunk_completed(*c4);
    coord.chunk_completed(*c5);
    coord.chunk_completed(*c6);
    coord.chunk_completed(*c7);

    REQUIRE(coord.get_progress().chunks_completed == 8);

    // Round 2: only 2 chunks remaining (8,9)
    auto c8 = coord.claim_chunk();
    auto c9 = coord.claim_chunk();
    REQUIRE(c8.has_value());
    REQUIRE(c9.has_value());
    CHECK(*c8 == 8);
    CHECK(*c9 == 9);

    coord.chunk_completed(*c8);
    coord.chunk_completed(*c9);

    REQUIRE(coord.is_complete());
    REQUIRE(coord.get_progress().chunks_completed == 10);
}

TEST_CASE("chunk_coordinator one slot per chunk - no blocking", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 160);

    // slots=0 means one slot per chunk (16 chunks for 160 blocks / 10)
    chunk_coordinator_config cfg{.chunk_size = 10, .slots = 0, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 160, cfg);

    // Should be able to claim all 16 chunks without blocking
    std::vector<uint32_t> claimed;
    for (int i = 0; i < 20; ++i) {
        auto c = coord.claim_chunk();
        if (c) claimed.push_back(*c);
    }

    REQUIRE(claimed.size() == 16);

    // Complete all
    for (auto id : claimed) {
        coord.chunk_completed(id);
    }
    REQUIRE(coord.is_complete());
}

TEST_CASE("chunk_coordinator timeout resets stalled slots", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 100);

    chunk_coordinator_config cfg{.chunk_size = 10, .slots = 2, .stall_timeout_secs = 1};
    chunk_coordinator coord(index, 1, 100, cfg);

    auto c0 = coord.claim_chunk();
    auto c1 = coord.claim_chunk();
    REQUIRE(c0.has_value());
    REQUIRE(c1.has_value());
    REQUIRE_FALSE(coord.claim_chunk().has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    coord.check_timeouts();

    // Slots should be free now
    auto c2 = coord.claim_chunk();
    REQUIRE(c2.has_value());
}

TEST_CASE("chunk_coordinator chunk_failed resets slot to FREE", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 100);

    chunk_coordinator_config cfg{.chunk_size = 10, .slots = 2, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 100, cfg);

    auto c0 = coord.claim_chunk();
    auto c1 = coord.claim_chunk();
    REQUIRE_FALSE(coord.claim_chunk().has_value());

    // Fail one - should become claimable again (same chunk_id, same round)
    coord.chunk_failed(*c0);
    auto c2 = coord.claim_chunk();
    REQUIRE(c2.has_value());
    REQUIRE(*c2 == *c0);  // Same chunk recycled in same round
}

// =============================================================================
// Roller coaster reproduction: limited slots
// =============================================================================

TEST_CASE("chunk_coordinator limited slots: fast peers blocked by slow peers", "[chunk_coordinator][slow_peer]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 320);

    // 8 slots for 32 chunks -> 4 rounds, fast peers blocked at round boundaries
    chunk_coordinator_config cfg{.chunk_size = 10, .slots = 8, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 320, cfg);

    std::vector<peer_result> results(4);
    auto const start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.emplace_back([&]{ results[0] = simulate_peer(coord, std::chrono::milliseconds(1), std::chrono::milliseconds(1)); });
    threads.emplace_back([&]{ results[1] = simulate_peer(coord, std::chrono::milliseconds(1), std::chrono::milliseconds(1)); });
    threads.emplace_back([&]{ results[2] = simulate_peer(coord, std::chrono::milliseconds(200), std::chrono::milliseconds(1)); });
    threads.emplace_back([&]{ results[3] = simulate_peer(coord, std::chrono::milliseconds(200), std::chrono::milliseconds(1)); });

    for (auto& t : threads) t.join();

    auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    REQUIRE(coord.is_complete());

    size_t fast_blocked = results[0].blocked_count + results[1].blocked_count;
    CHECK(fast_blocked > 0);  // Fast peers WERE blocked (the problem)

    uint64_t ideal_ms = 32;  // 32 chunks * 1ms
    CHECK(static_cast<uint64_t>(elapsed_ms) > ideal_ms * 5);  // Much slower than ideal

    INFO("Elapsed: " << elapsed_ms << "ms, ideal: " << ideal_ms << "ms, fast_blocked: " << fast_blocked);
}

// =============================================================================
// Solution: one slot per chunk eliminates blocking
// =============================================================================

TEST_CASE("chunk_coordinator full slots: fast peers never blocked", "[chunk_coordinator][slow_peer]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 320);

    // slots=0 -> one per chunk (32 slots for 32 chunks). No starvation possible.
    chunk_coordinator_config cfg{.chunk_size = 10, .slots = 0, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 320, cfg);

    std::vector<peer_result> results(4);
    auto const start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.emplace_back([&]{ results[0] = simulate_peer(coord, std::chrono::milliseconds(1), std::chrono::milliseconds(1)); });
    threads.emplace_back([&]{ results[1] = simulate_peer(coord, std::chrono::milliseconds(1), std::chrono::milliseconds(1)); });
    threads.emplace_back([&]{ results[2] = simulate_peer(coord, std::chrono::milliseconds(200), std::chrono::milliseconds(1)); });
    threads.emplace_back([&]{ results[3] = simulate_peer(coord, std::chrono::milliseconds(200), std::chrono::milliseconds(1)); });

    for (auto& t : threads) t.join();

    auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    REQUIRE(coord.is_complete());

    size_t fast_chunks = results[0].chunks_completed + results[1].chunks_completed;
    size_t slow_chunks = results[2].chunks_completed + results[3].chunks_completed;
    size_t fast_blocked = results[0].blocked_count + results[1].blocked_count;

    // With enough slots, fast peers are only blocked at the very end
    // (waiting for slow peers to finish their last chunks), not at every round boundary.
    // With limited slots (8), fast_blocked is typically in the thousands.
    CHECK(fast_blocked < 1000);

    // Fast peers should do most of the work
    CHECK(fast_chunks > slow_chunks);

    // Total time should be close to the slow peer time (they run in parallel)
    // Not 5x worse like with limited slots
    CHECK(static_cast<uint64_t>(elapsed_ms) < 2000);

    INFO("Elapsed: " << elapsed_ms << "ms, fast=" << fast_chunks << " slow=" << slow_chunks << " fast_blocked=" << fast_blocked);
}
