// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>
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

TEST_CASE("chunk_coordinator claims each chunk once, in order", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 95);

    // 10 blocks per chunk -> 10 chunks, the last one short (91-95).
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 95, cfg);

    REQUIRE(coord.get_progress().total_chunks == 10);

    std::vector<chunk_claim> claims;
    for (uint32_t expected = 0; expected < 10; ++expected) {
        auto const chunk = coord.claim_chunk();
        REQUIRE(chunk);
        REQUIRE(chunk->chunk_id == expected);
        claims.push_back(*chunk);
    }
    CHECK(coord.chunk_range(0) == std::pair<uint32_t, uint32_t>{1, 10});
    CHECK(coord.chunk_range(9) == std::pair<uint32_t, uint32_t>{91, 95});

    // Every chunk is in progress: nothing to claim, and nothing is complete.
    REQUIRE_FALSE(coord.claim_chunk().has_value());
    CHECK(coord.get_progress().chunks_in_progress == 10);
    CHECK_FALSE(coord.is_complete());

    for (uint32_t id = 0; id < 10; ++id) {
        CHECK_FALSE(coord.is_complete());
        coord.chunk_completed(claims[id]);
        CHECK(coord.get_progress().chunks_completed == id + 1);
    }
    REQUIRE(coord.is_complete());
    REQUIRE_FALSE(coord.claim_chunk().has_value());
}

TEST_CASE("chunk_coordinator one slot per chunk - no blocking", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 160);

    // 16 chunks for 160 blocks / 10
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 160, cfg);

    // Should be able to claim all 16 chunks without blocking
    std::vector<chunk_claim> claimed;
    for (int i = 0; i < 20; ++i) {
        auto c = coord.claim_chunk();
        if (c) claimed.push_back(*c);
    }

    REQUIRE(claimed.size() == 16);

    // Complete all
    for (auto const& claim : claimed) {
        coord.chunk_completed(claim);
    }
    REQUIRE(coord.is_complete());
}

TEST_CASE("chunk_coordinator timeout resets stalled slots", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 20);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 1};
    chunk_coordinator coord(index, 1, 20, cfg);

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

TEST_CASE("chunk_coordinator timeout leaves alone a chunk claimed after the check read the clock", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 10);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 15};
    chunk_coordinator coord(index, 1, 10, cfg);

    // The check reads the clock, and a peer claims the chunk before the check
    // reaches it: the claim is newer than the reading.
    auto const read_by_the_check = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto const claim = coord.claim_chunk();
    REQUIRE(claim.has_value());

    coord.check_timeouts(read_by_the_check);

    // The chunk is still the peer's: nobody else is handed it.
    REQUIRE_FALSE(coord.claim_chunk().has_value());
    REQUIRE(coord.get_progress().chunks_in_progress == 1);

    coord.chunk_completed(*claim);
    REQUIRE(coord.is_complete());
}

TEST_CASE("chunk_coordinator counts a chunk once when two peers complete it", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 20);

    // Two chunks, 1-10 and 11-20.
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 0};
    chunk_coordinator coord(index, 1, 20, cfg);

    // A slow peer takes the first chunk and stalls; the chunk is handed to a
    // second peer, and then both finish it.
    auto const slow = coord.claim_chunk();
    REQUIRE(slow);
    REQUIRE(slow->chunk_id == 0u);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    coord.check_timeouts();
    auto const second = coord.claim_chunk();
    REQUIRE(second);
    REQUIRE(second->chunk_id == 0u);

    coord.chunk_completed(*slow);
    coord.chunk_completed(*second);

    // The second chunk was never downloaded.
    CHECK(coord.get_progress().chunks_completed == 1);
    CHECK_FALSE(coord.is_complete());

    auto const last = coord.claim_chunk();
    REQUIRE(last);
    REQUIRE(last->chunk_id == 1u);
    coord.chunk_completed(*last);
    CHECK(coord.get_progress().chunks_completed == 2);
    CHECK(coord.is_complete());
}

TEST_CASE("chunk_coordinator chunk_failed resets slot to FREE", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 20);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 20, cfg);

    auto c0 = coord.claim_chunk();
    auto c1 = coord.claim_chunk();
    REQUIRE(c0.has_value());
    REQUIRE(c1.has_value());
    REQUIRE_FALSE(coord.claim_chunk().has_value());

    // Fail one - the same chunk becomes claimable again
    coord.chunk_failed(*c0);
    auto c2 = coord.claim_chunk();
    REQUIRE(c2.has_value());
    REQUIRE(c2->chunk_id == c0->chunk_id);
    CHECK(c2->number != c0->number);
}

TEST_CASE("chunk_coordinator keeps a chunk completed when a late claimant fails it", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 20);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 0};
    chunk_coordinator coord(index, 1, 20, cfg);

    // The first chunk stalls and is handed to a second peer; the slow peer
    // then delivers it and the second peer fails.
    auto const slow = coord.claim_chunk();
    REQUIRE(slow);
    REQUIRE(slow->chunk_id == 0u);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    coord.check_timeouts();
    auto const second = coord.claim_chunk();
    REQUIRE(second);
    REQUIRE(second->chunk_id == 0u);

    coord.chunk_completed(*slow);
    coord.chunk_failed(*second);

    // The delivered chunk is not offered again: the next claim is the other one.
    CHECK(coord.get_progress().chunks_completed == 1);
    auto const next = coord.claim_chunk();
    REQUIRE(next);
    REQUIRE(next->chunk_id == 1u);
    coord.chunk_completed(*next);
    CHECK(coord.is_complete());
}

TEST_CASE("chunk_coordinator keeps the second peer's claim when the first peer fails late", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 30);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 0};
    chunk_coordinator coord(index, 1, 30, cfg);

    // The first chunk stalls and is handed to a second peer. The slow peer's
    // request then fails, which is what a stalled request usually ends in.
    auto const slow = coord.claim_chunk();
    REQUIRE(slow);
    REQUIRE(slow->chunk_id == 0u);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    coord.check_timeouts();
    auto const second = coord.claim_chunk();
    REQUIRE(second);
    REQUIRE(second->chunk_id == 0u);

    coord.chunk_failed(*slow);

    // The chunk is still the second peer's: a third peer gets the next one
    // instead of downloading the first a third time.
    CHECK(coord.get_progress().chunks_in_progress == 1);
    auto const third = coord.claim_chunk();
    REQUIRE(third);
    REQUIRE(third->chunk_id == 1u);

    coord.chunk_completed(*second);
    CHECK(coord.get_progress().chunks_completed == 1);
}

TEST_CASE("chunk_coordinator does not take a downloaded chunk for a stalled one", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 30);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 0};
    chunk_coordinator coord(index, 1, 30, cfg);

    // Two chunks claimed; the first one's blocks arrive, the second's do not.
    auto const arrived = coord.claim_chunk();
    auto const pending = coord.claim_chunk();
    REQUIRE(arrived);
    REQUIRE(pending);
    coord.chunk_downloaded(*arrived);

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    coord.check_timeouts();

    // Only the request still pending was given back.
    CHECK(coord.get_progress().chunks_in_progress == 1);
    auto const again = coord.claim_chunk();
    REQUIRE(again);
    CHECK(again->chunk_id == pending->chunk_id);

    // The holder of the downloaded chunk can still give it back or deliver it.
    coord.chunk_failed(*arrived);
    auto const retried = coord.claim_chunk();
    REQUIRE(retried);
    CHECK(retried->chunk_id == arrived->chunk_id);
}

TEST_CASE("chunk_coordinator ignores a download report from a claim that was handed on", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 20);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 0};
    chunk_coordinator coord(index, 1, 20, cfg);

    auto const slow = coord.claim_chunk();
    REQUIRE(slow);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    coord.check_timeouts();
    auto const second = coord.claim_chunk();
    REQUIRE(second);
    REQUIRE(second->chunk_id == slow->chunk_id);

    // The slow peer's blocks arrive after all. That says nothing about the
    // second peer's request, which is still pending and can still stall.
    coord.chunk_downloaded(*slow);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    coord.check_timeouts();

    auto const third = coord.claim_chunk();
    REQUIRE(third);
    CHECK(third->chunk_id == slow->chunk_id);
}

TEST_CASE("chunk_coordinator offers a chunk given back below the completed ones", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 50);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 50, cfg);

    std::vector<chunk_claim> claims;
    for (int i = 0; i < 4; ++i) {
        auto const claim = coord.claim_chunk();
        REQUIRE(claim);
        claims.push_back(*claim);
    }

    // 0 and 1 completed, 2 given back, 3 completed: the search for a free
    // chunk starts past the completed prefix and still finds chunk 2 first.
    coord.chunk_completed(claims[0]);
    coord.chunk_completed(claims[1]);
    coord.chunk_completed(claims[3]);
    coord.chunk_failed(claims[2]);

    auto const retry = coord.claim_chunk();
    REQUIRE(retry);
    CHECK(retry->chunk_id == 2u);
    auto const fresh = coord.claim_chunk();
    REQUIRE(fresh);
    CHECK(fresh->chunk_id == 4u);
    REQUIRE_FALSE(coord.claim_chunk().has_value());

    coord.chunk_completed(*retry);
    coord.chunk_completed(*fresh);
    CHECK(coord.is_complete());
    CHECK(coord.get_progress().chunks_in_progress == 0);
}

TEST_CASE("chunk_coordinator counts a stalled chunk its first peer delivers before it is claimed again", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 20);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 0};
    chunk_coordinator coord(index, 1, 20, cfg);

    auto const slow = coord.claim_chunk();
    REQUIRE(slow);
    REQUIRE(slow->chunk_id == 0u);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    coord.check_timeouts();

    // Freed by the timeout, delivered before anyone else took it.
    coord.chunk_completed(*slow);
    CHECK(coord.get_progress().chunks_completed == 1);

    auto const next = coord.claim_chunk();
    REQUIRE(next);
    REQUIRE(next->chunk_id == 1u);
}

TEST_CASE("chunk_coordinator ignores reports for chunks outside the range", "[chunk_coordinator]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 20);

    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
    chunk_coordinator coord(index, 1, 20, cfg);

    coord.chunk_downloaded({.chunk_id = 2, .number = 1});
    coord.chunk_completed({.chunk_id = 2, .number = 1});
    coord.chunk_failed({.chunk_id = 2, .number = 1});
    coord.chunk_completed({.chunk_id = 1000, .number = 1});

    CHECK(coord.get_progress().chunks_completed == 0);
    CHECK_FALSE(coord.is_complete());
}

TEST_CASE("chunk_coordinator hands each chunk to one of many concurrent peers", "[chunk_coordinator]") {
    constexpr uint32_t chunks = 4000;
    constexpr size_t peers = 8;

    header_index index(chunks * 2);
    populate_dummy_headers(index, 1, chunks);

    // One block per chunk; nothing stalls, so no chunk is handed out twice.
    chunk_coordinator_config cfg{.chunk_size = 1, .stall_timeout_secs = 3600};
    chunk_coordinator coord(index, 1, chunks, cfg);

    std::vector<std::atomic<uint32_t>> claims(chunks);
    std::atomic<bool> complete_too_early{false};

    std::vector<std::thread> threads;
    for (size_t i = 0; i < peers; ++i) {
        threads.emplace_back([&] {
            while (auto const chunk = coord.claim_chunk()) {
                claims[chunk->chunk_id].fetch_add(1, std::memory_order_relaxed);
                // This chunk is still in progress, so the range is not done.
                if (coord.is_complete()) {
                    complete_too_early.store(true, std::memory_order_relaxed);
                }
                coord.chunk_completed(*chunk);
            }
        });
    }
    for (auto& t : threads) t.join();

    CHECK_FALSE(complete_too_early.load());
    CHECK(coord.is_complete());
    CHECK(coord.get_progress().chunks_completed == chunks);
    CHECK(coord.get_progress().chunks_in_progress == 0);
    size_t claimed_once = 0;
    for (auto const& count : claims) {
        if (count.load() == 1) ++claimed_once;
    }
    CHECK(claimed_once == chunks);
}

TEST_CASE("chunk_coordinator completes every chunk once while peers fail and stall concurrently", "[chunk_coordinator]") {
    constexpr uint32_t chunks = 3000;
    constexpr size_t peers = 8;

    header_index index(chunks * 2);
    populate_dummy_headers(index, 1, chunks);

    // No stall allowance: the checker frees every pending request it sees, so
    // chunks are handed on while their first peer is still working on them.
    chunk_coordinator_config cfg{.chunk_size = 1, .stall_timeout_secs = 0};
    chunk_coordinator coord(index, 1, chunks, cfg);

    std::vector<std::atomic<uint32_t>> deliveries(chunks);
    std::atomic<bool> complete_with_a_chunk_missing{false};
    std::atomic<bool> checking{true};

    std::thread checker([&] {
        while (checking.load(std::memory_order_relaxed)) {
            coord.check_timeouts();
            std::this_thread::yield();
        }
    });

    std::vector<std::thread> threads;
    for (size_t peer = 0; peer < peers; ++peer) {
        threads.emplace_back([&, peer] {
            uint32_t turn = uint32_t(peer);
            while ( ! coord.is_complete()) {
                auto const claim = coord.claim_chunk();
                if ( ! claim) {
                    std::this_thread::yield();
                    continue;
                }
                // One hold in three is given back, one is delivered straight
                // away, one is marked downloaded first.
                switch (turn++ % 3) {
                    case 0:
                        coord.chunk_failed(*claim);
                        continue;
                    case 1:
                        coord.chunk_downloaded(*claim);
                        break;
                    default:
                        std::this_thread::yield();
                        break;
                }
                deliveries[claim->chunk_id].fetch_add(1, std::memory_order_relaxed);
                coord.chunk_completed(*claim);
            }
        });
    }
    for (auto& t : threads) t.join();
    checking.store(false);
    checker.join();

    // Complete means every chunk was delivered at least once, and the count is
    // exact however many peers delivered the same chunk.
    size_t delivered = 0;
    for (auto const& count : deliveries) {
        if (count.load() >= 1) ++delivered;
    }
    CHECK(delivered == chunks);
    CHECK(coord.is_complete());
    CHECK(coord.get_progress().chunks_completed == chunks);
    CHECK(coord.get_progress().chunks_in_progress == 0);
    CHECK_FALSE(coord.claim_chunk().has_value());
}

// =============================================================================
// Mixed fast and slow peers
// =============================================================================

TEST_CASE("chunk_coordinator fast peers are not held back by slow peers", "[chunk_coordinator][slow_peer]") {
    header_index index(1024);
    populate_dummy_headers(index, 1, 320);

    // 32 chunks: a fast peer always finds one to claim until they run out.
    chunk_coordinator_config cfg{.chunk_size = 10, .stall_timeout_secs = 5};
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

    // Fast peers are only blocked at the very end, waiting for slow peers to
    // finish their last chunks.
    CHECK(fast_blocked < 1000);

    // Fast peers should do most of the work
    CHECK(fast_chunks > slow_chunks);

    // Total time should be close to the slow peer time (they run in parallel)
    CHECK(static_cast<uint64_t>(elapsed_ms) < 2000);

    INFO("Elapsed: " << elapsed_ms << "ms, fast=" << fast_chunks << " slow=" << slow_chunks << " fast_blocked=" << fast_blocked);
}
