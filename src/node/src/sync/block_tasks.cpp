// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/utxo_deletion_sweep.hpp>
#include <kth/node/sync/block_tasks.hpp>

#include <kth/node/sync/download_ownership.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <unistd.h>  // sysconf(_SC_PAGESIZE)

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/infrastructure/utility/stats.hpp>
#include <kth/blockchain/utxo_builder.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/node/sync/reorg.hpp>

namespace kth::node::sync {

namespace {

// MTP calculation for incremental UTXO build (same algorithm as utxo_builder)
[[nodiscard]]
uint32_t calculate_mtp(std::deque<uint32_t> const& timestamps) {
    if (timestamps.empty()) return 0;
    std::vector<uint32_t> sorted(timestamps.begin(), timestamps.end());
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2];
}

} // anonymous namespace

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

// Active download peer count (updated by supervisor, read by fast_validation for stats)
std::atomic<uint32_t> g_active_download_peers{0};

// =============================================================================
// Block Download Task (per-peer)
// =============================================================================

::asio::awaitable<void> block_download_task(
    network::peer_session::ptr peer,
    std::shared_ptr<chunk_coordinator> coordinator,
    std::atomic<uint32_t>& active_peers,
    block_download_task_output_channel& output,
    uint64_t task_id,
    uint64_t coordinator_epoch,
    fast_validation_input_channel* fast_val
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
    spdlog::info("[block_download] Peer {} entering main loop (peer_stopped={}, coord_stopped={}, coord_complete={}, thread_id={})",
        addr, peer->stopped(), coordinator->is_stopped(), coordinator->is_complete(),
        std::hash<std::thread::id>{}(std::this_thread::get_id()));

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

        // Capture the generation BEFORE reading any hash. Reading it after the
        // download would stamp blocks of the old branch with the new generation
        // if a switch landed while we waited on the peer — they would then be
        // accepted as current, which is precisely the corruption the stamp exists
        // to stop.
        auto const request_generation = coordinator->generation();

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

        // A switch may have landed while the hashes were being collected, in
        // which case they are a mix of two branches. Drop the request and let the
        // chunk be re-claimed against the new chain rather than downloading a
        // set that was never a chain.
        if (coordinator->generation() != request_generation) {
            spdlog::info("[block_download] Chain switched while building chunk {} — releasing it",
                chunk_id);
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

        // Send blocks - measure time
        auto send_start = std::chrono::steady_clock::now();
        bool send_error = false;

        if (fast_val) {
            // Fast path: collect all blocks into a downloaded_chunk and send once
            // This eliminates 16 channel operations per chunk (1 instead of 16)
            std::vector<std::shared_ptr<domain::chain::light_block const>> chunk_blocks;
            chunk_blocks.reserve(result->size());
            for (auto& blk : *result) {
                chunk_blocks.push_back(std::make_shared<domain::chain::light_block const>(std::move(blk.block)));
            }

            // Backpressure: retry for up to 60 seconds (storage is slower than download)
            bool sent = co_await try_send_with_retry(*fast_val,
                downloaded_chunk{
                    .start_height = chunk_start,
                    .chunk_id = chunk_id,
                    .blocks = std::move(chunk_blocks),
                    .source_peer = peer,
                    .generation = request_generation
                },
                600,   // 600 attempts
                100ms  // 100ms between attempts = 60 seconds total max wait
            );

            if (!sent) {
                spdlog::error("[block_download] Fast validation channel full after 60s for chunk {} - storage bottleneck!",
                    chunk_id);
                coordinator->chunk_failed(chunk_id);
                send_error = true;
            }

            g_blocks_sent_by_tasks.fetch_add(expected_count, std::memory_order_relaxed);
        } else {
            // Old path: send each block individually to supervisor
            auto const current_peers = active_peers.load(std::memory_order_relaxed);
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
                    spdlog::error("[block_download] Channel full after 50 retries for block {} - consumer may be stuck!",
                        height);
                    coordinator->chunk_failed(chunk_id);
                    send_error = true;
                    break;
                }

                g_blocks_sent_by_tasks.fetch_add(1, std::memory_order_relaxed);
            }
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
    // The identity this instance was started with, repeated verbatim: the
    // supervisor matches on all three, so a report from a worker that has already
    // been replaced changes nothing (#652).
    if (!co_await try_send_with_retry(output,
            download_task_ended{peer_nonce, task_id, coordinator_epoch})) {
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
    blockchain::header_organizer& organizer,
    fast_validation_input_channel* fast_val,
    download_worker_launcher launcher
) {
    auto executor = co_await ::asio::this_coro::executor;

    spdlog::info("[block_supervisor] Task started (thread_id={})",
        std::hash<std::thread::id>{}(std::this_thread::get_id()));

    task_group tasks("block_supervisor_tasks", executor);

    // Coordinator - created when we get a range
    // NOTE: Using shared_ptr so download tasks can safely hold a reference even if
    // a new range request arrives. Each task keeps the old coordinator alive until done.
    std::shared_ptr<chunk_coordinator> coordinator;
    std::atomic<uint32_t> active_peers{0};

    // Who owns each peer's download slot, and which peers exist at all (#652).
    //
    // The two are different sets, and conflating them is what left a range with
    // no consumers: the old code tracked only the peers with a RUNNING task, so
    // when a range finished and every task ended, it held nothing and the next
    // range had nobody to start. Recovery then depended on an unrelated peer
    // connect or disconnect — twice in one mainnet IBD, for 12m43s and 10m40s.
    sync::download_ownership ownership;

    // The peer objects behind the nonces the ownership reasons about. Replaced
    // wholesale on every update, because peers_updated is a SNAPSHOT: the
    // provider keeps one cumulative list, prunes stopped peers and broadcasts all
    // of it, so a nonce that is absent has been withdrawn.
    boost::unordered_flat_map<uint64_t, network::peer_session::ptr> known_peers;

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

    // Timer for periodic timeout checks (created here so we can cancel it on shutdown)
    ::asio::steady_timer timeout_timer(executor);
    std::atomic<bool> timer_running{true};

    // Helper to spawn download task for a peer (returns true if spawned)
    // Returns false ONLY for conditions checked before the start: no coordinator,
    // a peer that is gone, or a nonce that already has a worker. A start that
    // cannot happen is not one of them — it propagates.
    auto spawn_download = [&](network::peer_session::ptr const& peer) -> bool {
        // Re-checked here and not only by the caller: a snapshot can age between
        // the decision and this point.
        if ( ! peer || peer->stopped() || ! coordinator) return false;

        auto const nonce = peer->nonce();
        if (ownership.has_worker(nonce)) {
            return false;  // Already has a running task
        }

        auto const task_id = g_block_download_task_id.fetch_add(1);
        auto const epoch = ownership.epoch();
        auto task_name = fmt::format("block_download_{}:{}:{}", peer->authority(), nonce, task_id);

        spdlog::debug("[block_supervisor] Spawning download task for peer {} (task {}, epoch {})",
            peer->authority_with_agent(), task_id, epoch);

        // No try/catch here, deliberately. `task_group::spawn` increments its
        // active count before it can throw, so catching and carrying on would
        // leave the group waiting at join() for a task that will never report —
        // a supervisor that looked recovered and a shutdown that never finished.
        // A start that cannot happen is not a condition this can absorb, so it
        // propagates as it did before.
        if (launcher) {
            // The seam replaces the download itself and nothing else: the
            // bookkeeping below and the report handling are the real ones either
            // way.
            launcher(peer, coordinator, task_id, epoch, task_output);
        } else {
            tasks.spawn(task_name, block_download_task(
                peer,
                coordinator,  // Pass shared_ptr - task keeps coordinator alive until done
                active_peers,
                task_output,
                task_id,
                epoch,
                fast_val      // chunk-based fast validation (nullptr = old path)
            ));
        }

        // Immediately after the start and with no suspension in between, so the
        // report of this worker — whenever it arrives — is matched against a slot
        // that already exists.
        ownership.record(nonce, task_id, epoch);
        g_active_download_peers.store(
            static_cast<uint32_t>(ownership.worker_count()), std::memory_order_relaxed);
        return true;
    };

    // Start `nonce` against the CURRENT coordinator, using the peer the latest
    // snapshot holds — never a pointer captured by the worker that just ended.
    auto spawn_known = [&](uint64_t nonce) -> bool {
        auto const it = known_peers.find(nonce);
        if (it == known_peers.end()) return false;
        return spawn_download(it->second);
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

        // Forward a message to the unified events channel, retrying while the
        // channel is full instead of dropping it and breaking the bridge — a
        // dropped peers_updated / block_range_request there would silently kill
        // all forwarding and stall the download. Returns false when the channel is
        // closed or the wait is cancelled (both mean shutdown: the supervisor
        // cancels + closes `events` before joining), so the caller exits cleanly
        // and tasks.join() never blocks on a wedged retry.
        auto forward = [&](auto const& m) -> ::asio::awaitable<bool> {
            size_t retries = 0;
            while ( ! events.try_send(std::error_code{}, m)) {
                if ( ! events.is_open()) {
                    co_return false;  // shutting down
                }
                if (retries == 0 || retries % 50 == 0) {
                    spdlog::warn("[block_supervisor:input_bridge] events channel full — retrying forward (retry {})", retries);
                }
                ++retries;
                ::asio::steady_timer timer(co_await ::asio::this_coro::executor);
                timer.expires_after(std::chrono::milliseconds(20));
                auto [tec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
                if (tec) {
                    co_return false;
                }
            }
            if (retries > 0) {
                spdlog::info("[block_supervisor:input_bridge] forward succeeded after {} retries", retries);
            }
            co_return true;
        };

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
                if ( ! co_await forward(*peers)) break;
            } else if (auto* range = std::get_if<block_range_request>(&msg)) {
                if ( ! co_await forward(*range)) break;
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

            // Capture size before moving
            auto const block_size = block->block->serialized_size();

            // Forward block to validation with retry
            bool sent = co_await try_send_with_retry(output, std::move(*block), 20, std::chrono::milliseconds(10));
            if (!sent) {
                spdlog::error("[block_supervisor] Output channel full after retries for block {} (forwarded so far: {})",
                    height, blocks_forwarded);
                break;
            }
            // Track successful forward for pipeline debugging
            g_blocks_forwarded_by_supervisor.fetch_add(1, std::memory_order_relaxed);
            ++blocks_forwarded;
            bytes_downloaded += block_size;

            // Log stats periodically
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= 2s) {
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_time).count();
                auto blocks_delta = blocks_forwarded - last_blocks_forwarded;
                auto bytes_delta = bytes_downloaded - last_bytes_downloaded;
                auto blk_rate = elapsed_ms > 0 ? (blocks_delta * 1000 / elapsed_ms) : 0;
                double mb_rate = elapsed_ms > 0 ? (double(bytes_delta) / 1024.0 / 1024.0) / (double(elapsed_ms) / 1000.0) : 0.0;
                spdlog::info("[block_supervisor] Stats: {} blocks ({} blk/s, {:.1f} MB/s), {} peers downloading",
                    blocks_forwarded, blk_rate, mb_rate, ownership.worker_count());
                last_stats_time = now;
                last_blocks_forwarded = blocks_forwarded;
                last_bytes_downloaded = bytes_downloaded;
            }
            continue;
        }

        if (auto* ended = std::get_if<download_task_ended>(&msg)) {
            // The exact instance decides, before anything else is considered: a
            // report that does not name the live slot belongs to a worker that
            // was already replaced, and must not retire the one that replaced it.
            //
            // Only then the handoff. A worker of an earlier range has just freed
            // the only slot its peer has, and this is the moment that peer can be
            // given to the current coordinator — which is what replaces the
            // accidental peer event this defect depended on.
            auto const wants_workers = coordinator && ! coordinator->is_stopped() &&
                ! coordinator->is_complete();
            auto const handoff = ownership.ended(
                ended->peer_nonce, ended->task_id, ended->coordinator_epoch, wants_workers);

            g_active_download_peers.store(
                static_cast<uint32_t>(ownership.worker_count()), std::memory_order_relaxed);
            spdlog::info("[block_supervisor:task_ended] nonce={} task={} epoch={}, remaining downloading={}",
                ended->peer_nonce, ended->task_id, ended->coordinator_epoch,
                ownership.worker_count());

            if (handoff) {
                if (spawn_known(*handoff)) {
                    spdlog::info("[block_supervisor:task_ended] peer {} handed to the current range "
                        "(epoch {})", *handoff, ownership.epoch());
                } else {
                    // A refusal here is the state this whole change is about: the
                    // range has one consumer fewer and nothing else will say so.
                    spdlog::warn("[block_supervisor:task_ended] peer {} could not be handed to the "
                        "current range (epoch {}); it is no longer eligible or could not start",
                        *handoff, ownership.epoch());
                }
            }
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

            // The coordinator is installed FIRST, then the epoch moves, and only
            // then are workers started — so nothing is ever started against a
            // coordinator that is no longer current.
            //
            // begin_range() answers with the known peers that have no live slot.
            // A peer whose previous worker is still finishing is deliberately not
            // among them: it is handed over when that worker reports, which is
            // the one moment its slot is free.
            auto const to_start = ownership.begin_range();
            size_t started = 0;
            for (auto const nonce : to_start) {
                if (spawn_known(nonce)) {
                    ++started;
                }
            }
            spdlog::info("[block_supervisor] Range {}-{} (epoch {}): started {} of {} known peers, "
                "{} still finishing the previous range",
                request->start_height, request->end_height, ownership.epoch(),
                started, to_start.size(),
                ownership.worker_count() - started);
            continue;
        }

        if (auto* peers_msg = std::get_if<peers_updated>(&msg)) {
            // A SNAPSHOT: the provider keeps one cumulative list, prunes stopped
            // peers and broadcasts all of it, so a nonce that is absent has been
            // withdrawn and must not be started again. (The block channel gets
            // the fast-peer subset, so this set means "eligible to download".)
            known_peers.clear();
            std::vector<uint64_t> nonces;
            nonces.reserve(peers_msg->peers.size());
            for (auto const& peer : peers_msg->peers) {
                if ( ! peer || peer->stopped()) continue;
                known_peers.emplace(peer->nonce(), peer);
                nonces.push_back(peer->nonce());
            }
            ownership.set_known(nonces);

            spdlog::info("[block_supervisor:peers_updated] received {} peers, {} known, {} downloading "
                "(coordinator={})", peers_msg->peers.size(), known_peers.size(),
                ownership.worker_count(), coordinator ? "yes" : "no");

            if ( ! coordinator) {
                // Nothing to attach them to yet. They are remembered rather than
                // buffered: the next range starts from what is known, so this no
                // longer has to be the only chance they get.
                continue;
            }

            size_t spawned = 0;
            for (auto const nonce : nonces) {
                if (spawn_known(nonce)) ++spawned;
            }
            spdlog::info("[block_supervisor:peers_updated] result: spawned={}, already_running={}",
                spawned, ownership.worker_count() - spawned);
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

            // Lightweight validation: merkle root check for blocks under checkpoint
            if (next_height <= checkpoint_height) {
                result = co_await chain.organize_fast(it->second.block, next_height);
            }
            // Above checkpoint (full validation) is Stage 5 — not yet enabled

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
                size_t tx_count = it->second.block->tx_count();
                size_t total_bytes = it->second.block->serialized_size();
                spdlog::warn("[block_validation] Slow block at height {}: {}ms, {} txs, {} bytes",
                    next_height, organize_us / 1000, tx_count, total_bytes);
            }

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
                // Full-mode target is the current header tip; recompute ETA
                // against that. get_last_heights() is sync and reads cached
                // counters, so it's cheap to call from this log path.
                uint32_t full_target = current_height;
                if (auto h = chain.get_last_heights()) {
                    full_target = h->header;
                }
                auto const full_remaining = full_target > current_height
                    ? full_target - current_height : 0;
                auto const full_eta_mins = rate > 0
                    ? uint64_t(full_remaining / rate) / 60 : 0;
                spdlog::info("[block_sync:full] {}/{} ({} blk/s, ETA: {}m) | {} peers | pending: {}",
                    current_height, full_target, int(rate), full_eta_mins,
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

// =============================================================================
// Fast Validation Task (chunk-based, parallel merkle)
// =============================================================================

::asio::awaitable<void> fast_validation_task(
    blockchain::block_chain& chain,
    fast_validation_input_channel& input,
    chunk_validated_channel& output,
    block_storage_input_channel* storage
) {
    spdlog::info("[fast_validation] Task started (storage={})", storage ? "yes" : "no");

    uint64_t chunks_validated = 0;
    uint64_t blocks_validated = 0;
    uint64_t bytes_validated = 0;
    auto start_time = std::chrono::steady_clock::now();

    // Time-based stats (every 5 seconds)
    auto last_stats_time = start_time;
    uint64_t last_blocks = 0;
    uint64_t last_bytes = 0;

    // RSS tracking helper (Linux: reads /proc/self/statm)
    auto get_rss_mb = []() -> double {
        std::ifstream statm("/proc/self/statm");
        if (!statm.is_open()) return 0.0;
        size_t size_pages = 0, resident_pages = 0;
        statm >> size_pages >> resident_pages;
        auto const page_size = sysconf(_SC_PAGESIZE);
        return static_cast<double>(resident_pages) * page_size / (1024.0 * 1024.0);
    };

    auto initial_rss = get_rss_mb();
    spdlog::info("[fast_validation] Initial RSS: {:.0f} MB", initial_rss);

    while (true) {
        auto [ec, msg] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            spdlog::debug("[fast_validation] Input channel closed: {}", ec.message());
            break;
        }

        if (std::holds_alternative<stop_request>(msg)) {
            spdlog::debug("[fast_validation] Stop signal received");
            break;
        }

        auto& chunk = std::get<downloaded_chunk>(msg);
        auto const chunk_block_count = chunk.blocks.size();
        auto const chunk_start = chunk.start_height;
        auto const chunk_peer = chunk.source_peer;
        auto const chunk_generation = chunk.generation;

        // Track bytes before validation (blocks are still in memory)
        uint64_t chunk_bytes = 0;
        for (auto const& blk : chunk.blocks) {
            chunk_bytes += blk->raw_data().size();
        }

        auto val_start = std::chrono::steady_clock::now();
        auto result = co_await chain.validate_chunk(chunk.blocks, chunk_start);
        auto val_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - val_start).count();

        ++chunks_validated;
        blocks_validated += chunk_block_count;
        bytes_validated += chunk_bytes;

        // Time-based stats logging (every 5 seconds)
        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= 5s) {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_time).count();
            auto blocks_delta = blocks_validated - last_blocks;
            auto bytes_delta = bytes_validated - last_bytes;
            auto blk_rate = elapsed_ms > 0 ? (blocks_delta * 1000 / elapsed_ms) : 0UL;
            double mb_rate = elapsed_ms > 0 ? (double(bytes_delta) / 1024.0 / 1024.0) / (double(elapsed_ms) / 1000.0) : 0.0;
            auto peers = g_active_download_peers.load(std::memory_order_relaxed);
            auto current_rss = get_rss_mb();
            auto live_blocks = domain::chain::light_block::live_instances.load(std::memory_order_relaxed);

            spdlog::info("[sync] Stats: {} blocks at height {} ({} blk/s, {:.1f} MB/s), {} peers downloading, RSS: {:.0f} MB, live_blocks: {}",
                blocks_validated, chunk_start + chunk_block_count - 1,
                blk_rate, mb_rate, peers, current_rss, live_blocks);

            last_stats_time = now;
            last_blocks = blocks_validated;
            last_bytes = bytes_validated;
        }

        if (!result && storage) {
            // Valid chunk: forward to storage task (with block data intact)
            if ( ! co_await try_send_with_retry(*storage, downloaded_chunk{
                .start_height = chunk_start,
                .chunk_id = chunk.chunk_id,
                .blocks = std::move(chunk.blocks),
                .source_peer = chunk_peer,
                .generation = chunk_generation
            })) {
                spdlog::error("[fast_validation] Storage channel full after retries!");
                break;
            }
        } else {
            // No storage channel or validation failed:
            // Release block memory and send result directly to coordinator
            chunk.blocks.clear();
            chunk.blocks.shrink_to_fit();
            chunk.source_peer.reset();

            if ( ! co_await try_send_with_retry(output, chunk_validated{
                .start_height = chunk_start,
                .block_count = static_cast<uint32_t>(chunk_block_count),
                .result = result,
                .source_peer = chunk_peer
            })) {
                spdlog::error("[fast_validation] Output channel full after retries!");
                break;
            }
        }
    }

    auto final_rss = get_rss_mb();
    spdlog::info("[fast_validation] Task ended, validated {} chunks ({} blocks), RSS: {:.0f} MB (delta: +{:.0f})",
        chunks_validated, blocks_validated, final_rss, final_rss - initial_rss);
}

// =============================================================================
// Block Storage Task (writes validated blocks to flat files)
// =============================================================================
//
// Option 2: Parallel writes with pre-allocated positions.
// - No out-of-order buffer — writes each chunk immediately on arrival.
// - store_chunk() internally: 1 serial allocation + N parallel writes.
// - LMDB height updated with max (chunks may arrive out of order).
// - Eliminates the stall-on-gap problem from the previous sequential design.
//
// =============================================================================

::asio::awaitable<void> block_storage_task(
    blockchain::block_chain& chain,
    block_storage_input_channel& input,
    chunk_validated_channel& output,
    uint32_t start_height,
    blockchain::header_organizer& organizer,
    std::atomic<uint32_t>* contiguous_out
) {
    spdlog::info("[block_storage] Task started at height {}", start_height);

    // This task marks blocks have_data and advances the validated height, both of
    // which a chain switch rewrites, so it participates in the reorg barrier.
    reorg_participation const storage_participation(chain);

    uint64_t chunks_stored = 0;
    uint64_t blocks_stored = 0;
    uint32_t max_stored_height = start_height > 0 ? start_height - 1 : 0;
    uint32_t contiguous_height = start_height;  // all blocks in [start_height, contiguous_height) are stored
    auto task_start = std::chrono::steady_clock::now();

    // Collect chunk start heights in arrival order for fragmentation analysis.
    // Since allocation is serial (single coroutine), arrival order = disk order.
    std::vector<uint32_t> chunk_arrival_order;
    chunk_arrival_order.reserve(64000);

    // RSS tracking helper
    auto get_rss_mb = []() -> double {
        std::ifstream statm("/proc/self/statm");
        if (!statm.is_open()) return 0.0;
        size_t size_pages = 0, resident_pages = 0;
        statm >> size_pages >> resident_pages;
        auto const page_size = sysconf(_SC_PAGESIZE);
        return static_cast<double>(resident_pages) * page_size / (1024.0 * 1024.0);
    };

    auto initial_rss = get_rss_mb();

    while (true) {
        // Park BEFORE waiting for input. A paused pipeline delivers no chunk, so a
        // check placed after the receive is never reached and the barrier would
        // wait on a task that is itself waiting.
        if (chain.reorg_pause_requested()) {
            chain.enter_reorg_barrier();
            while (chain.reorg_pause_requested() && ! chain.stopped()) {
                ::asio::steady_timer pause_timer(co_await ::asio::this_coro::executor);
                pause_timer.expires_after(std::chrono::milliseconds(50));
                co_await pause_timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            }
            chain.leave_reorg_barrier();

            // The switch rewound the chain and reset the shared counter: re-derive
            // the local cursors instead of carrying pre-switch values.
            if (contiguous_out) {
                contiguous_height = contiguous_out->load(std::memory_order_acquire);
                max_stored_height = contiguous_height > 0 ? contiguous_height - 1 : 0;
                spdlog::info("[block_storage] Resuming after reorg at contiguous height {}",
                    contiguous_height);
            }
            continue;
        }

        auto [ec, msg] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            spdlog::debug("[block_storage] Input channel closed: {}", ec.message());
            break;
        }

        if (std::holds_alternative<stop_request>(msg)) {
            spdlog::debug("[block_storage] Stop signal received");
            break;
        }

        auto& chunk = std::get<downloaded_chunk>(msg);
        auto const chunk_start = chunk.start_height;
        auto const chunk_count = static_cast<uint32_t>(chunk.blocks.size());
        auto const chunk_peer = chunk.source_peer;
        auto const chunk_generation = chunk.generation;

        // Drop work from before a chain switch. Such a chunk was downloaded for
        // the abandoned branch, so its heights now name different blocks —
        // storing it would write the wrong data and mark the wrong hashes
        // have_data. This is the point of the generation stamp: a chunk that was
        // in flight, or buffered behind the reorg barrier, is recognisable
        // afterwards instead of being applied to the new chain.
        if (chunk_generation != chain.chain_generation()) {
            spdlog::info("[block_storage] Dropping chunk at {} from generation {} (chain is at {}) "
                "— downloaded for a branch that has since been abandoned",
                chunk_start, chunk_generation, chain.chain_generation());
            chunk.blocks.clear();
            chunk.blocks.shrink_to_fit();
            continue;
        }

        auto store_start = std::chrono::steady_clock::now();

        // Store chunk immediately — no ordering required.
        // store_chunk() does: 1 serial allocation + N parallel writes + header_index update.
        auto store_error = co_await chain.store_chunk(chunk.blocks, chunk_start);
        if (store_error) {
            spdlog::error("[block_storage] Failed to store chunk at {}: {}",
                chunk_start, store_error.message());
        }

        auto store_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - store_start).count();

        if (!store_error) {
            auto const chunk_last = chunk_start + chunk_count - 1;
            if (chunk_last > max_stored_height) {
                max_stored_height = chunk_last;
            }
            chunk_arrival_order.push_back(chunk_start);

            // Release block memory after storing
            chunk.blocks.clear();
            chunk.blocks.shrink_to_fit();

            // Advance contiguous_height using header_index have_data flags.
            // store_chunk() already set have_data for each stored block.
            //
            // By height through the active chain: the index numbers entries in
            // arrival order, so once a side branch is stored an entry's index is
            // no longer its height. Walking the index directly would count the
            // abandoned branch's blocks as contiguous and push the validated tip
            // past where the chain actually reaches.
            auto const prev_contiguous = contiguous_height;
            auto const& hdr = chain.headers();
            while (true) {
                auto const idx = hdr.active_at(static_cast<int32_t>(contiguous_height));
                if (idx == blockchain::header_index::null_index) break;
                if ( ! hdr.has_status(idx, blockchain::header_status::have_data)) break;
                ++contiguous_height;
            }

            // Publish contiguous height for utxo_build_task
            if (contiguous_out) {
                contiguous_out->store(contiguous_height, std::memory_order_release);
            }

            // Advance the finalized block as newly stored (batch-validated)
            // blocks extend the contiguous validated range.
            if (contiguous_height > prev_contiguous) {
                organizer.note_block_validated(static_cast<int32_t>(contiguous_height - 1));
            }
        } else {
            // Store failed — release memory
            chunk.blocks.clear();
            chunk.blocks.shrink_to_fit();
        }

        ++chunks_stored;
        blocks_stored += chunk_count;

        // Send chunk_validated to coordinator
        if ( ! co_await try_send_with_retry(output, chunk_validated{
            .start_height = chunk_start,
            .block_count = chunk_count,
            .result = store_error,
            .source_peer = chunk_peer
        })) {
            spdlog::error("[block_storage] Output channel full after retries!");
            break;
        }

        // Log progress periodically
        if (chunks_stored % 100 == 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - task_start).count();
            double rate = elapsed > 0 ? static_cast<double>(blocks_stored) / elapsed : 0;
            auto current_rss = get_rss_mb();
            spdlog::info("[block_storage] {} chunks ({} blocks) stored, {:.0f} blk/s, contiguous [{}..{}], max {}, last_store {}us, RSS: {:.0f} MB (+{:.0f})",
                chunks_stored, blocks_stored, rate,
                start_height, contiguous_height > 0 ? contiguous_height - 1 : 0, max_stored_height,
                store_us, current_rss, current_rss - initial_rss);
        }
    }


    // This task does NOT touch last_block_height (#653). Storing a block makes it
    // downloadable, not connected: the bytes are in a stdio buffer, the index
    // entry is in memory, and no barrier has run. The marker means the CONNECTED
    // tip — publish_chain_view builds state at it and disconnect_block refuses to
    // rewind past it — so writing the stored height here published a claim the
    // UTXO set could not back.
    //
    // It also only ever ran on the way out, which is what made the defect
    // self-masking: during the run the marker never moved, and a clean stop wrote
    // a recent-looking height that let the next start drain the remainder
    // immediately. The connect path publishes it now, per batch, with the barrier
    // and the record.

    // Fragmentation analysis: how ordered are the chunks on disk?
    // Since allocation is serial, arrival order = disk order.
    // We compare arrival order vs. ideal (sorted by height).
    if (chunk_arrival_order.size() > 1) {
        auto const n = chunk_arrival_order.size();

        // Count consecutive pairs that are in ascending height order
        uint64_t in_order = 0;
        for (size_t i = 1; i < n; ++i) {
            if (chunk_arrival_order[i] > chunk_arrival_order[i - 1]) {
                ++in_order;
            }
        }
        double order_pct = 100.0 * static_cast<double>(in_order) / (n - 1);

        // Count "backwards seeks": how many times a sequential height scan
        // would need to seek backwards in the file
        auto sorted = chunk_arrival_order;
        std::sort(sorted.begin(), sorted.end());

        // Build rank: for each position in arrival order, what's its rank in sorted order?
        // Number of inversions approximates the disorder
        boost::unordered_flat_map<uint32_t, size_t> height_to_rank;
        for (size_t i = 0; i < sorted.size(); ++i) {
            height_to_rank[sorted[i]] = i;
        }

        uint64_t inversions = 0;
        for (size_t i = 1; i < n; ++i) {
            if (height_to_rank[chunk_arrival_order[i]] < height_to_rank[chunk_arrival_order[i - 1]]) {
                ++inversions;
            }
        }

        spdlog::info("[block_storage:fragmentation] {} chunks: {:.1f}% in-order on disk, {} inversions ({:.1f}% backward seeks needed for sequential read)",
            n, order_pct, inversions, 100.0 * static_cast<double>(inversions) / (n - 1));
    }

    // Full header_index dump: height, hash, file, pos, status for every stored block
    {
        auto const& hdr = chain.headers();
        auto const total = hdr.size();
        uint32_t have_data_count = 0;
        uint32_t missing_data_count = 0;
        uint32_t first_gap = 0;
        bool found_gap = false;

        spdlog::info("[block_storage:header_index_dump] Dumping {} entries...", total);

        for (uint32_t idx = 0; idx < total; ++idx) {
            auto const file_num = hdr.get_file_number(idx);
            auto const data_pos = hdr.get_data_pos(idx);
            auto const status = hdr.get_status(idx);
            auto const has_data = has_flag(status, blockchain::header_status::have_data);

            if (has_data) {
                ++have_data_count;
            } else {
                ++missing_data_count;
                if (!found_gap && idx >= start_height) {
                    first_gap = idx;
                    found_gap = true;
                }
            }

            // Print every block (height, hash_prefix, file, pos, status flags)
            // Use debug level to avoid flooding — the full dump goes to log file
            auto const hash = hdr.get_hash(idx);
            spdlog::info("[header_index] h={} hash={:08x}… file={} pos={} status={:#04x}{}",
                idx,
                // First 4 bytes of hash as hex prefix
                (uint32_t(hash[0]) << 24) | (uint32_t(hash[1]) << 16) |
                (uint32_t(hash[2]) << 8) | uint32_t(hash[3]),
                file_num, data_pos, uint32_t(status),
                has_data ? " [DATA]" : "");
        }

        spdlog::info("[block_storage:header_index_dump] Summary: {} total, {} have_data, {} missing_data, contiguous [{}..{}]{}",
            total, have_data_count, missing_data_count,
            start_height, contiguous_height > 0 ? contiguous_height - 1 : 0,
            found_gap ? fmt::format(", first_gap at height {}", first_gap) : "");
    }

    auto final_rss = get_rss_mb();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - task_start).count();
    auto elapsed_min = total_elapsed / 60;
    auto elapsed_sec = total_elapsed % 60;
    spdlog::info("[block_storage] Task ended, stored {} chunks ({} blocks), contiguous [{}..{}], max {}, wall time {}m{}s, RSS: {:.0f} MB (delta: +{:.0f})",
        chunks_stored, blocks_stored,
        start_height, contiguous_height > 0 ? contiguous_height - 1 : 0,
        max_stored_height, elapsed_min, elapsed_sec,
        final_rss, final_rss - initial_rss);
}

// =============================================================================
// UTXO-build sync decisions (pure; unit-tested in test/sync_decisions.cpp)
// =============================================================================

uint32_t resume_utxo_built_height(std::optional<uint32_t> saved, uint32_t start_height) {
    if (saved) {
        return *saved;
    }
    return start_height > 0 ? start_height - 1 : 0;
}

uint32_t utxo_batch_len(uint32_t available, uint32_t batch_size, bool stale) {
    if (available >= batch_size) {
        return batch_size;
    }
    if (available >= 1 && ! stale) {
        return available;
    }
    return 0;
}

// =============================================================================
// Incremental UTXO Build Task
// =============================================================================

::asio::awaitable<void> utxo_build_task(
    blockchain::block_chain& chain,
    std::atomic<uint32_t> const& contiguous_height,
    uint32_t start_height,
    domain::config::network network,
    std::function<bool()> should_stop,
    std::function<void(std::string const&)> on_fatal
) {
    spdlog::info("[utxo_build] Task started at height {}", start_height);

    constexpr uint32_t batch_size = 1000;
    constexpr auto poll_interval = std::chrono::milliseconds(500);

    // Highest checkpoint height: at/below it IBD is merkle-only (fast), strictly
    // above it every block runs FULL validation. Constant for the whole run.
    uint32_t const checkpoint_height =
        static_cast<uint32_t>(chain.chain_settings().max_checkpoint_height);

    // Load bloom filter. The embedded bloom is the FINAL UTXO set as of the height
    // it was built for: it prunes outputs spent before then. Use it only when that
    // height matches the active checkpoint — otherwise skip-insert would drop
    // outputs still unspent at this checkpoint (a lowered test checkpoint, or any
    // build whose embedded bloom targets a different height). It is valid only up to
    // the checkpoint; above it we need the live UTXO set to resolve prevouts for
    // full validation, so the bloom is disabled per batch below.
    auto const bloom = blockchain::load_utxo_bloom();
    auto const* bloom_ptr = bloom.get();
    if (bloom_ptr != nullptr &&
        blockchain::embedded_bloom_checkpoint_height() != checkpoint_height) {
        spdlog::info(
            "[utxo_build] Embedded bloom checkpoint {} != active checkpoint {}; disabling bloom",
            blockchain::embedded_bloom_checkpoint_height(), checkpoint_height);
        bloom_ptr = nullptr;
    }

    // Resume from saved progress. The persisted utxo-built height is the
    // authoritative floor (see resume_utxo_built_height): it must win over
    // start_height, which is derived from the block-sync marker (blocks
    // downloaded) and can run ahead of what has been built. See the header note.
    auto const saved = chain.get_utxo_built_height();
    std::optional<uint32_t> const saved_opt =
        saved ? std::optional<uint32_t>(*saved) : std::nullopt;
    uint32_t utxo_built_height = resume_utxo_built_height(saved_opt, start_height);
    if (saved_opt) {
        spdlog::info("[utxo_build] Resuming from height {}", utxo_built_height);
    }

    // This task writes the UTXO set, so a chain switch must wait for it. The guard
    // retires it on every exit path — the batch loop has several co_returns.
    reorg_participation const build_participation(chain);

    // MTP window. Rebuilt from the active chain whenever the built height moves
    // (a reorg rewinds it), since timestamps carried over from an abandoned
    // branch would persist a wrong median_time_past into the UTXO entries.
    std::deque<uint32_t> timestamp_window;
    auto reload_timestamp_window = [&](uint32_t up_to) {
        timestamp_window.clear();
        // 11 entries, matching what the incremental path keeps (it pops before
        // pushing). A 12th would shift calculate_mtp's median element.
        uint32_t const preload_start = (up_to > 10) ? (up_to - 10) : 0;
        for (uint32_t h = preload_start; h <= up_to && h > 0; ++h) {
            auto hdr = chain.get_header(h);
            if (hdr) {
                timestamp_window.push_back(hdr->timestamp());
            }
        }
    };
    reload_timestamp_window(utxo_built_height);

    auto executor = co_await ::asio::this_coro::executor;

    // Dedicated worker for block parsing + full validation, so that CPU-heavy work
    // does not block this coroutine's executor (download supervision, storage).
    // validate_block_batch fans script verification out to its own threads, so one
    // pool thread to run the serial phase and orchestrate is enough.
    ::asio::thread_pool validation_pool(1);

    // Exit on the node-owned stop signal too, not only chain.stopped(): the chain
    // is stopped in full_node::join(), which runs AFTER run() completes (all sync
    // coroutines exited). Looping on chain.stopped() alone therefore deadlocks
    // shutdown — this task would never exit, so run() never completes, so
    // chain.stop() is never called. should_stop() reflects network.stopped(),
    // the same signal every other sync coroutine uses.
    while ( ! chain.stopped() && ! should_stop()) {
        // A chain switch rewrites the UTXO set, so it must run alone. Park at the
        // barrier between batches — never mid-batch, so the UTXO set is always at
        // a block boundary, which is what disconnect expects.
        if (chain.reorg_pause_requested()) {
            chain.enter_reorg_barrier();
            while (chain.reorg_pause_requested() && ! chain.stopped() && ! should_stop()) {
                ::asio::steady_timer timer(executor);
                timer.expires_after(poll_interval);
                co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            }
            chain.leave_reorg_barrier();

            // The switch rewound the built height and re-pointed the active chain,
            // so re-read the height and rebuild the MTP window from the new chain.
            if (auto const built = chain.get_utxo_built_height(); built) {
                spdlog::info("[utxo_build] Resuming after reorg: built height {} -> {}",
                    utxo_built_height, *built);
                utxo_built_height = *built;
                reload_timestamp_window(utxo_built_height);
            }
            continue;
        }

        uint32_t current_contiguous = contiguous_height.load(std::memory_order_acquire);

        // Blocks stored and contiguous ahead of what we've built.
        uint32_t available = (current_contiguous > utxo_built_height + 1)
            ? current_contiguous - 1 - utxo_built_height
            : 0;

        // Batch length by sync regime (see utxo_batch_len): a full batch_size
        // window during IBD for throughput; once caught up to the tip, drain
        // whatever is available down to a single block so the trailing remainder
        // and each newly mined block are validated promptly. is_stale() keys off
        // the top block's timestamp (a domain property), staying within the module
        // boundary.
        uint32_t const batch_len = utxo_batch_len(available, batch_size, chain.is_stale());

        if (batch_len == 0) {
            // Still in IBD and short of a full batch — wait and poll.
            ::asio::steady_timer timer(executor);
            timer.expires_after(poll_interval);
            co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            continue;
        }

        // Process one batch
        uint32_t batch_start = utxo_built_height + 1;
        uint32_t batch_end = batch_start + batch_len - 1;

        // Never let a batch straddle the checkpoint. Cap it at the checkpoint so
        // the below-checkpoint remainder is built (merkle-only) as its own batch
        // and the UTXO delta of those blocks is applied first. The next batch then
        // starts strictly above the checkpoint and is validated in full against a
        // UTXO-Z that already contains every below-checkpoint output. A straddling
        // batch would instead validate above-checkpoint blocks whose prevouts were
        // created by below-checkpoint blocks in the SAME batch — outputs whose delta
        // is applied only after validation — yielding spurious "prevout not found".
        if (batch_start <= checkpoint_height && checkpoint_height < batch_end) {
            batch_end = checkpoint_height;
        }

        // Above the checkpoint every block's undo data is captured and can only be
        // written after the batch's delta is applied, so the whole batch is held in
        // memory first. Cap the batch there: a full block can spend tens of
        // thousands of outputs, so an unbounded batch would hold hundreds of MB.
        static constexpr uint32_t max_undo_batch_len = 100;
        if (batch_start > checkpoint_height && batch_end - batch_start + 1 > max_undo_batch_len) {
            batch_end = batch_start + max_undo_batch_len - 1;
        }

        // Read raw blocks from flat files
        auto raw_result = chain.fetch_blocks_raw(batch_start, batch_end);
        if ( ! raw_result) {
            spdlog::error("[utxo_build] Failed to fetch blocks {}-{}", batch_start, batch_end);
            // Wait and retry — blocks might not be fully written yet
            ::asio::steady_timer timer(executor);
            timer.expires_after(std::chrono::seconds(1));
            co_await timer.async_wait(::asio::use_awaitable);
            continue;
        }

        // Post-checkpoint FULL validation (scripts / signatures / fees / coinbase
        // value) BEFORE the UTXO delta is applied. At/below the highest checkpoint
        // we keep the fast merkle-only path; strictly above it the whole batch runs
        // full validation. Because a batch never straddles the checkpoint (capped
        // above), a batch that starts above the checkpoint is entirely above it, so
        // every block in it is validated against a UTXO-Z that already holds every
        // below-checkpoint output. On failure we halt: the invalid block is never
        // applied.
        // Kept past the validation step: the mempool update below needs parsed
        // blocks, and above the checkpoint they are parsed here anyway. Below it
        // this stays empty and the raw bytes are used instead.
        std::vector<kth::block_const_ptr> validated_blocks;

        if (batch_start > checkpoint_height) {
            // Parse the blocks and validate them on the worker pool, off this
            // coroutine's executor.
            // A parse failure here is not a consensus failure, and the branch below
            // cannot tell them apart from the code alone. Kept separate so each is
            // reported as what it is.
            bool parse_failed = false;

            auto const vc = co_await ::asio::co_spawn(validation_pool.get_executor(),
                [&]() -> ::asio::awaitable<code> {
                    auto& full_blocks = validated_blocks;
                    full_blocks.reserve(raw_result->size());
                    for (uint32_t i = 0; i < raw_result->size(); ++i) {
                        uint32_t const h = batch_start + i;
                        auto const& raw = (*raw_result)[i];
                        byte_reader reader(byte_span{raw.data(), raw.size()});
                        auto blk = domain::message::block::from_data(reader, 0u);
                        if ( ! blk) {
                            spdlog::critical("[utxo_build] Failed to parse block for validation at height {}", h);
                            parse_failed = true;
                            co_return error::operation_failed;
                        }
                        full_blocks.push_back(
                            std::make_shared<domain::message::block const>(std::move(*blk)));
                    }
                    if (full_blocks.empty()) {
                        co_return error::success;
                    }
                    co_return blockchain::validate_block_batch(
                        chain, chain.chain_settings(), network, full_blocks, batch_start);
                }, ::asio::use_awaitable);

            if (vc) {
                if (parse_failed) {
                    // Bytes this node wrote, and the compact parser accepted when it
                    // stored them. Not being able to read them back is local damage,
                    // not a peer's doing.
                    on_fatal("a stored block could not be parsed for validation");
                    co_return;
                }

                // A block that does not satisfy consensus: a peer sent something
                // invalid. Recoverable in principle — the range wants re-fetching
                // from someone else — but the block is already stored and marked
                // have_data, its peer is not recorded here, and the contiguous
                // height has moved past it, so there is nothing this task can do
                // about it beyond stopping. Recovering properly needs a way back to
                // the coordinator with the failed height, which does not exist yet.
                spdlog::critical("[utxo_build] POST-CHECKPOINT VALIDATION FAILED at {}-{}: {}. "
                    "The UTXO build stops here and nothing re-drives it",
                    batch_start, batch_end, vc.message());
                co_return;
            }
            spdlog::info("[utxo_build] Full validation OK for {}-{}", batch_start, batch_end);
        }

        // Parse and process UTXO delta
        blockchain::utxo_raw_delta delta;
        // Undo records for this batch, persisted only after the delta is applied.
        struct pending_undo_entry {
            blockchain::header_index::index_t idx;
            database::block_undo undo;
            hash_digest prev_hash;
        };
        std::vector<pending_undo_entry> pending_undo;

        for (uint32_t i = 0; i < raw_result->size(); ++i) {
            uint32_t h = batch_start + i;
            uint32_t mtp = calculate_mtp(timestamp_window);

            auto parsed = blockchain::parse_utxo_block(
                byte_span{(*raw_result)[i].data(), (*raw_result)[i].size()});
            if ( ! parsed) {
                spdlog::critical("[utxo_build] Failed to parse block at height {}", h);
                on_fatal("a stored block could not be parsed");
                co_return;
            }

            auto const idx = chain.headers().active_at(static_cast<int32_t>(h));
            if (idx == blockchain::header_index::null_index) {
                spdlog::critical("[utxo_build] Height {} is not on the active chain", h);
                on_fatal("a height being built is not on the active chain");
                co_return;
            }
            // Prune with the bloom only up to the checkpoint; above it keep every
            // output (live UTXO set) so full validation can resolve prevouts.
            auto const* block_bloom = (h <= checkpoint_height) ? bloom_ptr : nullptr;
            auto block_delta_result = blockchain::process_compact_block_utxos(
                *parsed, h, mtp,
                chain.headers().get_file_number(idx),
                chain.headers().get_data_pos(idx),
                block_bloom);
            if ( ! block_delta_result) {
                // The header index has no file number for this block. Storing
                // references built from it would put UINT32_MAX in every entry,
                // so the batch stops here rather than writing entries that can
                // never be resolved.
                spdlog::critical("[utxo_build] cannot reference the block at height {}: "
                    "the header index and the block store disagree", h);
                on_fatal("the header index has no data for a block being built");
                co_return;
            }
            auto block_delta = std::move(*block_delta_result);

            // Capture undo data so this block can be disconnected on a reorg.
            // Only above the checkpoint: below it the bloom prunes the delta, so
            // the block is not disconnectable in principle (and reorgs only ever
            // happen near the tip). Captured from the block's OWN delta, before
            // merge collapses same-batch spends, and before the batch is applied.
            if (h > checkpoint_height) {
                auto undo = blockchain::capture_block_undo(block_delta, delta, chain, h);
                if ( ! undo) {
                    // Nothing is applied yet, so the state is clean — but neither
                    // cause is one to retry into. A read that failed is storage
                    // that is not answering; an output the set does not have,
                    // after this batch has already validated, is the set and the
                    // delta disagreeing. Both are local and both persist.
                    if (undo.error() == database::result_code::key_not_found) {
                        // Here — and not in capture_block_undo, which cannot know
                        // this — the reading is unambiguous: undo data is captured
                        // above the checkpoint, where this batch has already passed
                        // full validation, and that resolves every prevout against
                        // UTXO-Z or against a batch output created at or below the
                        // height being validated. A block cannot spend one from a
                        // later block. So the block is not the problem: the set and
                        // the delta describing it disagree.
                        spdlog::critical("[utxo_build] The UTXO set does not hold an output the "
                            "block at height {} spends, which validation resolved", h);
                        on_fatal("the UTXO set and the delta built from it disagree");
                    } else {
                        spdlog::critical("[utxo_build] Could not read the outputs the block at "
                            "height {} spends", h);
                        on_fatal("the UTXO set could not be read while capturing undo data");
                    }
                    co_return;
                }
                pending_undo.emplace_back(idx, std::move(*undo),
                                          chain.headers().get_prev_block_hash(idx));
            }

            delta.merge(std::move(block_delta));

            // Update MTP window from raw block header (timestamp at offset 68)
            uint32_t block_timestamp;
            std::memcpy(&block_timestamp, (*raw_result)[i].data() + 68, sizeof(block_timestamp));
            if (timestamp_window.size() >= 11) {
                timestamp_window.pop_front();
            }
            timestamp_window.push_back(block_timestamp);
        }

        // Entry closes here, before the first mutation, and the captures already
        // admitted are drained before anything moves. A template that entered
        // earlier finishes on copies it already holds; nothing new may capture
        // stores that are about to change (#621).
        if ( ! chain.begin_transition()) {
            spdlog::critical("[utxo_build] A batch began while another transition was running");
            on_fatal("two chain transitions overlapped");
            co_return;
        }

        // Step 2. Say, durably, that this batch is about to mutate the stores —
        // before it does. Nothing below can be undone: the delta writes the maps
        // in place, so a failure or a crash after this point leaves the set part
        // way with the built height still naming the batch BEFORE. The record is
        // what the next start reads instead of guessing.
        //
        // `intended_last_height` is what the batch is TRYING to reach. It is
        // written here, before the work, so it says what was attempted and never
        // what was achieved.
        auto const operation_id = database::make_operation_id();
        if (auto const recorded = chain.begin_transition_record(
                database::utxo_transition_record{
                    .format_version = database::utxo_transition_record::current_format_version,
                    .type = database::transition_type::connect_batch,
                    .operation_id = operation_id,
                    .first_height = batch_start,
                    .intended_last_height = batch_end,
                    .state = database::transition_state::in_progress});
            recorded != database::result_code::success) {
            spdlog::critical("[utxo_build] Could not record that batch {}-{} is in flight; "
                "refusing to mutate the UTXO set without it", batch_start, batch_end);
            on_fatal("the transition record could not be written");
            co_return;
        }

        // Step 3. The environment is opened MDB_NOSYNC, so the commit above has
        // not reached the disk. Without this the record is decorative: a power
        // cut would erase it while the UTXO-Z mutations it describes — a
        // separate store, with its own barrier — survived.
        if (auto const synced = chain.env_sync();
            synced != database::result_code::success) {
            spdlog::critical("[utxo_build] The transition record for batch {}-{} could not be put "
                "on stable storage; refusing to mutate the UTXO set behind a record that a "
                "restart may not find", batch_start, batch_end);
            on_fatal("the transition record could not be made durable");
            co_return;
        }

        // Step 4. Apply to UTXO-Z. THE FIRST MUTATION: from here the record is
        // the only thing that says this batch was ever started.
        // ONE window for the whole coherent mutation: the inserts, the undo
        // capture that reads under the same capability, the deletions, and the
        // barrier that makes them durable. Opened AFTER the last suspension
        // above and closed before the next, so the capability never lives across
        // a co_await — everything between is synchronous (#649).
        //
        // Per call would keep readers out of the mappings and still admit one
        // between the inserts and the deletions, where the set holds outputs
        // these blocks spent and the transition record does not protect a reader
        // that never consults it.
        // Steps 4 to 9 ONLY. The window dies with this scope, before step 10:
        // the organizer takes validation_mutex_ and then a UTXO read lease inside
        // accept(), so a connect still holding the window when mempool_remove_for_block
        // takes that same mutex would be acquiring the two in the opposite order —
        // the AB-BA deadlock. Nothing below step 9 needs the capability, and the
        // fsyncs of steps 10 and 11 have no business excluding readers.
        {
            auto const window = chain.begin_utxo_write();

            if ( ! delta.empty()) {
                auto result = chain.apply_utxo_inserts_raw(window, delta.inserts);
                if (result != database::result_code::success) {
                    spdlog::critical("[utxo_build] Failed to apply UTXO delta at batch {} "
                        "(operation {:#018x})", batch_start, operation_id);
                    on_fatal("a UTXO delta could not be applied");
                    co_return;
                }
            }

            // Step 5. Persist undo data AFTER the delta is applied and BEFORE the
            // built-height marker advances, so a crash can only leave undo data for a
            // block that is already connected — never a connected block without undo
            // data.
            //
            // Each write reports which rev file it landed in. A batch that crosses a
            // rotation writes into more than one, and the barrier below has to cover
            // every one of them: syncing "the last file" leaves the rest to the page
            // cache, which is the shape this replaced.
            std::vector<int32_t> undo_files;
            undo_files.reserve(pending_undo.size());
            for (auto& entry : pending_undo) {
                auto const file = chain.store_block_undo(entry.idx, entry.undo, entry.prev_hash);
                if ( ! file) {
                    // After the delta: these blocks are in the UTXO set and now cannot be
                    // disconnected, so a later reorganization would have nothing to
                    // reverse them with.
                    spdlog::critical("[utxo_build] Failed to store undo data for index {}", entry.idx);
                    on_fatal("a connected block has no undo data and cannot be disconnected");
                    co_return;
                }
                undo_files.push_back(*file);
            }

            // Step 6. The deletions this batch owes, applied from a batch the task
            // OWNS. They are part of this batch's delta, not work that follows it:
            // until they run, outputs these blocks spent are still in the set. So
            // they come before the state is published, and a failure is fatal rather
            // than logged — a spent output left behind is a double spend the node
            // would accept.
            //
            // ORDER MATTERS, and it used to be wrong: the height marker was
            // persisted first and the deletions applied afterwards. A crash between
            // the two left a marker saying the batch was complete over a set that
            // still held every output these blocks spent, and the restart trusted
            // the marker. The other direction — a crash after the deletions and
            // before the height — is closed by the record written at step 2.
            {
                std::vector<utxoz::deferred_deletion_entry> owed;
                owed.reserve(delta.deletes.size());
                for (auto const& [key, h] : delta.deletes) {
                    owed.emplace_back(key, h);
                }

                // The SAME policy the reorganization runs, from the same function:
                // `erased` retired permanently even when the walk reported a fault,
                // only `unresolved` resent and rebuilt from what came back, a fault
                // with nothing owed still fatal, bounded attempts. Reimplementing it
                // here is how the two drifted apart before.
                //
                // What differs is the tolerance, and only that: strict_absence()
                // says no proven absence is legitimate on this path, because a
                // connect batch nets out anything created and spent inside itself,
                // so every key it asks to delete was in the set.
                constexpr int max_deletion_attempts = 3;
                utxoz::deferred_deletion_entry offender{utxoz::raw_outpoint{}, 0};

                auto const outcome = blockchain::run_deletion_sweep(
                    std::move(owed), blockchain::strict_absence(),
                    [&chain, &window](std::span<utxoz::deferred_deletion_entry const> b) {
                        return chain.utxo_apply_deletes(window, b);
                    },
                    max_deletion_attempts,
                    [&](int attempt, utxoz::deletion_progress const& progress) {
                        if ( ! progress.unresolved.empty() || progress.error) {
                            spdlog::warn("[utxo_build] attempt {} of {} applied {}, proved {} absent, "
                                "left {} owed at batch {}-{}{}", attempt, max_deletion_attempts,
                                progress.erased.size(), progress.absent.size(),
                                progress.unresolved.size(), batch_start, batch_end,
                                progress.error
                                    ? fmt::format(", fault: {}",
                                        database::utxoz_error_name(*progress.error))
                                    : "");
                        }
                    },
                    &offender);

                switch (outcome) {
                    case blockchain::deletion_sweep_outcome::applied:
                        break;

                    case blockchain::deletion_sweep_outcome::absent_unaccounted:
                        spdlog::critical("[utxo_build] {} is proven absent at batch {}-{}: the UTXO "
                            "set does not hold an output these blocks spent",
                            utxoz::outpoint_to_string(offender.key), batch_start, batch_end);
                        on_fatal("a batch spent an output the UTXO set does not hold");
                        co_return;

                    case blockchain::deletion_sweep_outcome::fault_reported:
                        spdlog::critical("[utxo_build] the deletion walk reported a fault at batch "
                            "{}-{} with nothing left unresolved; refusing to publish over a store "
                            "that reported one", batch_start, batch_end);
                        on_fatal("the UTXO store reported a fault while applying deletions");
                        co_return;

                    case blockchain::deletion_sweep_outcome::attempts_exhausted:
                        spdlog::critical("[utxo_build] deletions could not be applied in {} attempts "
                            "at batch {}-{}; the UTXO set still holds outputs these blocks spent",
                            max_deletion_attempts, batch_start, batch_end);
                        on_fatal("the deletions a batch owed could not be applied");
                        co_return;
                }
            }

            utxo_built_height = batch_end;

            // Steps 7 and 8. Every rev file this batch wrote into, then the
            // directory entries naming them. Contents and names are two barriers: a
            // newly created rev*.dat can have every byte on the platter and still
            // not exist after a power cut, because the entry that reaches it was
            // never written. Both live inside flush_undo.
            if (auto const flushed = chain.flush_undo(undo_files); ! flushed) {
                if (flushed.error().file_number < 0) {
                    spdlog::critical("[utxo_build] The undo directory could not be put on stable "
                        "storage after batch {}-{}: the rev files this batch wrote may not survive "
                        "a restart, so these blocks would be connected and not disconnectable",
                        batch_start, batch_end);
                } else {
                    spdlog::critical("[utxo_build] The undo records in rev file {} could not be put "
                        "on stable storage after batch {}-{}: these blocks would be connected and "
                        "not disconnectable", flushed.error().file_number, batch_start, batch_end);
                }
                on_fatal("the undo records of a connected batch could not be made durable");
                co_return;
            }

            // Step 9. UTXO-Z's own barrier. `close()` does not run it, so without
            // this the set's mutations are the one part of the transition still in
            // the page cache when the record is cleared.
            //
            // `unsupported` is not a failure and not a guarantee either: it is the
            // documented answer where the platform has no barrier at all, and the
            // node's own durability level already says so. `failed` is fatal on
            // every platform — a level describes what a platform CAN promise, and it
            // never turns a barrier that was attempted and refused into a success.
            switch (chain.utxo_sync(window)) {
                case database::barrier_outcome::crossed:
                    break;
                case database::barrier_outcome::unsupported:
                    if (chain.durability() != database::durability_level::none) {
                        spdlog::critical("[utxo_build] The UTXO store reports no durability barrier "
                            "while this node claims '{}'; the two disagree about the same machine",
                            database::to_string(chain.durability()));
                        on_fatal("the UTXO store and the node disagree about what this platform can promise");
                        co_return;
                    }
                    spdlog::warn("[utxo_build] This platform exposes no durability barrier; batch "
                        "{}-{} is published without one", batch_start, batch_end);
                    break;
                case database::barrier_outcome::failed:
                    spdlog::critical("[utxo_build] The UTXO store's durability barrier failed after "
                        "batch {}-{} (operation {:#018x}); what it applied is not known to be on "
                        "disk", batch_start, batch_end, operation_id);
                    on_fatal("the UTXO set of a connected batch could not be made durable");
                    co_return;
            }
        }   // the window ends HERE, before publish_transition

        // Step 10. The built height AND the clearing of the record, in ONE
        // transaction. Separately there is an instant where the height says
        // "arrived" and the record says "clean" independently, and that instant
        // is the whole reason the record exists.
        //
        // Not ignorable: everything below treats these blocks as connected, and
        // on the next start the built height is what says how far the UTXO set
        // reaches. A set that moved without its height would be rebuilt over on
        // resume, applying deltas that are already in it.
        // BOTH heights, and the same value (#653). `last_block_height` is the
        // CONNECTED tip — what publish_chain_view builds state at, and what
        // disconnect_block refuses to rewind past — so the batch that just
        // connected these blocks is the only thing entitled to move it. It was
        // published empty here and written instead by the storage task with the
        // height of what had merely been DOWNLOADED, which is how a stopped node
        // ended up claiming a connected tip 952 blocks beyond its own UTXO set.
        //
        // Published after step 9's barrier and inside the same transaction as the
        // record, so the marker cannot outrun the data it describes: either both
        // heights and the cleared record commit, or none of them do.
        if (auto const published = chain.publish_transition(
                database::transition_heights{
                    .last_block_height = utxo_built_height,
                    .utxo_built_height = utxo_built_height});
            published != database::result_code::success) {
            spdlog::critical("[utxo_build] Could not publish batch {}-{} at built height {}: the "
                "UTXO set is ahead of what the next start will believe",
                batch_start, batch_end, utxo_built_height);
            on_fatal("the UTXO set advanced past the height marker that describes it");
            co_return;
        }

        // Step 11. And that transaction onto the disk, for the same reason step
        // 3 exists. Until this returns, a restart can still find the record.
        if (auto const synced = chain.env_sync();
            synced != database::result_code::success) {
            spdlog::critical("[utxo_build] The published state of batch {}-{} could not be put on "
                "stable storage; a restart may find this batch still in flight over a set that "
                "already holds it", batch_start, batch_end);
            on_fatal("a published transition could not be made durable");
            co_return;
        }

        // These blocks are connected now — the delta is applied, the undo records
        // are written and the marker has moved — so what they confirmed leaves the
        // mempool, along with anything pooled that conflicts with them.
        //
        // Through the chain rather than the mempool directly: the mempool's writes
        // are serialized by the validation mutex, which is not this task's to take.
        // Above the checkpoint the blocks are already parsed by validation; below
        // it the raw form parses only if there is something in the pool to remove.
        for (uint32_t i = 0; i < raw_result->size(); ++i) {
            if (i < validated_blocks.size()) {
                chain.mempool_remove_for_block(*validated_blocks[i]);
                continue;
            }
            auto const& raw = (*raw_result)[i];
            if (auto const ec = chain.mempool_remove_for_block(
                    byte_span{raw.data(), raw.size()}); ec) {
                // These bytes came off disk after the compact parser accepted
                // them, so failing here should not be reachable. If it is, the
                // pool now holds transactions this block confirmed and will keep
                // offering them for a template — carrying on would mine them.
                spdlog::critical("[utxo_build] Could not update the mempool for the block at "
                    "height {}: {}", batch_start + i, ec.message());
                on_fatal("the mempool still holds transactions a connected block confirmed");
                co_return;
            }
        }
        // Now the batch is closed: the delta is applied in full, the undo records
        // are written, the height marker has moved and the mempool no longer
        // holds what these blocks confirmed. Only now does a coherent state exist
        // to describe, and publishing it is what makes the chain state advance —
        // it used to be computed once at startup and left behind (#605).
        //
        // `batch_end` is the last height actually applied: it is clamped to the
        // checkpoint and to the undo batch limit above, so it is the tip and not
        // the range that was asked for.
        if (auto const ec = chain.publish_chain_view(batch_end); ec) {
            spdlog::critical("[utxo_build] Could not publish the chain state at height {}: {}",
                batch_end, ec.message());
            // Deliberately not reopened: the coherent state was never published,
            // so there is nothing for a capture to be coherent with. The gate
            // stays shut while the node winds down.
            on_fatal("a connected batch could not be described by a chain state");
            co_return;
        }

        // Earned, not unwound.
        chain.end_transition();

        spdlog::info("[utxo_build] UTXO built to height {}/{}", utxo_built_height, current_contiguous - 1);
    }


    // Final compaction
    spdlog::info("[utxo_build] Running final compaction...");
    if ( ! chain.utxo_compact()) {
        // Not fatal: compaction reclaims space, it does not decide correctness,
        // and the set is coherent either way. But it is not nothing, and the old
        // void signature meant nobody could tell it had happened.
        spdlog::error("[utxo_build] final compaction failed; the UTXO set is intact "
            "but was not compacted");
    }
    chain.utxo_print_statistics();
    chain.utxo_print_sizing_report();
    chain.utxo_print_height_range_stats();

    spdlog::info("[utxo_build] Task ended at UTXO height {}", utxo_built_height);
}

} // namespace kth::node::sync
