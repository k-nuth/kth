// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/block_tasks.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <spdlog/spdlog.h>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/network/protocols_coro.hpp>

namespace kth::node::sync {

using namespace ::asio::experimental::awaitable_operators;

// =============================================================================
// Block Download Task (per-peer)
// =============================================================================

::asio::awaitable<void> block_download_task(
    network::peer_session::ptr peer,
    chunk_coordinator& coordinator,
    std::atomic<uint32_t>& active_peers,
    block_download_channel& output
) {
    auto const addr = peer->authority_with_agent();
    size_t chunks_downloaded = 0;

    // Timing stats accumulation
    std::vector<uint64_t> download_times_ms;
    std::vector<uint64_t> send_times_ms;
    download_times_ms.reserve(100);
    send_times_ms.reserve(100);

    // Track active peers
    auto const peer_count = active_peers.fetch_add(1, std::memory_order_relaxed) + 1;
    spdlog::debug("[block_download] Task started for peer {} (active peers: {})",
        addr, peer_count);

    // RAII guard to decrement on exit and log stats
    struct peer_guard {
        std::atomic<uint32_t>& counter;
        std::string const& addr;
        size_t const& chunks;
        std::vector<uint64_t> const& download_times;
        std::vector<uint64_t> const& send_times;
        ~peer_guard() {
            auto const remaining = counter.fetch_sub(1, std::memory_order_relaxed) - 1;

            // Print download timing stats if we have data
            if (!download_times.empty()) {
                auto sorted_dl = download_times;
                std::sort(sorted_dl.begin(), sorted_dl.end());
                size_t n = sorted_dl.size();
                auto min_ms = sorted_dl.front();
                auto max_ms = sorted_dl.back();
                auto median_ms = sorted_dl[n / 2];
                uint64_t sum = 0;
                for (auto t : sorted_dl) sum += t;
                double avg_ms = static_cast<double>(sum) / n;

                spdlog::info("[download] Peer {} stats (n={}): "
                    "download min={}ms avg={:.0f}ms median={}ms max={}ms",
                    addr, n, min_ms, avg_ms, median_ms, max_ms);
            }

            // Print send timing stats if we have significant delays
            if (!send_times.empty()) {
                auto sorted_send = send_times;
                std::sort(sorted_send.begin(), sorted_send.end());
                size_t n = sorted_send.size();
                auto max_ms = sorted_send.back();
                if (max_ms > 50) {  // Only log if any send took >50ms
                    auto median_ms = sorted_send[n / 2];
                    uint64_t sum = 0;
                    for (auto t : sorted_send) sum += t;
                    double avg_ms = static_cast<double>(sum) / n;
                    spdlog::info("[download] Peer {} channel send: avg={:.0f}ms median={}ms max={}ms",
                        addr, avg_ms, median_ms, max_ms);
                }
            }

            spdlog::debug("[block_download] Task ended for peer {} (downloaded {} chunks, active peers: {})",
                addr, chunks, remaining);
        }
    } guard{active_peers, addr, chunks_downloaded, download_times_ms, send_times_ms};

    // Track current chunk for cleanup on exception
    std::optional<uint32_t> current_chunk_id;

    try {

    auto executor = co_await ::asio::this_coro::executor;

    while (!peer->stopped() && !coordinator.is_stopped()) {
        // Claim chunk via coordinator - lock-free CAS
        auto maybe_chunk = coordinator.claim_chunk();
        if (!maybe_chunk) {
            // No more chunks or all slots busy - wait and retry
            if (coordinator.is_complete() || coordinator.is_stopped()) {
                spdlog::debug("[block_download] Peer {} - sync complete or stopped", addr);
                break;
            }
            // Slots busy - wait with backoff before retry (avoid busy-wait)
            ::asio::steady_timer timer(executor);
            timer.expires_after(std::chrono::milliseconds(100));
            co_await timer.async_wait(::asio::use_awaitable);
            continue;
        }

        uint32_t chunk_id = *maybe_chunk;
        current_chunk_id = chunk_id;  // Track for exception cleanup
        auto [chunk_start, chunk_end] = coordinator.chunk_range(chunk_id);

        spdlog::debug("[block_download] Peer {} claiming chunk {} (blocks {}-{})",
            addr, chunk_id, chunk_start, chunk_end);

        // Build request with hashes from coordinator
        std::vector<std::pair<uint32_t, hash_digest>> blocks;
        blocks.reserve(chunk_end - chunk_start + 1);

        for (uint32_t h = chunk_start; h <= chunk_end; ++h) {
            auto hash = coordinator.get_block_hash(h);
            if (hash == null_hash) {
                spdlog::error("[block_download] No hash for height {}", h);
                continue;
            }
            blocks.emplace_back(h, hash);
        }

        if (blocks.empty()) {
            coordinator.chunk_failed(chunk_id);
            continue;
        }

        // Download from peer - measure time
        auto download_start = std::chrono::steady_clock::now();
        auto result = co_await network::request_blocks_batch(
            *peer, blocks, std::chrono::seconds(60));
        auto download_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - download_start).count();

        if (!result) {
            spdlog::debug("[block_download] Peer {} failed to download chunk {}: {}",
                addr, chunk_id, result.error().message());
            // Report failure - slot reset to FREE for retry by another peer
            coordinator.chunk_failed(chunk_id);
            break;
        }

        spdlog::debug("[block_download] Peer {} downloaded {} blocks for chunk {} in {}ms",
            addr, result->size(), chunk_id, download_ms);
        download_times_ms.push_back(download_ms);

        // Send each block to output channel - measure time
        auto send_start = std::chrono::steady_clock::now();
        auto const current_peers = active_peers.load(std::memory_order_relaxed);
        for (auto& blk : *result) {
            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                downloaded_block{
                    .height = blk.height,
                    .block = std::make_shared<domain::message::block const>(std::move(blk.block)),
                    .source_peer = peer,
                    .active_peers = current_peers
                },
                ::asio::as_tuple(::asio::use_awaitable)
            );
            if (send_ec) {
                spdlog::warn("[block_download] Failed to send block {} to channel: {}",
                    blk.height, send_ec.message());
                // Channel closed - abort
                coordinator.chunk_failed(chunk_id);
                co_return;
            }
        }
        auto send_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - send_start).count();
        send_times_ms.push_back(send_ms);

        // Report success only after all blocks sent
        coordinator.chunk_completed(chunk_id);
        current_chunk_id.reset();  // Clear - chunk successfully handed off
        ++chunks_downloaded;
    }

    } catch (::asio::system_error const& e) {
        // Asio system errors (e.g., operation_aborted during shutdown) are expected
        spdlog::debug("[block_download] Peer {} asio error: {} ({})", addr, e.what(), e.code().value());
        if (current_chunk_id) {
            coordinator.chunk_failed(*current_chunk_id);
        }
    } catch (std::exception const& e) {
        spdlog::error("[block_download] Peer {} exception: {}", addr, e.what());
        if (current_chunk_id) {
            coordinator.chunk_failed(*current_chunk_id);
        }
    } catch (...) {
        spdlog::error("[block_download] Peer {} unknown exception", addr);
        if (current_chunk_id) {
            coordinator.chunk_failed(*current_chunk_id);
        }
    }

    spdlog::debug("[block_download] Peer {} task exiting cleanly", addr);
}

// =============================================================================
// Block Download Supervisor
// =============================================================================

::asio::awaitable<void> block_download_supervisor(
    peer_channel& peers,
    block_request_channel& requests,
    block_download_channel& output,
    stop_channel& stop,
    blockchain::header_organizer& organizer
) {
    auto executor = co_await ::asio::this_coro::executor;

    spdlog::debug("[block_supervisor] Task started");

    task_group download_tasks(executor);

    // Coordinator - created when we get a range
    std::unique_ptr<chunk_coordinator> coordinator;
    std::atomic<uint32_t> active_peers{0};

    // Track peers that already have running download tasks (by address string)
    boost::unordered_flat_set<std::string> spawned_peers;

    // Buffer peers that arrive before we have a range
    std::vector<network::peer_session::ptr> pending_peers;

    // Timeout check timer (checks for stalled chunks every 10 seconds)
    ::asio::steady_timer timeout_timer(executor);
    auto reset_timer = [&]() {
        timeout_timer.expires_after(std::chrono::seconds(10));
    };
    reset_timer();

    // Helper to spawn download task for a peer (returns true if spawned)
    auto spawn_download = [&](network::peer_session::ptr peer) -> bool {
        if (peer->stopped() || !coordinator) return false;

        auto addr_key = peer->authority().to_string();
        if (spawned_peers.contains(addr_key)) {
            return false;  // Already has a running task
        }

        spdlog::debug("[block_supervisor] Spawning download task for peer {}", peer->authority_with_agent());
        spawned_peers.insert(addr_key);
        download_tasks.spawn(block_download_task(
            peer,
            *coordinator,
            active_peers,
            output
        ));
        return true;
    };

    while (true) {
        // Stop signal first to prioritize shutdown
        auto event = co_await (
            stop.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            peers.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            requests.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            timeout_timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
        );

        // Timeout timer fired (index 3)
        if (event.index() == 3) {
            // Check if we should stop before processing timeout
            if (coordinator && !coordinator->is_stopped()) {
                coordinator->check_timeouts();
            }
            reset_timer();
            continue;
        }

        if (event.index() == 0) {
            spdlog::debug("[block_supervisor] Stop signal received");
            if (coordinator) {
                coordinator->stop();
            }
            break;
        }

        if (event.index() == 2) {
            // New block range request
            auto [ec, request] = std::get<2>(event);
            if (ec) {
                spdlog::debug("[block_supervisor] Requests channel closed");
                break;
            }

            spdlog::info("[block_supervisor] New range request: {} to {} ({} blocks)",
                request.start_height, request.end_height,
                request.end_height - request.start_height + 1);

            // Create new coordinator for this range
            coordinator = std::make_unique<chunk_coordinator>(
                organizer.index(),
                request.start_height,
                request.end_height
            );

            // Spawn tasks for any pending peers
            if (!pending_peers.empty()) {
                spdlog::debug("[block_supervisor] Spawning tasks for {} buffered peers",
                    pending_peers.size());
                for (auto& p : pending_peers) {
                    spawn_download(p);
                }
                pending_peers.clear();
            }
            continue;
        }

        // Peers list updated (index 1)
        auto [ec, peers_msg] = std::get<1>(event);
        if (ec) {
            spdlog::debug("[block_supervisor] Peers channel closed");
            break;
        }

        if (!coordinator) {
            // Buffer peers until we have a range request
            pending_peers = peers_msg.peers;
            spdlog::debug("[block_supervisor] Buffered {} peers (waiting for range)",
                pending_peers.size());
            continue;
        }

        // Spawn tasks for any new peers in the list
        size_t spawned = 0;
        for (auto const& peer : peers_msg.peers) {
            if (spawn_download(peer)) {
                ++spawned;
            }
        }
        if (spawned > 0) {
            spdlog::debug("[block_supervisor] Spawned {} new download tasks", spawned);
        }
    }

    // Cancel timer to prevent dangling operations
    spdlog::debug("[block_supervisor] Cancelling timeout timer...");
    timeout_timer.cancel();

    // IMPORTANT: Stop coordinator FIRST to signal all tasks to exit
    if (coordinator) {
        spdlog::debug("[block_supervisor] Stopping coordinator...");
        coordinator->stop();
    }

    // Close output channel BEFORE waiting - this unblocks any download tasks
    // that are stuck in async_send() waiting for validation to consume
    spdlog::debug("[block_supervisor] Closing output channel...");
    output.close();

    // NOW wait for all download tasks to finish
    spdlog::debug("[block_supervisor] Waiting for {} download tasks to complete...",
        download_tasks.active_count());
    co_await download_tasks.join();
    spdlog::debug("[block_supervisor] All download tasks completed");

    // NOW it's safe to destroy the coordinator
    spdlog::debug("[block_supervisor] Destroying coordinator...");
    coordinator.reset();

    spdlog::debug("[block_supervisor] Task ended cleanly");
}

// =============================================================================
// Block Validation Task
// =============================================================================

::asio::awaitable<void> block_validation_task(
    blockchain::block_chain& chain,
    block_download_channel& input,
    block_validated_channel& output,
    stop_channel& stop,
    uint32_t start_height,
    uint32_t checkpoint_height
) {
    spdlog::debug("[block_validation] Task started at height {}, checkpoint at {}",
        start_height, checkpoint_height);

    // OWNED state - not shared with anyone
    uint32_t next_height = start_height;
    uint32_t last_seen_peers = 0;
    boost::unordered_flat_map<uint32_t, downloaded_block> pending;

    size_t validated_count = 0;
    auto const start_time = std::chrono::steady_clock::now();
    uint32_t last_logged_thousand = (start_height > 0 ? start_height - 1 : 0) / 1000;

    // Timing metrics for performance analysis (accumulate per 1000-block window)
    std::vector<uint64_t> organize_times_us;  // microseconds for precision
    organize_times_us.reserve(1000);

    // Fast mode timing (merkle + push only)
    std::vector<int64_t> fast_merkle_times, fast_push_times;
    fast_merkle_times.reserve(1000);
    fast_push_times.reserve(1000);

    // Per-phase timing from validation_t (microseconds) - full validation only
    std::vector<int64_t> deser_times, check_times, pop_times, accept_times;
    std::vector<int64_t> connect_times, notify_times, push_times;
    auto reserve_phase_vecs = [&]() {
        deser_times.reserve(1000);
        check_times.reserve(1000);
        pop_times.reserve(1000);
        accept_times.reserve(1000);
        connect_times.reserve(1000);
        notify_times.reserve(1000);
        push_times.reserve(1000);
    };
    reserve_phase_vecs();

    while (true) {
        // Stop signal first in || to prioritize shutdown over buffered data
        auto event = co_await (
            stop.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            input.async_receive(::asio::as_tuple(::asio::use_awaitable))
        );

        if (event.index() == 0) {
            spdlog::debug("[block_validation] Stop signal received");
            break;
        }

        auto [ec, downloaded] = std::get<1>(event);
        if (ec) {
            spdlog::debug("[block_validation] Input channel closed");
            break;
        }

        // Track latest peer count for display
        last_seen_peers = downloaded.active_peers;

        // Always add to pending first (simplifies logic) - store full struct for source_peer tracking
        pending[downloaded.height] = downloaded;

        // Log periodically when buffering a lot
        if (pending.size() % 1000 == 0 && !pending.contains(next_height)) {
            spdlog::debug("[sync] Buffered {} blocks (waiting for height {}, received {})",
                pending.size(), next_height, downloaded.height);
        }

        // Check if we can process the next expected block (either just received or already buffered)
        if (!pending.contains(next_height)) {
            continue;  // Gap not filled yet, wait for more blocks
        }

        // Log when we start validating (especially first block)
        if (validated_count == 0) {
            spdlog::info("[sync] Starting block validation at height {}", next_height);
        }

        // Flush consecutive pending blocks in batches
        // Process up to 100 at a time, then yield to allow other coroutines to run
        constexpr size_t batch_limit = 100;
        size_t batch_count = 0;
        code result;

        while (batch_count < batch_limit) {
            auto it = pending.find(next_height);
            if (it == pending.end()) break;

            // Measure organize time
            auto organize_start = std::chrono::steady_clock::now();

            // Use fast mode under checkpoint, full validation above
            if (next_height <= checkpoint_height) {
                result = co_await chain.organize_fast(it->second.block, next_height);
            } else {
                result = co_await chain.organize(it->second.block, /*headers_pre_validated=*/true);
            }

            auto organize_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - organize_start).count();
            organize_times_us.push_back(organize_us);

            // Log slow blocks (>100ms) for diagnosis
            if (organize_us > 100000) {  // 100ms
                auto const& txs = it->second.block->transactions();
                size_t tx_count = txs.size();
                size_t total_bytes = it->second.block->serialized_size(true);
                spdlog::warn("[block_validation] Slow block at height {}: {}ms, {} txs, {} bytes",
                    next_height, organize_us / 1000, tx_count, total_bytes);
            }

            // Collect per-phase timing from validation_t
            auto const& v = it->second.block->validation;
            auto to_us = [](auto start, auto end) {
                return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            };

            if (next_height <= checkpoint_height) {
                // Fast mode: merkle + push only
                fast_merkle_times.push_back(to_us(v.start_check, v.start_push));
                fast_push_times.push_back(to_us(v.start_push, v.end_push));
            } else {
                // Full validation mode
                deser_times.push_back(to_us(v.start_deserialize, v.end_deserialize));
                check_times.push_back(to_us(v.start_check, v.start_populate));
                pop_times.push_back(to_us(v.start_populate, v.start_accept));
                accept_times.push_back(to_us(v.start_accept, v.start_connect));
                connect_times.push_back(to_us(v.start_connect, v.start_notify));
                notify_times.push_back(to_us(v.start_notify, v.start_push));
                push_times.push_back(to_us(v.start_push, v.end_push));
            }

            output.try_send(std::error_code{}, block_validated{
                .height = next_height,
                .result = result,
                .source_peer = it->second.source_peer
            });

            pending.erase(it);
            ++next_height;
            ++validated_count;
            ++batch_count;

            if (result) {
                spdlog::error("[block_validation] Failed at height {}: {}",
                    next_height - 1, result.message());
                break;
            }
        }

        // Log progress at round 1000 boundaries (e.g., 228000, 229000)
        uint32_t current_thousand = (next_height - 1) / 1000;
        if (current_thousand > last_logged_thousand) {
            last_logged_thousand = current_thousand;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            double rate = elapsed > 0 ? static_cast<double>(validated_count) / elapsed : 0;

            // Calculate ETA
            auto const current_height = current_thousand * 1000;
            auto const remaining = checkpoint_height > current_height
                ? checkpoint_height - current_height : 0;
            auto const eta_secs = rate > 0 ? static_cast<uint64_t>(remaining / rate) : 0;
            auto const eta_mins = eta_secs / 60;

            // Show different label for fast mode vs full mode
            if (current_height <= checkpoint_height) {
                spdlog::info("[block_sync:fast] {}/{} ({} blk/s, ETA: {}m) | {} peers | pending: {}",
                    current_height, checkpoint_height, static_cast<int>(rate), eta_mins,
                    last_seen_peers, pending.size());
            } else {
                // TODO: For full validation mode, target is headers_synced_to not checkpoint
                spdlog::info("[block_sync:full] {} ({} blk/s) | {} peers | pending: {}",
                    current_height, static_cast<int>(rate),
                    last_seen_peers, pending.size());
            }

            // Print validation timing stats if we have data
            if (!organize_times_us.empty()) {
                // Sort for percentiles
                std::sort(organize_times_us.begin(), organize_times_us.end());
                size_t n = organize_times_us.size();

                // Min, max
                auto min_us = organize_times_us.front();
                auto max_us = organize_times_us.back();

                // Median (p50)
                auto median_us = organize_times_us[n / 2];

                // p95, p99
                auto p95_us = organize_times_us[static_cast<size_t>(n * 0.95)];
                auto p99_us = organize_times_us[static_cast<size_t>(n * 0.99)];

                // Average
                uint64_t sum = 0;
                for (auto t : organize_times_us) sum += t;
                double avg_us = static_cast<double>(sum) / n;

                // Standard deviation
                double variance = 0;
                for (auto t : organize_times_us) {
                    double diff = t - avg_us;
                    variance += diff * diff;
                }
                double stddev_us = std::sqrt(variance / n);

                spdlog::info("[validation] organize() stats (n={}): "
                    "min={:.1f}ms avg={:.1f}ms median={:.1f}ms p95={:.1f}ms p99={:.1f}ms max={:.1f}ms stddev={:.1f}ms",
                    n,
                    min_us / 1000.0, avg_us / 1000.0, median_us / 1000.0,
                    p95_us / 1000.0, p99_us / 1000.0, max_us / 1000.0, stddev_us / 1000.0);

                auto avg_vec = [](std::vector<int64_t> const& v) -> double {
                    if (v.empty()) return 0.0;
                    int64_t sum = 0;
                    for (auto t : v) sum += t;
                    return static_cast<double>(sum) / v.size() / 1000.0;  // to ms
                };

                // Per-phase average times from validation_t
                if (!fast_merkle_times.empty()) {
                    // Fast mode: show merkle + push breakdown
                    spdlog::info("[validation] fast mode phases avg: merkle={:.3f}ms push={:.3f}ms",
                        avg_vec(fast_merkle_times), avg_vec(fast_push_times));
                } else if (!deser_times.empty()) {
                    // Full validation mode
                    spdlog::info("[validation] phases avg: "
                        "deser={:.2f}ms check={:.2f}ms populate={:.2f}ms accept={:.2f}ms "
                        "connect={:.2f}ms notify={:.2f}ms push={:.2f}ms",
                        avg_vec(deser_times), avg_vec(check_times), avg_vec(pop_times),
                        avg_vec(accept_times), avg_vec(connect_times), avg_vec(notify_times),
                        avg_vec(push_times));
                }

                // Reset for next window
                organize_times_us.clear();
                fast_merkle_times.clear();
                fast_push_times.clear();
                deser_times.clear();
                check_times.clear();
                pop_times.clear();
                accept_times.clear();
                connect_times.clear();
                notify_times.clear();
                push_times.clear();
            }
        }
    }

    // Close output channel to signal coordinator
    output.close();
    spdlog::debug("[block_validation] Task ended, validated {} blocks", validated_count);
}

} // namespace kth::node::sync
