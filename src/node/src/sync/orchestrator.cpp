// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/orchestrator.hpp>

#include <chrono>

#include <boost/unordered/unordered_flat_set.hpp>
#include <spdlog/spdlog.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/node/sync/header_tasks.hpp>
#include <kth/node/sync/block_tasks.hpp>

namespace kth::node::sync {

using namespace ::asio::experimental::awaitable_operators;

// =============================================================================
// Error Classification
// =============================================================================
// Determines if an error indicates the peer sent invalid data (ban) vs
// network/timeout issues (don't ban).

[[nodiscard]]
static bool is_bannable_error(code const& ec) {
    if (!ec) return false;

    // Network errors - DON'T ban (not malicious, just connectivity issues)
    switch (ec.value()) {
        case error::success:
        case error::service_stopped:
        case error::operation_failed:
        case error::resolve_failed:
        case error::network_unreachable:
        case error::address_in_use:
        case error::listen_failed:
        case error::accept_failed:
        case error::bad_stream:
        case error::channel_timeout:
        case error::address_blocked:
        case error::channel_stopped:
        case error::peer_throttling:
        case error::not_found:              // Data not available yet
        case error::orphan_block:           // Just out of order, not invalid
        case error::orphan_transaction:
            return false;
        default:
            // Any other error is a validation failure -> BAN
            return true;
    }
}

// =============================================================================
// Header Persistence (background task)
// =============================================================================

static
::asio::awaitable<void> persist_headers_to_db(
    blockchain::block_chain& chain,
    blockchain::header_index const& index,
    uint32_t start_height,
    uint32_t end_height
) {
    if (start_height > end_height) {
        co_return;
    }

    auto const total = end_height - start_height + 1;
    spdlog::info("[header_persist] Starting: {} headers ({} to {})", total, start_height, end_height);

    auto const start_time = std::chrono::steady_clock::now();
    constexpr size_t batch_size = 1000;

    size_t persisted = 0;
    uint32_t height = start_height;

    while (height <= end_height) {
        domain::chain::header::list batch;
        auto const batch_end = std::min(height + uint32_t(batch_size) - 1, end_height);
        batch.reserve(batch_end - height + 1);

        for (uint32_t h = height; h <= batch_end; ++h) {
            batch.push_back(index.get_header(h));
        }

        auto const ec = chain.organize_headers_batch(batch, height);
        if (ec) {
            spdlog::warn("[header_persist] Failed at height {}: {}", height, ec.message());
            co_return;
        }

        persisted += batch.size();
        height = batch_end + 1;

        // Log progress every 100000 headers
        if (persisted % 100000 < batch_size) {
            auto const elapsed = std::chrono::steady_clock::now() - start_time;
            auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            auto const rate = elapsed_secs > 0 ? persisted / elapsed_secs : persisted;
            spdlog::info("[header_persist] {}/{} ({}/s)", persisted, total, rate);
        }

        // Yield to allow other coroutines to run
        co_await ::asio::post(co_await ::asio::this_coro::executor, ::asio::use_awaitable);
    }

    auto const elapsed = std::chrono::steady_clock::now() - start_time;
    auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    auto const rate = elapsed_secs > 0 ? persisted / elapsed_secs : persisted;
    spdlog::info("[header_persist] Complete: {} headers in {}s ({}/s)", persisted, elapsed_secs, rate);
}

// =============================================================================
// Sync Orchestrator
// =============================================================================

::asio::awaitable<void> sync_orchestrator(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    network::p2p_node& network
) {
    auto executor = co_await ::asio::this_coro::executor;

    spdlog::info("[sync_orchestrator] Starting CSP-based sync system");

    // =========================================================================
    // CHANNELS - The ONLY way tasks communicate
    // =========================================================================
    //
    // TODO(fernando): Refactor channel architecture. Currently header_download
    // communicates with 3 entities (peer_provider, sync_coordinator, header_validation)
    // via multiple channels. Channels should connect exactly 2 entities.
    //
    // Proposed architecture:
    //   - header_download ↔ header_manager (single bidirectional channel)
    //   - header_manager ↔ peer_provider (for peer issues)
    //   - header_manager ↔ header_validation (for validation)
    //   - header_manager ↔ sync_coordinator (for orchestration)
    //
    // This would make the data flow cleaner and avoid priority issues between
    // multiple input channels.
    // =========================================================================

    // Header pipeline - single input channels (CSP pattern)
    header_download_input_channel header_download_input(executor, 100);
    header_download_output_channel header_download_output(executor, 1000);  // task output: headers + failure + performance
    header_validation_input_channel header_validation_input(executor, 100);
    header_validated_channel validated_headers(executor, 100);

    // Block pipeline - single input channels (CSP pattern)
    block_download_input_channel block_download_input(executor, 100);
    block_download_channel downloaded_blocks(executor, 1000);  // Buffer for out-of-order blocks
    block_validation_input_channel block_validation_input(executor, 1000);
    block_validated_channel validated_blocks(executor, 100);

    // Control
    stop_channel stop_signal(executor, 1);

    // Sync coordinator unified event channel (created here so peer_provider can close it during shutdown)
    sync_coordinator_event_channel coordinator_events(executor, 100);

    // =========================================================================
    // TASKS - All independent, communicate only via channels
    // =========================================================================

    task_group all_tasks(executor);

    // -------------------------------------------------------------------------
    // 1. Peer provider - receives connected peers from network and distributes
    // -------------------------------------------------------------------------
    // Unified input channel for peer_provider (CSP pattern - single channel)
    peer_provider_input_channel peer_provider_input(executor, 100);

    // Bridge task: forwards from network.connected_peers() to unified channel
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[peer_bridge] Started, forwarding peers to peer_provider");
        auto exec = co_await ::asio::this_coro::executor;
        ::asio::steady_timer check_timer(exec);

        try {
            while (!network.stopped()) {
                // Use timer to periodically check if network stopped
                check_timer.expires_after(std::chrono::milliseconds(500));
                auto event = co_await (
                    network.connected_peers().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
                    check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
                );

                // Timer fired - just loop to check network.stopped()
                if (event.index() == 1) {
                    continue;
                }

                auto [ec, peer] = std::get<0>(event);
                if (ec) {
                    spdlog::debug("[peer_bridge] Network channel closed: {}", ec.message());
                    break;
                }
                if (!peer_provider_input.try_send(std::error_code{}, new_peer{peer})) {
                    spdlog::warn("[peer_bridge] Channel full, new_peer dropped for {}", peer->authority_with_agent());
                }
            }
        } catch (std::exception const& e) {
            spdlog::error("[peer_bridge] Exception: {}", e.what());
        }
        // Cancel timer before exiting to ensure clean shutdown of || operator internals
        check_timer.cancel();
        spdlog::debug("[peer_bridge] Ended");
    });

    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::info("[peer_provider] Task started, waiting for peers...");

        // Maintain full list of connected peers - single source of truth
        std::vector<network::peer_session::ptr> all_peers;

        // Peers that had issues during header sync (by nonce) - don't use for headers
        boost::unordered_flat_set<uint64_t> header_bad_peers;

        // Performance tracking per peer (by nonce)
        struct peer_stats {
            uint64_t total_blocks{0};
            uint64_t total_time_ms{0};
            uint32_t sample_count{0};

            double avg_ms_per_block() const {
                return total_blocks > 0 ? double(total_time_ms) / double(total_blocks) : 0.0;
            }
        };
        boost::unordered_flat_map<uint64_t, peer_stats> peer_performance_stats;
        constexpr uint32_t min_samples_for_eviction = 5;  // Need at least 5 chunks before evaluating
        constexpr size_t max_peers = 8;

        // Helper to clean stopped peers and send updated list
        auto broadcast_peers = [&]() {
            // Remove stopped peers and clean their stats
            std::erase_if(all_peers, [&](auto const& p) {
                if (p->stopped()) {
                    peer_performance_stats.erase(p->nonce());
                    return true;
                }
                return false;
            });

            // For headers: filter out bad peers
            std::vector<network::peer_session::ptr> header_peers;
            header_peers.reserve(all_peers.size());
            for (auto const& p : all_peers) {
                if (!header_bad_peers.contains(p->nonce())) {
                    header_peers.push_back(p);
                } else {
                    spdlog::info("[peer_provider] Filtering peer {} (nonce={}) - in bad list",
                        p->authority_with_agent(), p->nonce());
                }
            }

            spdlog::info("[peer_provider] Broadcasting {} peers to sync tasks (all={}, bad_list={})",
                header_peers.size(), all_peers.size(), header_bad_peers.size());

            // Send filtered list to headers, full list to blocks (via unified input channels)
            if (!header_download_input.try_send(std::error_code{}, peers_updated{header_peers})) {
                spdlog::warn("[peer_provider] Channel full, peers_updated dropped for header_download");
            }
            if (!block_download_input.try_send(std::error_code{}, peers_updated{all_peers})) {
                spdlog::warn("[peer_provider] Channel full, peers_updated dropped for block_download");
            }
        };

        auto exec = co_await ::asio::this_coro::executor;
        ::asio::steady_timer check_timer(exec);

        try {
            // Single channel, FIFO processing - check network.stopped() periodically
            while (!network.stopped()) {
                // Use timer to periodically check if network stopped
                check_timer.expires_after(std::chrono::milliseconds(500));
                auto event = co_await (
                    peer_provider_input.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
                    check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
                );

                // Timer fired - just loop to check network.stopped()
                if (event.index() == 1) {
                    continue;
                }

                auto [ec, msg] = std::get<0>(event);
                if (ec) {
                    spdlog::debug("[peer_provider] Input channel closed: {}", ec.message());
                    break;
                }
                auto& event_msg = msg;

                if (auto* np = std::get_if<new_peer>(&event_msg)) {
                    // New peer from network
                    if (np->peer->stopped()) continue;

                    spdlog::info("[peer_provider] New peer connected: {} (nonce={})",
                        np->peer->authority_with_agent(), np->peer->nonce());
                    all_peers.push_back(np->peer);
                    broadcast_peers();
                } else if (auto* err = std::get_if<peer_error>(&event_msg)) {
                    // Peer error report - decide action based on error type
                    spdlog::info("[peer_provider] Peer {} (nonce={}) error: {}",
                        err->peer->authority_with_agent(), err->peer->nonce(), err->error.message());

                    // Always exclude from header sync
                    header_bad_peers.insert(err->peer->nonce());

                    if (is_bannable_error(err->error)) {
                        // Invalid data = instant ban (1 year)
                        spdlog::warn("[peer_provider] Banning peer {} for invalid data: {}",
                            err->peer->authority_with_agent(), err->error.message());

                        // Remove from all_peers
                        std::erase_if(all_peers, [&](auto const& p) {
                            return p->nonce() == err->peer->nonce();
                        });

                        err->peer->misbehave(network::peer_session::misbehavior_invalid_data);
                        network.ban_peer(err->peer, std::chrono::hours{24 * 365}, network::ban_reason::node_misbehaving);
                    } else {
                        // Network error (timeout, etc.) = accumulate misbehavior score
                        if (err->peer->misbehave(network::peer_session::misbehavior_timeout)) {
                            spdlog::warn("[peer_provider] Peer {} reached misbehavior threshold, banning",
                                err->peer->authority_with_agent());
                            network.ban_peer(err->peer, std::chrono::hours{24}, network::ban_reason::node_misbehaving);
                        }
                    }

                    // Broadcast updated (filtered) list
                    broadcast_peers();
                } else if (auto* perf = std::get_if<peer_performance>(&event_msg)) {
                    // Update performance stats for this peer (block downloads)
                    auto& stats = peer_performance_stats[perf->peer_nonce];
                    stats.total_blocks += perf->blocks_downloaded;
                    stats.total_time_ms += perf->download_time_ms;
                    stats.sample_count++;

                    // Check if we should evict slow peer (only when at max capacity)
                    if (all_peers.size() >= max_peers) {
                        // Collect peers with enough samples
                        std::vector<std::pair<uint64_t, double>> peer_avgs;
                        for (auto const& [nonce, pstats] : peer_performance_stats) {
                            if (pstats.sample_count >= min_samples_for_eviction) {
                                peer_avgs.emplace_back(nonce, pstats.avg_ms_per_block());
                            }
                        }

                        // Need at least 3 peers with samples to make a fair comparison
                        if (peer_avgs.size() >= 3) {
                            // Sort by avg time
                            std::sort(peer_avgs.begin(), peer_avgs.end(),
                                [](auto const& a, auto const& b) { return a.second < b.second; });

                            // Calculate median
                            double median_avg = peer_avgs[peer_avgs.size() / 2].second;

                            // Get slowest
                            auto const& slowest = peer_avgs.back();
                            uint64_t slowest_nonce = slowest.first;
                            double slowest_avg = slowest.second;

                            // Only evict if slowest is significantly worse (2x slower than median)
                            constexpr double eviction_threshold = 2.0;
                            if (slowest_avg > median_avg * eviction_threshold) {
                                // Find the peer and disconnect (not ban)
                                for (auto it = all_peers.begin(); it != all_peers.end(); ++it) {
                                    if ((*it)->nonce() == slowest_nonce) {
                                        spdlog::info("[peer_provider] Evicting slow peer {} "
                                            "(avg {:.1f}ms/block, median {:.1f}ms/block, {:.1f}x slower)",
                                            (*it)->authority_with_agent(), slowest_avg, median_avg,
                                            slowest_avg / median_avg);
                                        (*it)->stop(error::channel_stopped);  // Graceful disconnect
                                        peer_performance_stats.erase(slowest_nonce);
                                        all_peers.erase(it);
                                        broadcast_peers();
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } else if (auto* hperf = std::get_if<header_performance>(&event_msg)) {
                    // Header download performance - log but don't use for eviction
                    // Header sync is too fast to reliably measure peer quality
                    spdlog::debug("[peer_provider] Header perf: peer={}, headers={}, time={}ms",
                        hperf->peer_nonce, hperf->headers_downloaded, hperf->download_time_ms);
                }
            }
        } catch (std::exception const& e) {
            spdlog::error("[peer_provider] Exception: {}", e.what());
        }

        // Cancel timer before any co_await to ensure clean shutdown of || operator internals
        check_timer.cancel();

        // Network stopped - close channels FIRST to unblock any pending sends,
        // then send stop signals. Closing channels will wake up blocked tasks immediately.
        // NOTE: Only close channels that are NOT read with || operator + timer.
        // Channels using || (stop_signal) must NOT be closed here to avoid SEGV.
        spdlog::info("[peer_provider:shutdown] Closing all data channels...");
        header_download_input.close();
        header_download_output.close();
        header_validation_input.close();
        validated_headers.close();
        block_download_input.close();
        downloaded_blocks.close();
        block_validation_input.close();
        validated_blocks.close();
        coordinator_events.close();
        // NOTE: Do NOT close stop_signal - coordinator:stop_bridge uses || with timer

        spdlog::info("[peer_provider:shutdown] All channels closed, task ending");
        spdlog::debug("[peer_provider] Task ended");
    });

    // -------------------------------------------------------------------------
    // 2. Header download task (1 input, 1 output)
    //    Sequential header download with sticky peer selection
    // -------------------------------------------------------------------------
    all_tasks.spawn(header_download_task(
        header_download_input,
        header_download_output
    ));

    // -------------------------------------------------------------------------
    // 3. Header download output bridge (demultiplexes headers, failures, and performance)
    //    - downloaded_headers → header_validation_input
    //    - peer_failure_report → header_validation_input
    //    - header_performance → peer_provider_input
    // -------------------------------------------------------------------------
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[header_bridge] Started, demultiplexing download output");
        try {
            while (true) {
                auto [ec, msg] = co_await header_download_output.async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                if (ec) {
                    spdlog::debug("[header_bridge] Download channel closed: {}", ec.message());
                    break;
                }

                if (auto* hdrs = std::get_if<downloaded_headers>(&msg)) {
                    // Forward to validation
                    if (!header_validation_input.try_send(std::error_code{}, std::move(*hdrs))) {
                        spdlog::warn("[header_bridge] Channel full, headers dropped");
                        break;
                    }
                } else if (auto* failure = std::get_if<peer_failure_report>(&msg)) {
                    // Forward failure report to validation (so it can forward to coordinator)
                    if (!header_validation_input.try_send(std::error_code{}, std::move(*failure))) {
                        spdlog::warn("[header_bridge] Channel full, failure report dropped");
                        break;
                    }
                } else if (auto* perf = std::get_if<header_performance>(&msg)) {
                    // Forward performance stats to peer_provider
                    if (!peer_provider_input.try_send(std::error_code{}, *perf)) {
                        spdlog::debug("[header_bridge] Channel full, performance stats dropped");
                    }
                }
            }
        } catch (std::exception const& e) {
            spdlog::error("[header_bridge] Exception: {}", e.what());
        }
        spdlog::debug("[header_bridge] Ended");
    });

    // -------------------------------------------------------------------------
    // 4. Header validation task (1 input, 1 output)
    // -------------------------------------------------------------------------
    uint32_t const initial_header_height = uint32_t(organizer.header_height());
    all_tasks.spawn(header_validation_task(
        organizer,
        header_validation_input,
        validated_headers
    ));

    // -------------------------------------------------------------------------
    // 5. Block download supervisor (1 input, 1 output)
    // -------------------------------------------------------------------------
    all_tasks.spawn(block_download_supervisor(
        block_download_input,
        downloaded_blocks,
        organizer
    ));

    // -------------------------------------------------------------------------
    // 6. Block supervisor output bridge (demultiplexes blocks and performance)
    //    - downloaded_block → block_validation_input
    //    - peer_performance → peer_provider_input
    // -------------------------------------------------------------------------
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[block_bridge] Started, demultiplexing supervisor output");
        try {
            while (true) {
                auto [ec, msg] = co_await downloaded_blocks.async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                if (ec) {
                    spdlog::debug("[block_bridge] Download channel closed: {}", ec.message());
                    break;
                }

                if (auto* block = std::get_if<downloaded_block>(&msg)) {
                    // Track received for pipeline debugging
                    g_blocks_received_by_bridge.fetch_add(1, std::memory_order_relaxed);

                    // Forward to validation
                    if (!block_validation_input.try_send(std::error_code{}, std::move(*block))) {
                        spdlog::warn("[block_bridge] Channel full, block {} dropped", block->height);
                        break;
                    }
                    g_blocks_forwarded_by_bridge.fetch_add(1, std::memory_order_relaxed);
                } else if (auto* perf = std::get_if<peer_performance>(&msg)) {
                    // Forward performance stats to peer_provider
                    if (!peer_provider_input.try_send(std::error_code{}, *perf)) {
                        spdlog::debug("[block_bridge] Channel full, performance stats dropped");
                    }
                }
            }
        } catch (std::exception const& e) {
            spdlog::error("[block_bridge] Exception: {}", e.what());
        }
        spdlog::debug("[block_bridge] Ended");
    });

    // -------------------------------------------------------------------------
    // 7. Block validation task (1 input, 1 output)
    // -------------------------------------------------------------------------
    auto heights_result = co_await chain.fetch_last_height();
    uint32_t const initial_block_height = heights_result
        ? heights_result->block
        : 0;

    // Get checkpoint height for fast IBD
    uint32_t const checkpoint_height = uint32_t(chain.chain_settings().max_checkpoint_height);
    spdlog::info("[sync_orchestrator] Fast IBD mode up to checkpoint height {}", checkpoint_height);

    all_tasks.spawn(block_validation_task(
        chain,
        block_validation_input,
        validated_blocks,
        initial_block_height + 1,
        checkpoint_height
    ));

    // -------------------------------------------------------------------------
    // 8. Sync coordinator - orchestrates the sync flow
    // Uses unified event channel to avoid || operator between multiple channels
    // -------------------------------------------------------------------------

    // Bridge: stop_signal -> coordinator_events
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[coordinator:stop_bridge] Started");
        auto exec = co_await ::asio::this_coro::executor;
        ::asio::steady_timer check_timer(exec);

        // stop_signal is an event channel, we need to check it periodically
        while (!network.stopped()) {
            check_timer.expires_after(std::chrono::milliseconds(100));
            auto event = co_await (
                stop_signal.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
                check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
            );

            if (event.index() == 0) {
                // Stop signal received
                auto [ec] = std::get<0>(event);
                if (!ec) {
                    if (!coordinator_events.try_send(std::error_code{}, stop_request{})) {
                        spdlog::warn("[coordinator:stop_bridge] Channel full, stop_request dropped");
                    }
                }
                break;
            }
            // Timer fired - loop to check network.stopped()
        }
        // Cancel timer before exiting to ensure clean shutdown of || operator internals
        check_timer.cancel();
        spdlog::debug("[coordinator:stop_bridge] Ended");
    });

    // Bridge: validated_headers -> coordinator_events
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[coordinator:headers_bridge] Started");
        while (true) {
            auto [ec, result] = co_await validated_headers.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[coordinator:headers_bridge] Channel closed: {}", ec.message());
                break;
            }
            if (!coordinator_events.try_send(std::error_code{}, result)) {
                spdlog::warn("[coordinator:headers_bridge] Channel full, headers_validated dropped");
                break;
            }
        }
        spdlog::debug("[coordinator:headers_bridge] Ended");
    });

    // Bridge: validated_blocks -> coordinator_events
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[coordinator:blocks_bridge] Started");
        while (true) {
            auto [ec, result] = co_await validated_blocks.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[coordinator:blocks_bridge] Channel closed: {}", ec.message());
                break;
            }
            if (!coordinator_events.try_send(std::error_code{}, result)) {
                spdlog::warn("[coordinator:blocks_bridge] Channel full, block_validated dropped");
                break;
            }
        }
        spdlog::debug("[coordinator:blocks_bridge] Ended");
    });

    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[sync_coordinator] Task started");

        uint32_t blocks_synced_to = initial_block_height;
        uint32_t headers_synced_to = initial_header_height;
        bool header_sync_complete = false;

        // For ETA calculation
        auto header_sync_start = std::chrono::steady_clock::now();
        uint32_t headers_at_start = headers_synced_to;

        spdlog::info("[sync_coordinator] Initial state: blocks={}, headers={}",
            blocks_synced_to, headers_synced_to);

        auto exec = co_await ::asio::this_coro::executor;

        // Trigger parallel header sync (supervisor manages chunk coordination)
        auto from_hash = organizer.index().get_hash(
            static_cast<blockchain::header_index::index_t>(headers_synced_to));

        spdlog::debug("[sync_coordinator] Starting parallel header sync from height {}", headers_synced_to);
        if (!header_download_input.try_send(std::error_code{}, header_request{
            .from_height = headers_synced_to,
            .from_hash = from_hash
        })) {
            spdlog::warn("[sync_coordinator] Channel full, initial header_request dropped");
        }

        // Main loop: ONLY receives from unified channel (no || operator)
        while (true) {
            spdlog::debug("[sync_coordinator] Waiting for events...");
            auto [ec, event] = co_await coordinator_events.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));

            if (ec) {
                spdlog::debug("[sync_coordinator] Events channel closed");
                break;
            }

            // Process event based on variant type (FIFO order guaranteed)
            if (std::holds_alternative<stop_request>(event)) {
                spdlog::debug("[sync_coordinator] Stop signal received");
                break;
            }

            if (auto* result = std::get_if<headers_validated>(&event)) {
                spdlog::debug("[sync_coordinator] Received: height={}, count={}, result={}",
                    result->height, result->count, result->result ? result->result.message() : "ok");

                if (result->result) {
                    // Header validation failed - normal network behavior (peer on wrong chain)
                    spdlog::debug("[sync_coordinator] Header validation failed: {} from peer {}",
                        result->result.message(),
                        result->source_peer ? result->source_peer->authority_with_agent() : "unknown");

                    // Update progress if some headers were added before the failure
                    if (result->count > 0) {
                        headers_synced_to = result->height;
                        spdlog::debug("[sync_coordinator] Headers synced to {} before failure", headers_synced_to);
                    }

                    // Report error to peer_provider - it decides what to do (ban, exclude, etc.)
                    if (result->source_peer) {
                        if (!peer_provider_input.try_send(std::error_code{}, peer_error{
                            .peer = result->source_peer,
                            .error = result->result
                        })) {
                            spdlog::warn("[sync_coordinator] Channel full, peer_error dropped for header validation");
                        }
                    }

                    // Retry header sync with a different peer
                    auto retry_hash = organizer.index().get_hash(
                        static_cast<blockchain::header_index::index_t>(headers_synced_to));

                    spdlog::info("[sync_coordinator] Retrying header sync from height {} with different peer",
                        headers_synced_to);

                    if (!header_download_input.try_send(std::error_code{}, header_request{
                        .from_height = headers_synced_to,
                        .from_hash = retry_hash
                    })) {
                        spdlog::warn("[sync_coordinator] Channel full, retry header_request dropped");
                    }
                } else if (result->count > 0) {
                    // Sequential header sync - track progress and request next batch
                    auto const prev_headers_synced = headers_synced_to;
                    headers_synced_to = result->height;

                    // Log progress when crossing 10000 boundaries (handles any batch size)
                    uint32_t prev_10k = prev_headers_synced / 10000;
                    uint32_t curr_10k = headers_synced_to / 10000;
                    if (curr_10k > prev_10k) {
                        auto const headers_downloaded = headers_synced_to - headers_at_start;
                        auto const elapsed = std::chrono::steady_clock::now() - header_sync_start;
                        auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                        auto const rate = elapsed_secs > 0 ? headers_downloaded / elapsed_secs : 0;

                        spdlog::info("[header_sync] height {} ({}/s)", headers_synced_to, rate);
                    }

                    // Request next batch of headers (sequential sync)
                    auto next_hash = organizer.index().get_hash(
                        static_cast<blockchain::header_index::index_t>(headers_synced_to));

                    if (!header_download_input.try_send(std::error_code{}, header_request{
                        .from_height = headers_synced_to,
                        .from_hash = next_hash
                    })) {
                        spdlog::warn("[sync_coordinator] Channel full, next batch header_request dropped");
                    }
                } else if (result->count == 0 && !header_sync_complete) {
                    // Tip reached - header sync complete (guard against duplicate events)
                    header_sync_complete = true;

                    auto const elapsed = std::chrono::steady_clock::now() - header_sync_start;
                    auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                    auto const total_headers = headers_synced_to - headers_at_start;
                    auto const rate = elapsed_secs > 0 ? total_headers / elapsed_secs : total_headers;

                    spdlog::info("[sync_coordinator] Header sync COMPLETE: {} headers in {}s ({}/s)",
                        total_headers, elapsed_secs, rate);

                    // Spawn background task to persist headers to DB
                    if (headers_synced_to > initial_header_height) {
                        ::asio::co_spawn(exec,
                            persist_headers_to_db(chain, organizer.index(),
                                initial_header_height + 1, headers_synced_to),
                            ::asio::detached);
                    }

                    // Trigger block download if we have headers ahead of blocks
                    if (headers_synced_to > blocks_synced_to) {
                        // Determine end height based on sync stage
                        uint32_t end_height;
                        if (blocks_synced_to < checkpoint_height) {
                            // Fast sync stage: only download up to checkpoint
                            end_height = checkpoint_height;
                            spdlog::info("[sync_coordinator] Starting FAST block sync: {} to {} ({} blocks)",
                                blocks_synced_to + 1, end_height,
                                end_height - blocks_synced_to);
                        } else {
                            // Slow sync stage: download up to headers (requires UTXO)
                            end_height = headers_synced_to;
                            spdlog::info("[sync_coordinator] Starting SLOW block sync: {} to {} ({} blocks)",
                                blocks_synced_to + 1, end_height,
                                end_height - blocks_synced_to);
                        }

                        if (!block_download_input.try_send(std::error_code{}, block_range_request{
                            .start_height = blocks_synced_to + 1,
                            .end_height = end_height
                        })) {
                            spdlog::warn("[sync_coordinator] Channel full, block_range_request dropped");
                        }
                    } else {
                        // Already synced - wait and check for new blocks later
                        spdlog::info("[sync_coordinator] Fully synced at height {}", blocks_synced_to);
                        ::asio::steady_timer timer(exec);
                        timer.expires_after(std::chrono::seconds(10));
                        auto [timer_ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
                        if (timer_ec || network.stopped()) break;

                        // Reset for new sync cycle - start parallel header sync again
                        header_sync_complete = false;
                        header_sync_start = std::chrono::steady_clock::now();
                        headers_at_start = headers_synced_to;

                        auto next_hash = organizer.index().get_hash(
                            static_cast<blockchain::header_index::index_t>(headers_synced_to));

                        if (!header_download_input.try_send(std::error_code{}, header_request{
                            .from_height = headers_synced_to,
                            .from_hash = next_hash
                        })) {
                            spdlog::warn("[sync_coordinator] Channel full, new cycle header_request dropped");
                        }
                    }
                }
                continue;
            }

            if (auto* result = std::get_if<block_validated>(&event)) {
                if (!result->result) {
                    blocks_synced_to = result->height;

                    // Check if fast sync stage is complete
                    if (blocks_synced_to == checkpoint_height) {
                        spdlog::info("[sync_coordinator] *** FAST SYNC COMPLETE at checkpoint {} ***",
                            checkpoint_height);
                        spdlog::info("[sync_coordinator] TODO: Trigger UTXO build stage");
                        // TODO: Trigger UTXO build stage
                        // After UTXO build completes, trigger slow sync from checkpoint+1 to headers_synced_to
                    }

                    // Check if we've caught up to headers (slow sync complete)
                    if (blocks_synced_to >= headers_synced_to && header_sync_complete) {
                        spdlog::info("[sync_coordinator] Block sync caught up to headers at {}",
                            blocks_synced_to);
                        // Could trigger new header sync here if needed
                    }
                } else {
                    spdlog::error("[sync_coordinator] Block validation failed at {}: {}",
                        result->height, result->result.message());

                    // Report error to peer_provider - it decides what to do
                    spdlog::debug("[sync_coordinator] Reporting error to peer_provider...");
                    if (result->source_peer) {
                        if (!peer_provider_input.try_send(std::error_code{}, peer_error{
                            .peer = result->source_peer,
                            .error = result->result
                        })) {
                            spdlog::warn("[sync_coordinator] Channel full, peer_error dropped");
                        }
                    }
                    spdlog::debug("[sync_coordinator] Breaking out of event loop...");
                    break;
                }
                continue;
            }
        }

        spdlog::debug("[sync_coordinator] Event loop ended, task completing...");
        spdlog::debug("[sync_coordinator] Task ended");
    });

    // Wait for all tasks
    spdlog::info("[sync_orchestrator] All {} tasks spawned, running...", all_tasks.active_count());
    co_await all_tasks.join();

    spdlog::info("[sync_orchestrator:shutdown] All tasks completed - orchestrator exiting");
}

} // namespace kth::node::sync
