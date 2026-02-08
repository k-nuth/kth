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
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/infrastructure/utility/stats.hpp>
#include <kth/network/protocols_coro.hpp>

namespace kth::node::sync {

using namespace ::asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

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

// 2026-02-07: Counter for unique block_download task IDs (helps identify tasks in logs)
std::atomic<uint64_t> g_block_download_task_id{0};

// =============================================================================
// Block Download Task (per-peer)
// =============================================================================

::asio::awaitable<void> block_download_task(
    network::peer_session::ptr peer,
    std::shared_ptr<chunk_coordinator> coordinator,
    std::atomic<uint32_t>& active_peers,
    block_download_task_output_channel& output
) {
    auto const addr = peer->authority_with_agent();
    auto const peer_nonce = peer->nonce();
    size_t chunks_downloaded = 0;

    // Timing stats accumulation
    std::vector<uint64_t> download_times_ms;
    std::vector<uint64_t> send_times_ms;
    std::vector<uint64_t> network_times_us;    // Network wait time per chunk
    std::vector<uint64_t> deserialize_times_us; // Deserialize time per chunk
    download_times_ms.reserve(100);
    send_times_ms.reserve(100);
    network_times_us.reserve(100);
    deserialize_times_us.reserve(100);

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
        std::vector<uint64_t> const& network_times;
        std::vector<uint64_t> const& deserialize_times;
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

            // Print network vs deserialize breakdown if we have data
            if (!network_times.empty() && !deserialize_times.empty()) {
                uint64_t total_net = 0, total_deser = 0;
                for (auto t : network_times) total_net += t;
                for (auto t : deserialize_times) total_deser += t;

                double net_avg_ms = static_cast<double>(total_net) / network_times.size() / 1000.0;
                double deser_avg_ms = static_cast<double>(total_deser) / deserialize_times.size() / 1000.0;
                double total_ms = net_avg_ms + deser_avg_ms;
                double net_pct = total_ms > 0 ? (net_avg_ms / total_ms) * 100.0 : 0;

                spdlog::info("[download] Peer {} timing: net={:.1f}ms ({:.0f}%) deser={:.1f}ms ({:.0f}%)",
                    addr, net_avg_ms, net_pct, deser_avg_ms, 100.0 - net_pct);
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
    } guard{active_peers, addr, chunks_downloaded, download_times_ms, send_times_ms, network_times_us, deserialize_times_us};

    // Track current chunk for cleanup on exception
    std::optional<uint32_t> current_chunk_id;

    try {

    auto executor = co_await ::asio::this_coro::executor;

    // Log initial state
    spdlog::info("[block_download] Peer {} entering main loop (peer_stopped={}, coord_stopped={}, coord_complete={})",
        addr, peer->stopped(), coordinator->is_stopped(), coordinator->is_complete());

    while (!peer->stopped() && !coordinator->is_stopped()) {
        // Claim chunk via coordinator - lock-free CAS
        auto maybe_chunk = coordinator->claim_chunk();
        if (!maybe_chunk) {
            // No more chunks or all slots busy - wait and retry
            if (coordinator->is_complete() || coordinator->is_stopped()) {
                spdlog::info("[block_download] Peer {} exiting: sync complete or stopped (complete={}, stopped={})",
                    addr, coordinator->is_complete(), coordinator->is_stopped());
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
        auto [chunk_start, chunk_end] = coordinator->chunk_range(chunk_id);

        spdlog::debug("[block_download] Peer {} claiming chunk {} (blocks {}-{})",
            addr, chunk_id, chunk_start, chunk_end);

        // Build request with hashes from coordinator
        std::vector<std::pair<uint32_t, hash_digest>> blocks;
        blocks.reserve(chunk_end - chunk_start + 1);

        for (uint32_t h = chunk_start; h <= chunk_end; ++h) {
            auto hash = coordinator->get_block_hash(h);
            if (hash == null_hash) {
                spdlog::error("[block_download] No hash for height {}", h);
                continue;
            }
            blocks.emplace_back(h, hash);
        }

        if (blocks.empty()) {
            coordinator->chunk_failed(chunk_id);
            continue;
        }

        // Download from peer - measure time
        auto download_start = std::chrono::steady_clock::now();
        auto result = co_await network::request_blocks_batch<network::sync_mode::fast>(
            *peer, blocks, 60s);
        auto download_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - download_start).count();

        if (!result) {
            spdlog::info("[block_download] Peer {} FAILED chunk {} after {}ms: {} (peer_stopped={}, coord_stopped={})",
                addr, chunk_id, download_ms, result.error().message(),
                peer->stopped(), coordinator->is_stopped());
            // Report failure - slot reset to FREE for retry by another peer
            coordinator->chunk_failed(chunk_id);
            break;
        }

        // Validate we got all expected blocks
        auto const expected_count = chunk_end - chunk_start + 1;
        if (result->size() != expected_count) {
            spdlog::warn("[block_download] Peer {} returned {} blocks for chunk {} (expected {}), failing chunk",
                addr, result->size(), chunk_id, expected_count);
            coordinator->chunk_failed(chunk_id);
            continue;  // Try another chunk instead of disconnecting
        }

        spdlog::debug("[block_download] Peer {} downloaded {} blocks for chunk {} in {}ms",
            addr, result->size(), chunk_id, download_ms);
        download_times_ms.push_back(download_ms);

        // Accumulate network and deserialize times from individual blocks
        uint64_t chunk_net_us = 0, chunk_deser_us = 0;
        for (auto const& blk : *result) {
            chunk_net_us += blk.network_wait_us;
            chunk_deser_us += blk.deserialize_us;
        }
        network_times_us.push_back(chunk_net_us);
        deserialize_times_us.push_back(chunk_deser_us);

        // Send each block to output channel - measure time
        // 2026-01-29: Use try_send_with_retry instead of bare try_send or async_send
        // async_send can cause deadlocks, bare try_send exits too quickly
        auto send_start = std::chrono::steady_clock::now();
        auto const current_peers = active_peers.load(std::memory_order_relaxed);
        bool send_error = false;
        for (auto& blk : *result) {
            auto const height = blk.height;

            // Check stop conditions before sending
            if (peer->stopped() || coordinator->is_stopped()) {
                spdlog::debug("[block_download] Peer {} stopping before send (block {})",
                    addr, height);
                coordinator->chunk_failed(chunk_id);
                send_error = true;
                break;
            }

            // Record timestamp when sending to supervisor (for pipeline latency tracking)
            auto sent_to_supervisor_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

            // Use try_send_with_retry - retries with delays instead of blocking or giving up
            bool sent = co_await try_send_with_retry(output,
                downloaded_light_block{
                    .height = height,
                    .block = std::make_shared<domain::chain::light_block const>(std::move(blk.block)),
                    .source_peer = peer,
                    .active_peers = current_peers,
                    .deserialize_us = blk.deserialize_us,
                    .network_wait_us = blk.network_wait_us,
                    .received_from_net_us = blk.received_at_us,
                    .sent_to_supervisor_us = static_cast<uint64_t>(sent_to_supervisor_us)
                },
                50,  // 50 attempts
                20ms  // 20ms between attempts = 1 second total max wait
            );

            if (!sent) {
                // Channel still full after retries - something is wrong with the consumer
                spdlog::error("[block_download] Channel full after 50 retries for block {} - consumer may be stuck!",
                    height);
                coordinator->chunk_failed(chunk_id);
                send_error = true;
                break;
            }

            // Track successful sends for pipeline debugging
            g_blocks_sent_by_tasks.fetch_add(1, std::memory_order_relaxed);
        }

        // If send failed, DON'T exit - try another chunk
        // Only exit if peer/coordinator stopped
        if (send_error && (peer->stopped() || coordinator->is_stopped())) {
            break;
        }
        if (send_error) {
            // Channel issue but we're not stopped - continue with next chunk
            continue;
        }

        auto send_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - send_start).count();
        send_times_ms.push_back(send_ms);

        // Report success only after all blocks sent
        coordinator->chunk_completed(chunk_id);
        current_chunk_id.reset();  // Clear - chunk successfully handed off
        ++chunks_downloaded;

        // Report performance to peer_provider (via supervisor)
        if (!output.try_send(std::error_code{}, peer_performance{
            .peer_nonce = peer_nonce,
            .blocks_downloaded = expected_count,
            .download_time_ms = static_cast<uint32_t>(download_ms)
        })) {
            spdlog::debug("[block_download] Channel full, peer_performance dropped");
        }
    }

    // Log why we exited the while loop
    spdlog::info("[block_download] Peer {} exited main loop (peer_stopped={}, coord_stopped={}, coord_complete={}, chunks={})",
        addr, peer->stopped(), coordinator->is_stopped(), coordinator->is_complete(), chunks_downloaded);

    } catch (::asio::system_error const& e) {
        // Asio system errors (e.g., operation_aborted during shutdown) are expected
        spdlog::debug("[block_download] Peer {} asio error: {} ({})", addr, e.what(), e.code().value());
        if (current_chunk_id) {
            coordinator->chunk_failed(*current_chunk_id);
        }
    } catch (std::exception const& e) {
        spdlog::error("[block_download] Peer {} exception: {}", addr, e.what());
        if (current_chunk_id) {
            coordinator->chunk_failed(*current_chunk_id);
        }
    } catch (...) {
        spdlog::error("[block_download] Peer {} unknown exception", addr);
        if (current_chunk_id) {
            coordinator->chunk_failed(*current_chunk_id);
        }
    }

    // Notify supervisor that this task is ending (CSP: communicate via channel)
    // Use retry to ensure this critical message is delivered
    spdlog::debug("[block_download:shutdown] Peer {} - sending task_ended notification...", addr);
    if (!co_await try_send_with_retry(output, download_task_ended{peer_nonce})) {
        spdlog::warn("[block_download:shutdown] Failed to send task_ended after retries for peer {}", addr);
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

    task_group tasks("block_supervisor_tasks", executor);

    // Coordinator - created when we get a range
    // NOTE: Using shared_ptr so download tasks can safely hold a reference even if
    // a new range request arrives. Each task keeps the old coordinator alive until done.
    std::shared_ptr<chunk_coordinator> coordinator;
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
    uint64_t last_blocks_forwarded = 0;
    uint64_t bytes_downloaded = 0;
    uint64_t last_bytes_downloaded = 0;
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
        // 2026-02-07: task_id is a unique counter, nonce identifies the peer session
        auto const task_id = g_block_download_task_id.fetch_add(1);
        auto task_name = fmt::format("block_download_{}:{}:{}", peer->authority(), nonce, task_id);
        tasks.spawn(task_name, block_download_task(
            peer,
            coordinator,  // Pass shared_ptr - task keeps coordinator alive until done
            active_peers,
            task_output
        ));
        return true;
    };

    // -------------------------------------------------------------------------
    // Timer task: sends supervisor_timeout to unified events channel
    // Uses external timeout_timer that can be cancelled on shutdown
    // -------------------------------------------------------------------------
    tasks.spawn("block_supervisor_timer", [&, &timer = timeout_timer]() -> ::asio::awaitable<void> {
        spdlog::debug("[block_supervisor:timer] Started");
        while (timer_running.load(std::memory_order_relaxed)) {
            timer.expires_after(std::chrono::seconds(10));
            auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec || !timer_running.load(std::memory_order_relaxed)) break;
            // Send timeout message to unified channel
            if (!events.try_send(std::error_code{}, supervisor_timeout{})) {
                spdlog::debug("[block_supervisor:timer] Channel full, timeout dropped");
                break;
            }
        }
        spdlog::debug("[block_supervisor:timer] Ended");
    });

    // -------------------------------------------------------------------------
    // Bridge: input channel -> unified events channel
    // -------------------------------------------------------------------------
    tasks.spawn("block_supervisor_input_bridge", [&]() -> ::asio::awaitable<void> {
        spdlog::debug("[block_supervisor:input_bridge] Started");
        while (true) {
            auto [ec, msg] = co_await input.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[block_supervisor:input_bridge] Channel closed: {}", ec.message());
                // Notify main loop to exit by sending stop_request
                if (!events.try_send(std::error_code{}, stop_request{})) {
                    spdlog::warn("[block_supervisor:input_bridge] Channel full, stop_request on close dropped");
                }
                break;
            }
            // Forward to unified channel
            if (std::holds_alternative<stop_request>(msg)) {
                if (!events.try_send(std::error_code{}, stop_request{})) {
                    spdlog::warn("[block_supervisor:input_bridge] Channel full, stop_request dropped");
                }
                break;  // Exit after forwarding stop signal
            } else if (auto* peers = std::get_if<peers_updated>(&msg)) {
                if (!events.try_send(std::error_code{}, *peers)) {
                    spdlog::warn("[block_supervisor:input_bridge] Channel full, peers_updated dropped");
                    break;
                }
            } else if (auto* range = std::get_if<block_range_request>(&msg)) {
                if (!events.try_send(std::error_code{}, *range)) {
                    spdlog::warn("[block_supervisor:input_bridge] Channel full, block_range_request dropped");
                    break;
                }
            }
        }
        spdlog::debug("[block_supervisor:input_bridge] Ended");
    });

    // -------------------------------------------------------------------------
    // Bridge: task_output channel -> unified events channel
    // -------------------------------------------------------------------------
    tasks.spawn("block_supervisor_task_bridge", [&]() -> ::asio::awaitable<void> {
        spdlog::debug("[block_supervisor:task_bridge] Started");
        uint64_t blocks_forwarded = 0;
        while (true) {
            auto [ec, msg] = co_await task_output.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[block_supervisor:task_bridge] Channel closed: {}", ec.message());
                break;
            }
            // Forward to unified channel
            if (auto* block = std::get_if<downloaded_light_block>(&msg)) {
                g_blocks_received_by_supervisor.fetch_add(1, std::memory_order_relaxed);
                // Use try_send_with_retry for blocks
                bool sent = co_await try_send_with_retry(events, std::move(*block), 20, std::chrono::milliseconds(10));
                if (!sent) {
                    spdlog::error("[block_supervisor:task_bridge] Events channel full after retries! blocks_forwarded={}", blocks_forwarded);
                    break;
                }
                ++blocks_forwarded;
            } else if (auto* ended = std::get_if<download_task_ended>(&msg)) {
                // task_ended critical - use retry
                bool sent = co_await try_send_with_retry(events, *ended, 20, std::chrono::milliseconds(10));
                if (!sent) {
                    spdlog::error("[block_supervisor:task_bridge] Events channel full for task_ended!");
                    break;
                }
            } else if (auto* perf = std::get_if<peer_performance>(&msg)) {
                if (!events.try_send(std::error_code{}, *perf)) {
                    spdlog::debug("[block_supervisor:task_bridge] Channel full, peer_performance dropped");
                }
            }
        }
        spdlog::info("[block_supervisor:task_bridge] Ended, forwarded {} blocks", blocks_forwarded);
    });

    // -------------------------------------------------------------------------
    // Main loop: ONLY receives from unified channel (no || operator at all)
    // -------------------------------------------------------------------------
    spdlog::info("[block_supervisor] Entering main event loop");
    uint64_t events_processed = 0;
    while (true) {
        auto [ec, msg] = co_await events.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            spdlog::debug("[block_supervisor] Events channel closed");
            break;
        }
        ++events_processed;

        // Process message based on variant type (FIFO order guaranteed)
        if (std::holds_alternative<stop_request>(msg)) {
            spdlog::info("[block_supervisor] Stop signal received");
            if (coordinator) {
                spdlog::info("[block_supervisor] Stopping coordinator...");
                coordinator->stop();
            }
            break;
        }

        if (auto* block = std::get_if<downloaded_light_block>(&msg)) {
            auto const height = block->height;

            // Log first few blocks and periodically after that
            if (blocks_forwarded < 10 || blocks_forwarded % 1000 == 0) {
                spdlog::debug("[block_supervisor] Forwarding block {} (total forwarded: {})", height, blocks_forwarded);
            }

            // TEMPORARY: Skip validation/storage for download-only benchmark
            // TODO: Remove this block when done benchmarking
#if 0
            // Forward block to validation with retry
            bool sent = co_await try_send_with_retry(output, std::move(*block), 20, std::chrono::milliseconds(10));
            if (!sent) {
                spdlog::error("[block_supervisor] Output channel full after retries for block {} (forwarded so far: {})",
                    height, blocks_forwarded);
                break;
            }
#endif
            // Track successful forward for pipeline debugging
            g_blocks_forwarded_by_supervisor.fetch_add(1, std::memory_order_relaxed);
            ++blocks_forwarded;
            bytes_downloaded += block->block->serialized_size();

            // Log stats periodically
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= 2s) {
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_time).count();
                auto blocks_delta = blocks_forwarded - last_blocks_forwarded;
                auto bytes_delta = bytes_downloaded - last_bytes_downloaded;
                auto blk_rate = elapsed_ms > 0 ? (blocks_delta * 1000 / elapsed_ms) : 0;
                double mb_rate = elapsed_ms > 0 ? (double(bytes_delta) / 1024.0 / 1024.0) / (double(elapsed_ms) / 1000.0) : 0.0;
                spdlog::info("[block_supervisor] Stats: {} blocks ({} blk/s, {:.1f} MB/s), {} peers downloading",
                    blocks_forwarded, blk_rate, mb_rate, spawned_peers.size());
                last_stats_time = now;
                last_blocks_forwarded = blocks_forwarded;
                last_bytes_downloaded = bytes_downloaded;
            }
            continue;
        }

        if (auto* ended = std::get_if<download_task_ended>(&msg)) {
            spawned_peers.erase(ended->peer_nonce);
            spdlog::info("[block_supervisor:task_ended] nonce={}, remaining downloading={}",
                ended->peer_nonce, spawned_peers.size());
            continue;
        }

        if (auto* request = std::get_if<block_range_request>(&msg)) {
            spdlog::info("[block_supervisor] New range request: {} to {} ({} blocks)",
                request->start_height, request->end_height,
                request->end_height - request->start_height + 1);

            // Stop old coordinator if one exists - this signals running tasks to exit gracefully
            // Tasks hold shared_ptr so old coordinator stays alive until they finish
            if (coordinator) {
                spdlog::debug("[block_supervisor] Stopping old coordinator before creating new one");
                coordinator->stop();
            }

            // Create new coordinator for this range
            coordinator = std::make_shared<chunk_coordinator>(
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
            // Count idle peers (connected but not downloading)
            size_t idle_count = 0;
            for (auto const& peer : peers_msg->peers) {
                if (!peer->stopped() && !spawned_peers.contains(peer->nonce())) {
                    ++idle_count;
                }
            }
            spdlog::info("[block_supervisor:peers_updated] received {} peers, {} idle, {} downloading (coordinator={})",
                peers_msg->peers.size(), idle_count, spawned_peers.size(), coordinator ? "yes" : "no");

            if (!coordinator) {
                // Buffer peers until we have a range request
                pending_peers = peers_msg->peers;
                spdlog::debug("[block_supervisor] Buffered {} peers (waiting for range)",
                    pending_peers.size());
                continue;
            }

            // Spawn tasks for any new peers in the list
            size_t spawned = 0;
            size_t stopped_count = 0;
            size_t already_spawned_count = 0;
            for (auto const& peer : peers_msg->peers) {
                if (peer->stopped()) {
                    ++stopped_count;
                    continue;
                }
                if (spawned_peers.contains(peer->nonce())) {
                    ++already_spawned_count;
                    continue;
                }
                if (spawn_download(peer)) {
                    ++spawned;
                }
            }
            spdlog::info("[block_supervisor:peers_updated] result: spawned={}, already_running={}, stopped={}",
                spawned, already_spawned_count, stopped_count);
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
            if (!output.try_send(std::error_code{}, *perf)) {
                spdlog::debug("[block_supervisor] Channel full, peer_performance dropped");
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

    // Cancel and close internal channels BEFORE waiting - this unblocks any download tasks and bridges
    // NOTE: Don't close `output` - it's owned by sync_orchestrator, peer_provider closes it
    // NOTE: cancel() wakes up pending async ops, close() alone does NOT!
    spdlog::info("[block_supervisor:shutdown] Step 3/5: Closing internal channels...");
    task_output.cancel();
    task_output.close();
    events.cancel();
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
    spdlog::info("[block_validation] Task started at height {}, checkpoint at {}",
        start_height, checkpoint_height);
    spdlog::info("[block_validation] Entering main receive loop, waiting for blocks...");

    // OWNED state - not shared with anyone
    uint32_t next_height = start_height;
    uint32_t last_seen_peers = 0;
    boost::unordered_flat_map<uint32_t, downloaded_light_block> pending;

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
    // 2026-02-02: Reduced from 30s to 10s to match chunk timeout
    constexpr auto stuck_threshold = std::chrono::seconds(10);

    // Channel wait timing stats (accumulate per window)
    std::vector<uint64_t> recv_wait_times_us;
    recv_wait_times_us.reserve(1000);

    // Block deserialization timing (from download task, for comparing with light_block later)
    std::vector<uint64_t> block_deserialize_times_us;
    std::vector<uint64_t> block_network_times_us;
    block_deserialize_times_us.reserve(1000);
    block_network_times_us.reserve(1000);

    // Pipeline latency tracking (to distinguish network vs channel overhead)
    std::vector<uint64_t> pipeline_download_to_supervisor_us;  // time from net receive to supervisor send
    std::vector<uint64_t> pipeline_supervisor_to_validation_us; // time from supervisor send to validation receive
    pipeline_download_to_supervisor_us.reserve(1000);
    pipeline_supervisor_to_validation_us.reserve(1000);

    // Single channel, FIFO processing - no priority issues
    while (true) {
        // Measure time waiting to receive from bridge
        auto recv_start = std::chrono::steady_clock::now();
        auto [ec, msg] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        auto recv_wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - recv_start).count();

        if (ec) {
            spdlog::debug("[block_validation] Input channel closed");
            break;
        }

        recv_wait_times_us.push_back(recv_wait_us);

        // Process message based on variant type (FIFO order guaranteed)
        if (std::holds_alternative<stop_request>(msg)) {
            spdlog::debug("[block_validation] Stop signal received");
            break;
        }

        auto* downloaded_ptr = std::get_if<downloaded_light_block>(&msg);
        if (!downloaded_ptr) {
            spdlog::warn("[block_validation] Received non-block message, variant index: {}", msg.index());
            continue;
        }

        auto& downloaded = *downloaded_ptr;

        // Track received for pipeline debugging
        auto const total_received = g_blocks_received_by_validation.fetch_add(1, std::memory_order_relaxed) + 1;

        // Log first few blocks received
        if (total_received <= 10 || total_received % 1000 == 0) {
            spdlog::info("[block_validation] Received block {} (total received: {}, pending: {})",
                downloaded.height, total_received, pending.size());
        }

        // Track latest peer count for display
        last_seen_peers = downloaded.active_peers;

        // Accumulate deserialize and network timing from download task
        block_deserialize_times_us.push_back(downloaded.deserialize_us);
        block_network_times_us.push_back(downloaded.network_wait_us);

        // Track pipeline latency (network vs channel overhead)
        if (downloaded.received_from_net_us > 0 && downloaded.sent_to_supervisor_us > 0) {
            auto arrived_at_validation_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

            // Time from network receive to supervisor send (download task processing)
            auto download_to_supervisor = downloaded.sent_to_supervisor_us - downloaded.received_from_net_us;
            // Time from supervisor send to validation receive (channel overhead: supervisor→bridge→validation)
            auto supervisor_to_validation = arrived_at_validation_us - downloaded.sent_to_supervisor_us;

            pipeline_download_to_supervisor_us.push_back(download_to_supervisor);
            pipeline_supervisor_to_validation_us.push_back(supervisor_to_validation);
        }

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

#if 0  // TODO: disabled for light_block benchmark - needs message::block
            // Use fast mode under checkpoint, full validation above
            if (next_height <= checkpoint_height) {
                result = co_await chain.organize_fast(it->second.block, next_height);
            } else {
                result = co_await chain.organize(it->second.block, /*headers_pre_validated=*/true);
            }
#endif

            auto organize_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - organize_start).count();
            organize_times_us.push_back(organize_us);

            // Debug: log first few validated blocks
            if (validated_count < 5) {
                spdlog::debug("[block_validation] Validated block {} in {}us, result: {}",
                    next_height, organize_us, result ? result.message() : "success");
            }

#if 0  // TODO: disabled for light_block benchmark - needs message::block
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
#endif

            if (!output.try_send(std::error_code{}, block_validated{
                .height = next_height,
                .result = result,
                .source_peer = it->second.source_peer
            })) {
                spdlog::warn("[block_validation] Channel full, block_validated {} dropped", next_height);
            }

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

                    // Block statistics (for baseline measurements)
                    auto blocks_in_window = fast_merkle_times.size();
                    double avg_txs_per_block = blocks_in_window > 0 ? static_cast<double>(fast_total_txs) / blocks_in_window : 0;
                    double avg_bytes_per_block = blocks_in_window > 0 ? static_cast<double>(fast_total_bytes) / blocks_in_window : 0;
                    spdlog::info("[block_stats] window: {} blocks, {} txs ({:.1f} txs/blk), {:.2f} MB ({:.0f} bytes/blk)",
                        blocks_in_window, fast_total_txs, avg_txs_per_block,
                        fast_total_bytes / 1'000'000.0, avg_bytes_per_block);
                } else if (!deser_times.empty()) {
                    // Full validation mode
                    spdlog::info("[validation] phases avg: "
                        "deser={:.2f}ms check={:.2f}ms populate={:.2f}ms accept={:.2f}ms "
                        "connect={:.2f}ms notify={:.2f}ms push={:.2f}ms",
                        avg_vec(deser_times), avg_vec(check_times), avg_vec(pop_times),
                        avg_vec(accept_times), avg_vec(connect_times), avg_vec(notify_times),
                        avg_vec(push_times));
                }

                // Print block storage stats
                auto& stats = global_sync_stats();
                auto alloc_calls = stats.allocate_calls.load(std::memory_order_relaxed);
                auto write_calls = stats.write_block_calls.load(std::memory_order_relaxed);

                if (alloc_calls > 0 || write_calls > 0) {
                    auto alloc_time_ns = stats.allocate_time_ns.load(std::memory_order_relaxed);
                    auto alloc_bytes = stats.allocate_bytes.load(std::memory_order_relaxed);
                    auto write_time_ns = stats.write_block_time_ns.load(std::memory_order_relaxed);
                    auto write_bytes = stats.write_block_bytes.load(std::memory_order_relaxed);
                    auto open_calls = stats.file_open_calls.load(std::memory_order_relaxed);
                    auto open_time_ns = stats.file_open_time_ns.load(std::memory_order_relaxed);

                    double alloc_avg_ms = alloc_calls > 0 ? (alloc_time_ns / 1'000'000.0) / alloc_calls : 0.0;
                    double write_avg_ms = write_calls > 0 ? (write_time_ns / 1'000'000.0) / write_calls : 0.0;
                    double open_avg_us = open_calls > 0 ? (open_time_ns / 1000.0) / open_calls : 0.0;
                    double write_throughput_mb_s = write_time_ns > 0
                        ? (write_bytes / 1'000'000.0) / (write_time_ns / 1'000'000'000.0) : 0.0;

                    spdlog::info("[block_storage] alloc: n={} avg={:.1f}ms total={:.1f}MB | "
                        "write: n={} avg={:.2f}ms {:.1f}MB/s | open: n={} avg={:.1f}us",
                        alloc_calls, alloc_avg_ms, alloc_bytes / 1'000'000.0,
                        write_calls, write_avg_ms, write_throughput_mb_s,
                        open_calls, open_avg_us);

                    // Reset storage stats for next window
                    stats.allocate_calls = 0;
                    stats.allocate_time_ns = 0;
                    stats.allocate_bytes = 0;
                    stats.write_block_calls = 0;
                    stats.write_block_time_ns = 0;
                    stats.write_block_bytes = 0;
                    stats.file_open_calls = 0;
                    stats.file_open_time_ns = 0;
                }

                // Print channel wait timing (time validation spent waiting for blocks)
                if (!recv_wait_times_us.empty()) {
                    uint64_t total_wait = 0;
                    for (auto t : recv_wait_times_us) total_wait += t;
                    double avg_wait_ms = static_cast<double>(total_wait) / recv_wait_times_us.size() / 1000.0;

                    // Calculate what percentage of time was spent waiting vs processing
                    uint64_t total_organize = 0;
                    for (auto t : organize_times_us) total_organize += t;
                    double total_time_ms = (static_cast<double>(total_wait) + static_cast<double>(total_organize)) / 1000.0;
                    double wait_pct = total_time_ms > 0 ? (static_cast<double>(total_wait) / 1000.0 / total_time_ms) * 100.0 : 0;

                    spdlog::info("[validation] channel_wait: avg={:.2f}ms/blk ({:.0f}% of time waiting for blocks)",
                        avg_wait_ms, wait_pct);
                }

                // Print block deserialization timing (from download task - heavy block parsing)
                if (!block_deserialize_times_us.empty()) {
                    uint64_t total_deser = 0, total_net = 0;
                    for (auto t : block_deserialize_times_us) total_deser += t;
                    for (auto t : block_network_times_us) total_net += t;

                    double deser_avg_ms = static_cast<double>(total_deser) / block_deserialize_times_us.size() / 1000.0;
                    double net_avg_ms = static_cast<double>(total_net) / block_network_times_us.size() / 1000.0;

                    spdlog::info("[download_timing] block deser={:.3f}ms/blk net_wait={:.3f}ms/blk (n={})",
                        deser_avg_ms, net_avg_ms, block_deserialize_times_us.size());
                }

                // Print pipeline latency breakdown (network vs channel overhead)
                if (!pipeline_download_to_supervisor_us.empty()) {
                    uint64_t total_dl_to_sup = 0, total_sup_to_val = 0;
                    for (auto t : pipeline_download_to_supervisor_us) total_dl_to_sup += t;
                    for (auto t : pipeline_supervisor_to_validation_us) total_sup_to_val += t;

                    double dl_to_sup_avg_ms = static_cast<double>(total_dl_to_sup) / pipeline_download_to_supervisor_us.size() / 1000.0;
                    double sup_to_val_avg_ms = static_cast<double>(total_sup_to_val) / pipeline_supervisor_to_validation_us.size() / 1000.0;
                    double total_pipeline_ms = dl_to_sup_avg_ms + sup_to_val_avg_ms;

                    // Calculate percentages
                    double dl_pct = total_pipeline_ms > 0 ? (dl_to_sup_avg_ms / total_pipeline_ms) * 100.0 : 0;
                    double ch_pct = total_pipeline_ms > 0 ? (sup_to_val_avg_ms / total_pipeline_ms) * 100.0 : 0;

                    spdlog::info("[pipeline_latency] download_task={:.3f}ms ({:.0f}%) channels={:.3f}ms ({:.0f}%) total={:.3f}ms/blk",
                        dl_to_sup_avg_ms, dl_pct, sup_to_val_avg_ms, ch_pct, total_pipeline_ms);
                }

                // Reset for next window
                organize_times_us.clear();
                recv_wait_times_us.clear();
                block_deserialize_times_us.clear();
                block_network_times_us.clear();
                pipeline_download_to_supervisor_us.clear();
                pipeline_supervisor_to_validation_us.clear();
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

    // NOTE: Don't close output channel here - peer_provider closes all channels during shutdown
    spdlog::info("[block_validation] Task ended, validated {} blocks", validated_count);
}

} // namespace kth::node::sync
