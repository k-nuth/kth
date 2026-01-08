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
// Pipeline Counters for debugging block loss
// =============================================================================
// These atomic counters track blocks through the pipeline to identify where
// blocks are being lost.

// Global counters - accessible from other files for bridge tracking
std::atomic<uint64_t> g_blocks_sent_by_tasks{0};       // Sent from download tasks to task_output
std::atomic<uint64_t> g_blocks_received_by_supervisor{0}; // Received by supervisor from task_output
std::atomic<uint64_t> g_blocks_forwarded_by_supervisor{0}; // Forwarded by supervisor to downloaded_blocks
std::atomic<uint64_t> g_blocks_received_by_bridge{0}; // Received by bridge from downloaded_blocks
std::atomic<uint64_t> g_blocks_forwarded_by_bridge{0}; // Forwarded by bridge to validation_input
std::atomic<uint64_t> g_blocks_received_by_validation{0}; // Received by validation task

// =============================================================================
// Block Download Task (per-peer)
// =============================================================================

::asio::awaitable<void> block_download_task(
    network::peer_session::ptr peer,
    chunk_coordinator& coordinator,
    std::atomic<uint32_t>& active_peers,
    block_download_task_output_channel& output
) {
    auto const addr = peer->authority_with_agent();
    auto const peer_nonce = peer->nonce();
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

    // RAII guard for counter decrement and stats logging
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
            timer.expires_after(std::chrono::milliseconds(20));
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

        // Validate we got all expected blocks
        auto const expected_count = chunk_end - chunk_start + 1;
        if (result->size() != expected_count) {
            spdlog::warn("[block_download] Peer {} returned {} blocks for chunk {} (expected {}), failing chunk",
                addr, result->size(), chunk_id, expected_count);
            coordinator.chunk_failed(chunk_id);
            continue;  // Try another chunk instead of disconnecting
        }

        spdlog::debug("[block_download] Peer {} downloaded {} blocks for chunk {} in {}ms",
            addr, result->size(), chunk_id, download_ms);
        download_times_ms.push_back(download_ms);

        // Send each block to output channel - measure time
        auto send_start = std::chrono::steady_clock::now();
        auto const current_peers = active_peers.load(std::memory_order_relaxed);
        for (auto& blk : *result) {
            auto const height = blk.height;
            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                downloaded_block{
                    .height = height,
                    .block = std::make_shared<domain::message::block const>(std::move(blk.block)),
                    .source_peer = peer,
                    .active_peers = current_peers
                },
                ::asio::as_tuple(::asio::use_awaitable)
            );
            if (send_ec) {
                spdlog::warn("[block_download] Failed to send block {} to channel: {}",
                    height, send_ec.message());
                // Channel closed - abort
                coordinator.chunk_failed(chunk_id);
                co_return;
            }
            // Track successful sends for pipeline debugging
            g_blocks_sent_by_tasks.fetch_add(1, std::memory_order_relaxed);
        }
        auto send_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - send_start).count();
        send_times_ms.push_back(send_ms);

        // Report success only after all blocks sent
        coordinator.chunk_completed(chunk_id);
        current_chunk_id.reset();  // Clear - chunk successfully handed off
        ++chunks_downloaded;

        // Report performance to peer_provider (via supervisor)
        output.try_send(std::error_code{}, peer_performance{
            .peer_nonce = peer_nonce,
            .blocks_downloaded = expected_count,
            .download_time_ms = static_cast<uint32_t>(download_ms)
        });
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

    // Notify supervisor that this task is ending (CSP: communicate via channel)
    spdlog::debug("[block_download:shutdown] Peer {} - sending task_ended notification...", addr);
    auto [send_ec] = co_await output.async_send(
        std::error_code{},
        download_task_ended{peer_nonce},
        ::asio::as_tuple(::asio::use_awaitable)
    );
    if (send_ec) {
        spdlog::debug("[block_download:shutdown] Failed to send task_ended for peer {}: {}", addr, send_ec.message());
    }

    spdlog::info("[block_download:shutdown] Peer {} task exiting cleanly (downloaded {} chunks)", addr, chunks_downloaded);
}

// =============================================================================
// Block Download Supervisor
// =============================================================================

::asio::awaitable<void> block_download_supervisor(
    block_download_input_channel& input,
    block_download_channel& output,
    blockchain::header_organizer& organizer
) {
    auto executor = co_await ::asio::this_coro::executor;

    spdlog::debug("[block_supervisor] Task started");

    task_group tasks(executor);

    // Coordinator - created when we get a range
    std::unique_ptr<chunk_coordinator> coordinator;
    std::atomic<uint32_t> active_peers{0};

    // Track peers that already have running download tasks (by nonce)
    boost::unordered_flat_set<uint64_t> spawned_peers;

    // Internal channel for tasks output (blocks + task_ended)
    block_download_task_output_channel task_output(executor, 256);

    // UNIFIED EVENT CHANNEL - combines all input sources
    // This avoids the || operator between multiple channels which causes message loss
    block_supervisor_event_channel events(executor, 512);

    // Stats
    uint64_t blocks_forwarded = 0;
    auto last_stats_time = std::chrono::steady_clock::now();

    // Buffer peers that arrive before we have a range
    std::vector<network::peer_session::ptr> pending_peers;

    // Timer for periodic timeout checks (created here so we can cancel it on shutdown)
    ::asio::steady_timer timeout_timer(executor);
    std::atomic<bool> timer_running{true};

    // Helper to spawn download task for a peer (returns true if spawned)
    auto spawn_download = [&](network::peer_session::ptr peer) -> bool {
        if (peer->stopped() || !coordinator) return false;

        auto const nonce = peer->nonce();
        if (spawned_peers.contains(nonce)) {
            return false;  // Already has a running task
        }

        spdlog::debug("[block_supervisor] Spawning download task for peer {}", peer->authority_with_agent());
        spawned_peers.insert(nonce);
        tasks.spawn(block_download_task(
            peer,
            *coordinator,
            active_peers,
            task_output
        ));
        return true;
    };

    // -------------------------------------------------------------------------
    // Timer task: sends supervisor_timeout to unified events channel
    // Uses external timeout_timer that can be cancelled on shutdown
    // -------------------------------------------------------------------------
    tasks.spawn([&, &timer = timeout_timer]() -> ::asio::awaitable<void> {
        spdlog::debug("[block_supervisor:timer] Started");
        while (timer_running.load(std::memory_order_relaxed)) {
            timer.expires_after(std::chrono::seconds(10));
            auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec || !timer_running.load(std::memory_order_relaxed)) break;
            // Send timeout message to unified channel
            auto [send_ec] = co_await events.async_send(
                std::error_code{}, supervisor_timeout{},
                ::asio::as_tuple(::asio::use_awaitable));
            if (send_ec) break;
        }
        spdlog::debug("[block_supervisor:timer] Ended");
    });

    // -------------------------------------------------------------------------
    // Bridge: input channel -> unified events channel
    // -------------------------------------------------------------------------
    tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[block_supervisor:input_bridge] Started");
        while (true) {
            auto [ec, msg] = co_await input.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[block_supervisor:input_bridge] Channel closed: {}", ec.message());
                break;
            }
            // Forward to unified channel - use async_send to never lose messages
            auto forward = [&](auto&& m) -> ::asio::awaitable<bool> {
                auto [send_ec] = co_await events.async_send(
                    std::error_code{}, std::forward<decltype(m)>(m),
                    ::asio::as_tuple(::asio::use_awaitable));
                co_return !send_ec;
            };

            bool ok = true;
            if (std::holds_alternative<stop_request>(msg)) {
                ok = co_await forward(stop_request{});
            } else if (auto* peers = std::get_if<peers_updated>(&msg)) {
                ok = co_await forward(*peers);
            } else if (auto* range = std::get_if<block_range_request>(&msg)) {
                ok = co_await forward(*range);
            }
            if (!ok) break;
        }
        spdlog::debug("[block_supervisor:input_bridge] Ended");
    });

    // -------------------------------------------------------------------------
    // Bridge: task_output channel -> unified events channel
    // -------------------------------------------------------------------------
    tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[block_supervisor:task_bridge] Started");
        while (true) {
            auto [ec, msg] = co_await task_output.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[block_supervisor:task_bridge] Channel closed: {}", ec.message());
                break;
            }
            // Forward to unified channel - use async_send to never lose messages
            std::error_code send_ec;
            if (auto* block = std::get_if<downloaded_block>(&msg)) {
                g_blocks_received_by_supervisor.fetch_add(1, std::memory_order_relaxed);
                auto [err] = co_await events.async_send(
                    std::error_code{}, std::move(*block),
                    ::asio::as_tuple(::asio::use_awaitable));
                send_ec = err;
            } else if (auto* ended = std::get_if<download_task_ended>(&msg)) {
                auto [err] = co_await events.async_send(
                    std::error_code{}, *ended,
                    ::asio::as_tuple(::asio::use_awaitable));
                send_ec = err;
            } else if (auto* perf = std::get_if<peer_performance>(&msg)) {
                auto [err] = co_await events.async_send(
                    std::error_code{}, *perf,
                    ::asio::as_tuple(::asio::use_awaitable));
                send_ec = err;
            }
            if (send_ec) {
                spdlog::debug("[block_supervisor:task_bridge] Send failed: {}", send_ec.message());
                break;
            }
        }
        spdlog::debug("[block_supervisor:task_bridge] Ended");
    });

    // -------------------------------------------------------------------------
    // Main loop: ONLY receives from unified channel (no || operator at all)
    // -------------------------------------------------------------------------
    while (true) {
        auto [ec, msg] = co_await events.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            spdlog::debug("[block_supervisor] Events channel closed");
            break;
        }

        // Process message based on variant type (FIFO order guaranteed)
        if (std::holds_alternative<stop_request>(msg)) {
            spdlog::info("[block_supervisor] Stop signal received");
            if (coordinator) {
                spdlog::info("[block_supervisor] Stopping coordinator...");
                coordinator->stop();
            }
            break;
        }

        if (auto* block = std::get_if<downloaded_block>(&msg)) {
            auto const height = block->height;

            // Forward block to validation
            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                std::move(*block),
                ::asio::as_tuple(::asio::use_awaitable)
            );
            if (send_ec) {
                spdlog::warn("[block_supervisor] Failed to forward block {} to validation: {}", height, send_ec.message());
            } else {
                // Track successful forward for pipeline debugging
                g_blocks_forwarded_by_supervisor.fetch_add(1, std::memory_order_relaxed);
            }
            ++blocks_forwarded;

            // Log stats periodically
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= std::chrono::seconds(10)) {
                spdlog::info("[block_supervisor] Stats: {} blocks forwarded, {} peers spawned",
                    blocks_forwarded, spawned_peers.size());
                last_stats_time = now;
            }
            continue;
        }

        if (auto* ended = std::get_if<download_task_ended>(&msg)) {
            spawned_peers.erase(ended->peer_nonce);
            spdlog::debug("[block_supervisor] Task ended, removed peer nonce {} from spawned set",
                ended->peer_nonce);
            continue;
        }

        if (auto* request = std::get_if<block_range_request>(&msg)) {
            spdlog::info("[block_supervisor] New range request: {} to {} ({} blocks)",
                request->start_height, request->end_height,
                request->end_height - request->start_height + 1);

            // Create new coordinator for this range
            coordinator = std::make_unique<chunk_coordinator>(
                organizer.index(),
                request->start_height,
                request->end_height
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

        if (auto* peers_msg = std::get_if<peers_updated>(&msg)) {
            if (!coordinator) {
                // Buffer peers until we have a range request
                pending_peers = peers_msg->peers;
                spdlog::debug("[block_supervisor] Buffered {} peers (waiting for range)",
                    pending_peers.size());
                continue;
            }

            // Spawn tasks for any new peers in the list
            size_t spawned = 0;
            for (auto const& peer : peers_msg->peers) {
                if (spawn_download(peer)) {
                    ++spawned;
                }
            }
            if (spawned > 0) {
                spdlog::debug("[block_supervisor] Spawned {} new download tasks", spawned);
            }
            continue;
        }

        if (std::holds_alternative<supervisor_timeout>(msg)) {
            // Periodic timeout - check for stalled chunks
            if (coordinator && !coordinator->is_stopped()) {
                coordinator->check_timeouts();
            }
            continue;
        }

        if (auto* perf = std::get_if<peer_performance>(&msg)) {
            // Forward performance stats through output (bridge will route to peer_provider)
            auto [send_ec] = co_await output.async_send(
                std::error_code{}, *perf,
                ::asio::as_tuple(::asio::use_awaitable));
            if (send_ec) {
                spdlog::debug("[block_supervisor] Failed to send performance stats: {}", send_ec.message());
            }
            continue;
        }
    }

    // Stop timer task - set flag AND cancel timer to wake it immediately
    spdlog::info("[block_supervisor:shutdown] Step 1/5: Stopping timer task...");
    timer_running.store(false, std::memory_order_relaxed);
    timeout_timer.cancel();  // Wake up timer immediately instead of waiting 10s

    // IMPORTANT: Stop coordinator FIRST to signal all tasks to exit
    if (coordinator) {
        spdlog::info("[block_supervisor:shutdown] Step 2/5: Stopping coordinator...");
        coordinator->stop();
    }

    // Close channels BEFORE waiting - this unblocks any download tasks and bridges
    spdlog::info("[block_supervisor:shutdown] Step 3/5: Closing channels...");
    output.close();
    task_output.close();
    events.close();

    // NOW wait for all tasks (download tasks + bridges) to finish
    auto const active = tasks.active_count();
    spdlog::info("[block_supervisor:shutdown] Step 4/5: Waiting for {} tasks to complete...", active);
    co_await tasks.join();
    spdlog::info("[block_supervisor:shutdown] Step 5/5: All tasks completed");

    // NOW it's safe to destroy the coordinator
    spdlog::info("[block_supervisor] Destroying coordinator...");
    coordinator.reset();

    spdlog::info("[block_supervisor] Task ended cleanly");
}

// =============================================================================
// Block Validation Task
// =============================================================================

::asio::awaitable<void> block_validation_task(
    blockchain::block_chain& chain,
    block_validation_input_channel& input,
    block_validated_channel& output,
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
    uint64_t fast_total_txs = 0;      // Total transactions in window
    uint64_t fast_total_bytes = 0;    // Total bytes in window

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

    // Time-based progress tracking (fallback for boundary-based logging)
    auto last_progress_log = std::chrono::steady_clock::now();
    constexpr auto progress_log_interval = std::chrono::seconds(10);

    // Track when we started waiting for a specific height (for stuck detection)
    uint32_t waiting_for_height = 0;
    auto waiting_since = std::chrono::steady_clock::now();
    constexpr auto stuck_threshold = std::chrono::seconds(30);

    // Single channel, FIFO processing - no priority issues
    while (true) {
        auto [ec, msg] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            spdlog::debug("[block_validation] Input channel closed");
            break;
        }

        // Process message based on variant type (FIFO order guaranteed)
        if (std::holds_alternative<stop_request>(msg)) {
            spdlog::debug("[block_validation] Stop signal received");
            break;
        }

        auto* downloaded_ptr = std::get_if<downloaded_block>(&msg);
        if (!downloaded_ptr) {
            spdlog::warn("[block_validation] Received non-block message, variant index: {}", msg.index());
            continue;
        }

        auto& downloaded = *downloaded_ptr;

        // Track received for pipeline debugging
        g_blocks_received_by_validation.fetch_add(1, std::memory_order_relaxed);

        // Track latest peer count for display
        last_seen_peers = downloaded.active_peers;

        // Always add to pending first (simplifies logic) - store full struct for source_peer tracking
        pending[downloaded.height] = downloaded;

        // Check if we can process the next expected block (either just received or already buffered)
        if (!pending.contains(next_height)) {
            // Track how long we've been waiting for this specific height
            auto now = std::chrono::steady_clock::now();
            if (waiting_for_height != next_height) {
                // Started waiting for a new height
                waiting_for_height = next_height;
                waiting_since = now;
            }

            // Time-based progress even when waiting for blocks
            if (now - last_progress_log >= progress_log_interval) {
                last_progress_log = now;
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                double rate = elapsed > 0 ? static_cast<double>(validated_count) / elapsed : 0;
                auto waiting_secs = std::chrono::duration_cast<std::chrono::seconds>(now - waiting_since).count();
                spdlog::info("[block_sync:waiting] validated={} pending={} (waiting for height {} for {}s) | {:.0f} blk/s",
                    validated_count, pending.size(), next_height, waiting_secs, rate);
            }

            // If we've been waiting too long for the same height, log detailed diagnostics
            if (now - waiting_since >= stuck_threshold) {
                // Find min/max heights in pending buffer
                uint32_t min_h = UINT32_MAX, max_h = 0;
                for (auto const& [h, _] : pending) {
                    min_h = std::min(min_h, h);
                    max_h = std::max(max_h, h);
                }

                // Check which blocks we have in the chunk containing next_height
                // Assuming chunk_size of 16, find chunk boundaries
                constexpr uint32_t chunk_size = 16;
                uint32_t chunk_start = ((next_height - 1) / chunk_size) * chunk_size + 1;
                uint32_t chunk_end = chunk_start + chunk_size - 1;

                std::string chunk_status;
                for (uint32_t h = chunk_start; h <= chunk_end; ++h) {
                    if (!chunk_status.empty()) chunk_status += " ";
                    if (pending.contains(h)) {
                        chunk_status += std::to_string(h) + ":OK";
                    } else if (h < next_height) {
                        chunk_status += std::to_string(h) + ":validated";
                    } else {
                        chunk_status += std::to_string(h) + ":MISSING";
                    }
                }

                // Pipeline counters for debugging
                auto const task_sent = g_blocks_sent_by_tasks.load(std::memory_order_relaxed);
                auto const sup_recv = g_blocks_received_by_supervisor.load(std::memory_order_relaxed);
                auto const sup_fwd = g_blocks_forwarded_by_supervisor.load(std::memory_order_relaxed);
                auto const brg_recv = g_blocks_received_by_bridge.load(std::memory_order_relaxed);
                auto const brg_fwd = g_blocks_forwarded_by_bridge.load(std::memory_order_relaxed);
                auto const val_recv = g_blocks_received_by_validation.load(std::memory_order_relaxed);

                spdlog::warn("[block_sync:STUCK] Waiting for height {} for {}s! "
                    "Pending: {} blocks [{}, {}]. "
                    "Chunk [{}-{}]: [{}]",
                    next_height,
                    std::chrono::duration_cast<std::chrono::seconds>(now - waiting_since).count(),
                    pending.size(), min_h, max_h,
                    chunk_start, chunk_end, chunk_status);

                spdlog::warn("[block_sync:STUCK] Pipeline counts: "
                    "task_sent={} sup_recv={} sup_fwd={} brg_recv={} brg_fwd={} val_recv={}",
                    task_sent, sup_recv, sup_fwd, brg_recv, brg_fwd, val_recv);

                // Show where blocks are being lost (non-zero means loss)
                auto const lost_task_sup = task_sent - sup_recv;
                auto const lost_sup_fwd = sup_recv - sup_fwd;
                auto const lost_sup_brg = sup_fwd - brg_recv;
                auto const lost_brg_fwd = brg_recv - brg_fwd;
                auto const lost_brg_val = brg_fwd - val_recv;

                if (lost_task_sup > 0 || lost_sup_fwd > 0 || lost_sup_brg > 0 || lost_brg_fwd > 0 || lost_brg_val > 0) {
                    spdlog::error("[block_sync:STUCK] BLOCK LOSS DETECTED: "
                        "task->sup={} sup_recv->fwd={} sup->brg={} brg_recv->fwd={} brg->val={}",
                        lost_task_sup, lost_sup_fwd, lost_sup_brg, lost_brg_fwd, lost_brg_val);
                }

                // Reset waiting_since to avoid spamming this log
                waiting_since = now;
            }

            continue;  // Gap not filled yet, wait for more blocks
        }

        // We found the block we were waiting for, reset tracking
        waiting_for_height = 0;

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

            // Debug: log first few validated blocks
            if (validated_count < 5) {
                spdlog::debug("[block_validation] Validated block {} in {}us, result: {}",
                    next_height, organize_us, result ? result.message() : "success");
            }

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
                fast_total_txs += it->second.block->transactions().size();
                fast_total_bytes += it->second.block->serialized_size(true);
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

        // Log progress at round 1000 boundaries (e.g., 228000, 229000) OR every 10 seconds
        auto now = std::chrono::steady_clock::now();
        bool time_to_log = (now - last_progress_log >= progress_log_interval);
        uint32_t current_thousand = (next_height - 1) / 1000;
        bool boundary_crossed = (current_thousand > last_logged_thousand);

        if (boundary_crossed || time_to_log) {
            if (boundary_crossed) {
                last_logged_thousand = current_thousand;
            }
            last_progress_log = now;
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            double rate = elapsed > 0 ? static_cast<double>(validated_count) / elapsed : 0;

            // Calculate ETA
            auto const current_height = current_thousand * 1000;
            auto const remaining = checkpoint_height > current_height
                ? checkpoint_height - current_height : 0;
            auto const eta_secs = rate > 0 ? uint64_t(remaining / rate) : 0;
            auto const eta_mins = eta_secs / 60;

            // Show different label for fast mode vs full mode
            if (current_height <= checkpoint_height) {
                spdlog::info("[block_sync:fast] {}/{} ({} blk/s, ETA: {}m) | {} peers | pending: {}",
                    current_height, checkpoint_height, int(rate), eta_mins,
                    last_seen_peers, pending.size());
            } else {
                // TODO: For full validation mode, target is headers_synced_to not checkpoint
                spdlog::info("[block_sync:full] {} ({} blk/s) | {} peers | pending: {}",
                    current_height, int(rate),
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
                    double merkle_avg_ms = avg_vec(fast_merkle_times);
                    double push_avg_ms = avg_vec(fast_push_times);

                    // Calculate per-tx merkle time (microseconds)
                    int64_t total_merkle_us = 0;
                    for (auto t : fast_merkle_times) total_merkle_us += t;
                    double merkle_per_tx_us = fast_total_txs > 0
                        ? static_cast<double>(total_merkle_us) / fast_total_txs : 0.0;

                    // Calculate per-byte push time (nanoseconds)
                    int64_t total_push_us = 0;
                    for (auto t : fast_push_times) total_push_us += t;
                    double push_per_byte_ns = fast_total_bytes > 0
                        ? static_cast<double>(total_push_us) * 1000.0 / fast_total_bytes : 0.0;

                    spdlog::info("[validation] height {} fast mode avg: merkle={:.3f}ms ({:.2f}us/tx) push={:.3f}ms ({:.2f}ns/byte)",
                        current_height, merkle_avg_ms, merkle_per_tx_us, push_avg_ms, push_per_byte_ns);
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
                fast_total_txs = 0;
                fast_total_bytes = 0;
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
