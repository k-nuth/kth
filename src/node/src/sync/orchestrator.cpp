// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/orchestrator.hpp>

#include <chrono>

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
    spdlog::debug("[header_persist] Starting: {} headers ({} to {})", total, start_height, end_height);

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

        // Log progress every 50000 headers
        if (persisted % 50000 < batch_size) {
            auto const elapsed = std::chrono::steady_clock::now() - start_time;
            auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            auto const rate = elapsed_secs > 0 ? persisted / elapsed_secs : persisted;
            spdlog::debug("[header_persist] Progress: {}/{} ({}/s)", persisted, total, rate);
        }

        // Yield to allow other coroutines to run
        co_await ::asio::post(co_await ::asio::this_coro::executor, ::asio::use_awaitable);
    }

    auto const elapsed = std::chrono::steady_clock::now() - start_time;
    auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    spdlog::debug("[header_persist] Complete: {} headers in {}ms", persisted, elapsed_ms);
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

    // Peer distribution
    peer_channel peers_for_headers(executor, 100);
    peer_channel peers_for_blocks(executor, 100);

    // Header pipeline
    header_request_channel header_requests(executor, 10);
    header_download_channel downloaded_headers(executor, 100);
    header_validated_channel validated_headers(executor, 100);

    // Block pipeline
    block_request_channel block_requests(executor, 10);
    block_download_channel downloaded_blocks(executor, 1000);  // Buffer for out-of-order blocks
    block_validated_channel validated_blocks(executor, 100);

    // Control
    stop_channel stop_signal(executor, 1);

    // =========================================================================
    // TASKS - All independent, communicate only via channels
    // =========================================================================

    task_group all_tasks(executor);

    // -------------------------------------------------------------------------
    // 1. Peer provider - receives connected peers from network and distributes
    // -------------------------------------------------------------------------
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::info("[peer_provider] Task started, waiting for peers from network...");

        // Maintain full list of connected peers - single source of truth
        std::vector<network::peer_session::ptr> all_peers;

        // Helper to clean stopped peers and send updated list
        auto broadcast_peers = [&]() {
            // Remove stopped peers first
            std::erase_if(all_peers, [](auto const& p) { return p->stopped(); });

            spdlog::debug("[peer_provider] Broadcasting {} peers to sync tasks", all_peers.size());

            // Send full list to both pipelines
            peers_for_headers.try_send(std::error_code{}, peers_updated{all_peers});
            peers_for_blocks.try_send(std::error_code{}, peers_updated{all_peers});
        };

        try {
            // Receive peers from network's channel (CSP push, no polling!)
            while (!network.stopped()) {
                auto [ec, peer] = co_await network.connected_peers().async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));

                if (ec) {
                    spdlog::debug("[peer_provider] Channel closed or error: {}", ec.message());
                    break;
                }

                if (peer->stopped()) continue;

                spdlog::debug("[peer_provider] New peer connected: {}", peer->authority_with_agent());

                // Add to our list and broadcast updated list
                all_peers.push_back(peer);
                broadcast_peers();
            }
        } catch (std::exception const& e) {
            spdlog::error("[peer_provider] Exception: {}", e.what());
        }

        // Network stopped - signal all tasks to stop
        spdlog::info("[peer_provider] Network stopped, signaling all tasks to stop");
        stop_signal.close();

        spdlog::debug("[peer_provider] Task ended");
    });

    // -------------------------------------------------------------------------
    // 2. Header download task
    // -------------------------------------------------------------------------
    all_tasks.spawn(header_download_task(
        peers_for_headers,
        header_requests,
        downloaded_headers,
        stop_signal
    ));

    // -------------------------------------------------------------------------
    // 3. Header validation task
    // -------------------------------------------------------------------------
    all_tasks.spawn(header_validation_task(
        organizer,
        downloaded_headers,
        validated_headers,
        stop_signal
    ));

    // -------------------------------------------------------------------------
    // 4. Block download supervisor (spawns per-peer download tasks)
    // -------------------------------------------------------------------------
    all_tasks.spawn(block_download_supervisor(
        peers_for_blocks,
        block_requests,
        downloaded_blocks,
        stop_signal,
        organizer
    ));

    // -------------------------------------------------------------------------
    // 5. Block validation task
    // -------------------------------------------------------------------------
    auto heights_result = co_await chain.fetch_last_height();
    uint32_t const initial_block_height = heights_result
        ? heights_result->block
        : 0;

    all_tasks.spawn(block_validation_task(
        chain,
        downloaded_blocks,
        validated_blocks,
        stop_signal,
        initial_block_height + 1
    ));

    // -------------------------------------------------------------------------
    // 6. Sync coordinator - orchestrates the sync flow
    // -------------------------------------------------------------------------
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[sync_coordinator] Task started");

        uint32_t blocks_synced_to = initial_block_height;
        uint32_t headers_synced_to = uint32_t(organizer.header_height());
        uint32_t const initial_header_height = headers_synced_to;  // For persistence
        bool header_sync_complete = false;
        constexpr size_t max_headers_per_batch = 2000;

        // For ETA calculation
        auto header_sync_start = std::chrono::steady_clock::now();
        uint32_t headers_at_start = headers_synced_to;

        spdlog::info("[sync_coordinator] Initial state: blocks={}, headers={}",
            blocks_synced_to, headers_synced_to);

        auto exec = co_await ::asio::this_coro::executor;

        // Trigger initial header sync (will be buffered until peers arrive)
        auto from_hash = organizer.index().get_hash(
            static_cast<blockchain::header_index::index_t>(headers_synced_to));

        spdlog::debug("[sync_coordinator] Starting header sync from height {}", headers_synced_to);
        header_requests.try_send(std::error_code{}, header_request{
            .from_height = headers_synced_to,
            .from_hash = from_hash
        });

        while (true) {
            // Wait for events OR stop signal (stop first for priority)
            spdlog::debug("[sync_coordinator] Waiting for validated headers/blocks...");
            auto event = co_await (
                stop_signal.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
                validated_headers.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
                validated_blocks.async_receive(::asio::as_tuple(::asio::use_awaitable))
            );

            // Stop signal received (index 0)
            if (event.index() == 0) {
                spdlog::debug("[sync_coordinator] Stop signal received");
                break;
            }

            if (event.index() == 1) {
                // Headers validated
                auto [ec, result] = std::get<1>(event);
                if (ec) {
                    spdlog::debug("[sync_coordinator] Headers channel closed");
                    break;
                }

                spdlog::debug("[sync_coordinator] Received: height={}, count={}, result={}",
                    result.height, result.count, result.result ? result.result.message() : "ok");

                if (result.result) {
                    // Header validation failed - normal network behavior (peer on wrong chain)
                    spdlog::debug("[sync_coordinator] Header validation failed: {} from peer {}",
                        result.result.message(),
                        result.source_peer ? result.source_peer->authority_with_agent() : "unknown");

                    // Update progress if some headers were added before the failure
                    if (result.count > 0) {
                        headers_synced_to = result.height;
                        spdlog::debug("[sync_coordinator] Headers synced to {} before failure", headers_synced_to);
                    }

                    // Ban peer if wrong chain (checkpoint failure or missing parent)
                    if (result.source_peer && (
                            result.result == error::checkpoints_failed ||
                            result.result == error::store_block_missing_parent)) {
                        spdlog::debug("[sync_coordinator] Banning peer {} for wrong chain: {}",
                            result.source_peer->authority_with_agent(), result.result.message());
                        // ban_peer() is thread-safe: banlist uses concurrent_flat_map, stop() uses atomic
                        network.ban_peer(result.source_peer, std::chrono::hours{24 * 365}, network::ban_reason::checkpoint_failed);
                    }

                    // Retry header sync from current position with another peer
                    auto next_hash = organizer.index().get_hash(
                        static_cast<blockchain::header_index::index_t>(headers_synced_to));

                    spdlog::debug("[sync_coordinator] Retrying header sync from height {} with another peer",
                        headers_synced_to);

                    header_requests.try_send(std::error_code{}, header_request{
                        .from_height = headers_synced_to,
                        .from_hash = next_hash
                    });
                } else if (result.count > 0) {
                    headers_synced_to = result.height;

                    // Log progress with ETA every 10000 headers
                    auto const headers_downloaded = headers_synced_to - headers_at_start;
                    if (headers_downloaded % 10000 < max_headers_per_batch) {
                        auto const elapsed = std::chrono::steady_clock::now() - header_sync_start;
                        auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                        auto const rate = elapsed_secs > 0 ? headers_downloaded / elapsed_secs : 0;
                        spdlog::debug("[sync_coordinator] Headers: {} | {}/s",
                            headers_synced_to, rate);
                    }

                    // Check if this is the last batch (less than max headers)
                    if (result.count < max_headers_per_batch) {
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

                        // NOW trigger block download for the full range
                        if (headers_synced_to > blocks_synced_to) {
                            spdlog::info("[sync_coordinator] Starting block download: {} to {} ({} blocks)",
                                blocks_synced_to + 1, headers_synced_to,
                                headers_synced_to - blocks_synced_to);

                            block_requests.try_send(std::error_code{}, block_range_request{
                                .start_height = blocks_synced_to + 1,
                                .end_height = headers_synced_to
                            });
                        }
                    } else {
                        // Continue header sync - more headers to fetch
                        auto next_hash = organizer.index().get_hash(
                            static_cast<blockchain::header_index::index_t>(headers_synced_to));

                        header_requests.try_send(std::error_code{}, header_request{
                            .from_height = headers_synced_to,
                            .from_hash = next_hash
                        });
                    }
                } else if (result.count == 0) {
                    // No new headers - header sync complete (at tip)
                    header_sync_complete = true;
                    spdlog::info("[sync_coordinator] Header sync COMPLETE at height {} (peer returned 0 headers)",
                        headers_synced_to);

                    // Spawn background task to persist headers to DB
                    if (headers_synced_to > initial_header_height) {
                        ::asio::co_spawn(exec,
                            persist_headers_to_db(chain, organizer.index(),
                                initial_header_height + 1, headers_synced_to),
                            ::asio::detached);
                    }

                    // Trigger block download if we have headers ahead of blocks
                    if (headers_synced_to > blocks_synced_to) {
                        spdlog::info("[sync_coordinator] Starting block download: {} to {} ({} blocks)",
                            blocks_synced_to + 1, headers_synced_to,
                            headers_synced_to - blocks_synced_to);

                        block_requests.try_send(std::error_code{}, block_range_request{
                            .start_height = blocks_synced_to + 1,
                            .end_height = headers_synced_to
                        });
                    } else {
                        // Already synced - wait and check for new blocks later
                        spdlog::info("[sync_coordinator] Fully synced at height {}", blocks_synced_to);
                        ::asio::steady_timer timer(exec);
                        timer.expires_after(std::chrono::seconds(10));
                        auto [timer_ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
                        if (timer_ec || network.stopped()) break;

                        // Reset for new sync cycle
                        header_sync_complete = false;
                        auto next_hash = organizer.index().get_hash(
                            static_cast<blockchain::header_index::index_t>(headers_synced_to));

                        header_requests.try_send(std::error_code{}, header_request{
                            .from_height = headers_synced_to,
                            .from_hash = next_hash
                        });
                    }
                }
            } else {
                // Block validated (index 2)
                auto [ec, result] = std::get<2>(event);
                if (ec) {
                    spdlog::debug("[sync_coordinator] Blocks channel closed");
                    break;
                }

                if (!result.result) {
                    blocks_synced_to = result.height;

                    // Check if we've caught up to headers
                    if (blocks_synced_to >= headers_synced_to && header_sync_complete) {
                        spdlog::info("[sync_coordinator] Block sync caught up to headers at {}",
                            blocks_synced_to);
                        // Could trigger new header sync here if needed
                    }
                } else {
                    spdlog::error("[sync_coordinator] Block validation failed at {}: {}",
                        result.height, result.result.message());
                    break;
                }
            }
        }

        spdlog::debug("[sync_coordinator] Task ended");
    });

    // Wait for all tasks
    co_await all_tasks.join();

    spdlog::info("[sync_orchestrator] All tasks completed");
}

} // namespace kth::node::sync
