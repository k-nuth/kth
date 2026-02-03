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
#include <kth/blockchain/utxo_builder.hpp>
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

    // Bridge task: forwards from network.peer_events() to unified channel
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[peer_bridge] Started, forwarding peer events to peer_provider");
        auto exec = co_await ::asio::this_coro::executor;
        ::asio::steady_timer check_timer(exec);

        try {
            while (!network.stopped()) {
                // Use timer to periodically check if network stopped
                check_timer.expires_after(std::chrono::milliseconds(500));
                auto event = co_await (
                    network.peer_events().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
                    check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
                );

                // Timer fired - just loop to check network.stopped()
                if (event.index() == 1) {
                    continue;
                }

                auto [ec, notification] = std::get<0>(event);
                if (ec) {
                    spdlog::debug("[peer_bridge] Network channel closed: {}", ec.message());
                    break;
                }

                // Demultiplex based on event type
                if (notification.event == network::peer_event_type::connected) {
                    if (!peer_provider_input.try_send(std::error_code{}, new_peer{notification.peer})) {
                        spdlog::warn("[peer_bridge] Channel full, new_peer dropped for {}",
                            notification.peer->authority_with_agent());
                    }
                } else {
                    if (!peer_provider_input.try_send(std::error_code{}, peer_disconnected{notification.peer})) {
                        spdlog::warn("[peer_bridge] Channel full, peer_disconnected dropped for {}",
                            notification.peer->authority_with_agent());
                    }
                }
            }
        } catch (std::exception const& e) {
            spdlog::error("[peer_bridge] Exception: {}", e.what());
        }
        // Cancel timer before exiting to ensure clean shutdown of || operator internals
        check_timer.cancel();
        spdlog::info("[peer_bridge] Task ended");
    });

    // -------------------------------------------------------------------------
    // peer_provider - Minimal peer distributor for sync tasks
    //
    // Responsibilities:
    // - Receive peer connect/disconnect events from network
    // - Broadcast peer list to header_download and block_download tasks
    // - Report errors to network.report_misbehavior() (reputation system handles bans)
    // - Record performance to network.record_peer_performance() (persistent storage)
    // - Close channels on shutdown
    //
    // All reputation tracking, ban decisions, and eviction logic are handled by
    // peer_database in the network layer.
    // -------------------------------------------------------------------------
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::info("[peer_provider] Task started, waiting for peers...");

        // Local peer list for broadcasting to sync tasks
        std::vector<network::peer_session::ptr> peers;

        // Broadcast current peer list to download tasks
        auto broadcast_peers = [&]() {
            // Remove stopped peers
            std::erase_if(peers, [](auto const& p) { return p->stopped(); });

            spdlog::info("[peer_provider] Broadcasting {} peers to sync tasks", peers.size());

            if (!header_download_input.try_send(std::error_code{}, peers_updated{peers})) {
                spdlog::warn("[peer_provider] Channel full, peers_updated dropped for header_download");
            }
            if (!block_download_input.try_send(std::error_code{}, peers_updated{peers})) {
                spdlog::warn("[peer_provider] Channel full, peers_updated dropped for block_download");
            }
        };

        auto exec = co_await ::asio::this_coro::executor;
        ::asio::steady_timer check_timer(exec);

        try {
            while (!network.stopped()) {
                check_timer.expires_after(std::chrono::milliseconds(500));
                auto event = co_await (
                    peer_provider_input.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
                    check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
                );

                if (event.index() == 1) continue;  // Timer - check network.stopped()

                auto [ec, msg] = std::get<0>(event);
                if (ec) {
                    spdlog::debug("[peer_provider] Input channel closed: {}", ec.message());
                    break;
                }

                if (auto* np = std::get_if<new_peer>(&msg)) {
                    if (np->peer->stopped()) continue;
                    spdlog::info("[peer_provider] New peer: {} (nonce={})",
                        np->peer->authority_with_agent(), np->peer->nonce());
                    peers.push_back(np->peer);
                    broadcast_peers();

                } else if (auto* dp = std::get_if<peer_disconnected>(&msg)) {
                    spdlog::info("[peer_provider] Peer disconnected: {} (nonce={})",
                        dp->peer->authority_with_agent(), dp->peer->nonce());
                    std::erase_if(peers, [&](auto const& p) { return p->nonce() == dp->peer->nonce(); });
                    broadcast_peers();

                } else if (auto* err = std::get_if<peer_error>(&msg)) {
                    spdlog::info("[peer_provider] Peer error: {} - {}",
                        err->peer->authority_with_agent(), err->error.message());

                    // Report to reputation system - it decides if peer should be banned
                    int score = is_bannable_error(err->error) ? 100 : 10;
                    bool banned = network.report_misbehavior(err->peer, score, err->error.message());

                    if (banned) {
                        // Peer was banned - remove from local list
                        std::erase_if(peers, [&](auto const& p) { return p->nonce() == err->peer->nonce(); });
                        broadcast_peers();
                    }

                } else if (auto* perf = std::get_if<peer_performance>(&msg)) {
                    // Find peer by nonce and record to persistent storage
                    for (auto const& p : peers) {
                        if (p->nonce() == perf->peer_nonce) {
                            network.record_peer_performance(p, perf->blocks_downloaded, perf->download_time_ms);
                            break;
                        }
                    }

                } else if (auto* hperf = std::get_if<header_performance>(&msg)) {
                    // Header performance - just log, not used for eviction
                    spdlog::debug("[peer_provider] Header perf: peer={}, headers={}, time={}ms",
                        hperf->peer_nonce, hperf->headers_downloaded, hperf->download_time_ms);
                }
            }
        } catch (std::exception const& e) {
            spdlog::error("[peer_provider] Exception: {}", e.what());
        }

        check_timer.cancel();

        // Cancel and close all data channels to unblock pending operations
        // Note: cancel() wakes up pending async ops, close() alone does NOT!
        spdlog::info("[peer_provider:shutdown] Closing channels...");
        header_download_input.cancel();
        header_download_input.close();
        header_download_output.cancel();
        header_download_output.close();
        header_validation_input.cancel();
        header_validation_input.close();
        validated_headers.cancel();
        validated_headers.close();
        block_download_input.cancel();
        block_download_input.close();
        downloaded_blocks.cancel();
        downloaded_blocks.close();
        block_validation_input.cancel();
        block_validation_input.close();
        validated_blocks.cancel();
        validated_blocks.close();
        coordinator_events.cancel();
        coordinator_events.close();

        spdlog::info("[peer_provider] Task ended");
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
        spdlog::info("[header_bridge] Task ended");
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
        spdlog::info("[block_bridge] Started, waiting for blocks from supervisor");
        uint64_t blocks_forwarded = 0;

        // Timing stats
        uint64_t total_recv_wait_us = 0;
        uint64_t total_send_wait_us = 0;
        auto last_stats_time = std::chrono::steady_clock::now();

        try {
            while (true) {
                // Measure time waiting to receive from supervisor
                auto recv_start = std::chrono::steady_clock::now();
                auto [ec, msg] = co_await downloaded_blocks.async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                auto recv_wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - recv_start).count();

                if (ec) {
                    spdlog::debug("[block_bridge] Download channel closed: {}", ec.message());
                    break;
                }

                total_recv_wait_us += recv_wait_us;

                if (auto* block = std::get_if<downloaded_light_block>(&msg)) {
                    // Track received for pipeline debugging
                    g_blocks_received_by_bridge.fetch_add(1, std::memory_order_relaxed);

                    // Log first few blocks and periodically
                    if (blocks_forwarded < 10 || blocks_forwarded % 1000 == 0) {
                        spdlog::info("[block_bridge] Received block {} from supervisor (total: {})",
                            block->height, blocks_forwarded);
                    }

                    // Forward to validation with retry - measure send time
                    auto send_start = std::chrono::steady_clock::now();
                    bool sent = co_await try_send_with_retry(block_validation_input, std::move(*block), 20, std::chrono::milliseconds(10));
                    auto send_wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - send_start).count();

                    if (!sent) {
                        spdlog::error("[block_bridge] Validation channel full after retries! forwarded={}", blocks_forwarded);
                        break;
                    }

                    total_send_wait_us += send_wait_us;
                    g_blocks_forwarded_by_bridge.fetch_add(1, std::memory_order_relaxed);
                    ++blocks_forwarded;

                    // Log timing stats every 10 seconds
                    auto now = std::chrono::steady_clock::now();
                    if (now - last_stats_time >= std::chrono::seconds(10)) {
                        double avg_recv_ms = blocks_forwarded > 0
                            ? static_cast<double>(total_recv_wait_us) / blocks_forwarded / 1000.0 : 0;
                        double avg_send_ms = blocks_forwarded > 0
                            ? static_cast<double>(total_send_wait_us) / blocks_forwarded / 1000.0 : 0;
                        spdlog::info("[block_bridge] timing: recv_wait={:.2f}ms/blk send_wait={:.2f}ms/blk (n={})",
                            avg_recv_ms, avg_send_ms, blocks_forwarded);
                        last_stats_time = now;
                    }
                } else if (auto* perf = std::get_if<peer_performance>(&msg)) {
                    // Forward performance stats to peer_provider (non-critical)
                    if (!peer_provider_input.try_send(std::error_code{}, *perf)) {
                        spdlog::debug("[block_bridge] Channel full, performance stats dropped");
                    }
                }
            }
        } catch (std::exception const& e) {
            spdlog::error("[block_bridge] Exception: {}", e.what());
        }
        spdlog::info("[block_bridge] Task ended, forwarded {} blocks", blocks_forwarded);
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

    // =========================================================================
    // TODO(fernando): TEMPORARY - REMOVE THIS BLOCK AFTER TESTING UTXO BUILD
    // =========================================================================
    {
        spdlog::warn("##############################################################");
        spdlog::warn("###                                                        ###");
        spdlog::warn("###   !!! TEMPORARY DEBUG CODE - CLEARING UTXO SET !!!     ###");
        spdlog::warn("###   !!! REMOVE THIS AFTER TESTING IS COMPLETE   !!!      ###");
        spdlog::warn("###                                                        ###");
        spdlog::warn("##############################################################");

        auto clear_result = chain.clear_utxo_set();
        if (clear_result != database::result_code::success) {
            spdlog::error("[sync_orchestrator] Failed to clear UTXO set!");
        }

        spdlog::warn("##############################################################");
        spdlog::warn("###   UTXO SET CLEARED - WILL REBUILD FROM SCRATCH         ###");
        spdlog::warn("##############################################################");
    }
    // =========================================================================
    // END TEMPORARY CODE
    // =========================================================================

    // Check if we need to build UTXO set on startup
    // This happens when fast sync completed in a previous session but UTXO wasn't built
    if (initial_block_height >= checkpoint_height) {
        auto utxo_height = chain.get_utxo_built_height();
        uint32_t current_utxo_height = utxo_height.value_or(0);

        if (current_utxo_height < checkpoint_height) {
            spdlog::info("[sync_orchestrator] UTXO set incomplete (at {}), building to checkpoint {}...",
                current_utxo_height, checkpoint_height);

            auto utxo_result = co_await blockchain::build_utxo_set(
                chain,
                network.thread_pool().get(),
                1,  // Start from block 1 (skip genesis)
                checkpoint_height,
                blockchain::utxo_build_strategy::sequential_batch
            );

            if (utxo_result != database::result_code::success) {
                spdlog::error("[sync_orchestrator] UTXO build failed on startup!");
                co_return;
            }

            spdlog::info("[sync_orchestrator] UTXO set build complete on startup");
        } else {
            spdlog::info("[sync_orchestrator] UTXO set already built to height {}", current_utxo_height);
        }
    }

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
        spdlog::info("[coordinator:stop_bridge] Task ended");
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
        spdlog::info("[coordinator:headers_bridge] Task ended");
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
        spdlog::info("[coordinator:blocks_bridge] Task ended");
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

                    spdlog::info("[sync_coordinator] Retrying header sync from height {} with different peer, from_hash={}",
                        headers_synced_to, encode_hash(retry_hash));

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
                    // All peers returned 0 headers - check if we've reached checkpoint
                    if (headers_synced_to < checkpoint_height) {
                        // Not at checkpoint yet - we need more headers but all peers are behind
                        // Wait a bit and retry (peers might sync more)
                        spdlog::warn("[sync_coordinator] All peers at height {} but checkpoint is {} - waiting and retrying",
                            headers_synced_to, checkpoint_height);

                        ::asio::steady_timer timer(exec);
                        timer.expires_after(std::chrono::seconds(10));
                        auto [timer_ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
                        if (timer_ec || network.stopped()) break;

                        // Retry header sync
                        auto retry_hash = organizer.index().get_hash(
                            static_cast<blockchain::header_index::index_t>(headers_synced_to));

                        if (!header_download_input.try_send(std::error_code{}, header_request{
                            .from_height = headers_synced_to,
                            .from_hash = retry_hash
                        })) {
                            spdlog::warn("[sync_coordinator] Channel full, retry header_request dropped");
                        }
                        continue;
                    }

                    // At or past checkpoint - header sync complete
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
                            // Fast sync stage: download up to checkpoint
                            // NOTE: We should only reach here if headers_synced_to >= checkpoint_height
                            // because header_sync should not complete until we have checkpoint headers
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
                        spdlog::info("[sync_coordinator] Starting UTXO set build...");

                        // Build UTXO set from all stored blocks
                        auto utxo_result = co_await blockchain::build_utxo_set(
                            chain,
                            network.thread_pool().get(),
                            1,  // Start from block 1 (skip genesis)
                            checkpoint_height,
                            blockchain::utxo_build_strategy::sequential_batch
                        );

                        if (utxo_result != database::result_code::success) {
                            spdlog::error("[sync_coordinator] UTXO build failed!");
                            break;
                        }

                        spdlog::info("[sync_coordinator] UTXO set build complete, ready for slow sync");
                        // After UTXO build completes, slow sync continues automatically
                        // from checkpoint+1 to headers_synced_to
                    }

                    // Check if we've caught up to headers
                    if (blocks_synced_to >= headers_synced_to && header_sync_complete) {
                        // Whether before or after checkpoint, we need to keep looking for more headers
                        // to stay in sync with the network tip
                        spdlog::info("[sync_coordinator] Block sync caught up to headers at {} (checkpoint={}), restarting header sync",
                            blocks_synced_to, checkpoint_height);

                        header_sync_complete = false;
                        header_sync_start = std::chrono::steady_clock::now();
                        headers_at_start = headers_synced_to;

                        auto next_hash = organizer.index().get_hash(
                            static_cast<blockchain::header_index::index_t>(headers_synced_to));

                        if (!header_download_input.try_send(std::error_code{}, header_request{
                            .from_height = headers_synced_to,
                            .from_hash = next_hash
                        })) {
                            spdlog::warn("[sync_coordinator] Channel full, restart header_request dropped");
                        }
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

        spdlog::info("[sync_coordinator] Event loop ended, task completing...");
        spdlog::info("[sync_coordinator] Task ended");
    });

    // Wait for all tasks
    spdlog::info("[sync_orchestrator] All {} tasks spawned, running...", all_tasks.active_count());
    co_await all_tasks.join();

    spdlog::info("[sync_orchestrator:shutdown] All tasks completed - orchestrator exiting");
}

} // namespace kth::node::sync
