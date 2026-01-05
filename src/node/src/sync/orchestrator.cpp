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
    header_download_output_channel header_download_output(executor, 100);
    header_validation_input_channel header_validation_input(executor, 100);
    header_validated_channel validated_headers(executor, 100);

    // Block pipeline - single input channels (CSP pattern)
    block_download_input_channel block_download_input(executor, 100);
    block_download_channel downloaded_blocks(executor, 1000);  // Buffer for out-of-order blocks
    block_validation_input_channel block_validation_input(executor, 1000);
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
    // Unified input channel for peer_provider (CSP pattern - single channel)
    peer_provider_input_channel peer_provider_input(executor, 100);

    // Bridge task: forwards from network.connected_peers() to unified channel
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[peer_bridge] Started, forwarding peers to peer_provider");
        try {
            while (!network.stopped()) {
                auto [ec, peer] = co_await network.connected_peers().async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                if (ec) {
                    spdlog::debug("[peer_bridge] Network channel closed: {}", ec.message());
                    break;
                }
                peer_provider_input.try_send(std::error_code{}, new_peer{peer});
            }
        } catch (std::exception const& e) {
            spdlog::error("[peer_bridge] Exception: {}", e.what());
        }
        spdlog::debug("[peer_bridge] Ended");
    });

    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::info("[peer_provider] Task started, waiting for peers...");

        // Maintain full list of connected peers - single source of truth
        std::vector<network::peer_session::ptr> all_peers;

        // Peers that had issues during header sync (by nonce) - don't use for headers
        boost::unordered_flat_set<uint64_t> header_bad_peers;

        // Helper to clean stopped peers and send updated list
        auto broadcast_peers = [&]() {
            // Remove stopped peers first
            std::erase_if(all_peers, [](auto const& p) { return p->stopped(); });

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
            header_download_input.try_send(std::error_code{}, peers_updated{header_peers});
            block_download_input.try_send(std::error_code{}, peers_updated{all_peers});
        };

        try {
            // Single channel, FIFO processing - no priority issues
            while (!network.stopped()) {
                auto [ec, event] = co_await peer_provider_input.async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));

                if (ec) {
                    spdlog::debug("[peer_provider] Input channel closed: {}", ec.message());
                    break;
                }

                if (auto* np = std::get_if<new_peer>(&event)) {
                    // New peer from network
                    if (np->peer->stopped()) continue;

                    spdlog::info("[peer_provider] New peer connected: {} (nonce={})",
                        np->peer->authority_with_agent(), np->peer->nonce());
                    all_peers.push_back(np->peer);
                    broadcast_peers();
                } else if (auto* issue = std::get_if<peer_issue>(&event)) {
                    // Peer issue report from header_download
                    spdlog::info("[peer_provider] Peer {} (nonce={}) reported issue: {}, excluding from header sync",
                        issue->peer->authority_with_agent(), issue->peer->nonce(), issue->error.message());
                    header_bad_peers.insert(issue->peer->nonce());

                    // Update peer's misbehavior score (timeout = 10 points)
                    // If score reaches threshold (100), the peer will be banned
                    if (issue->peer->misbehave(network::peer_session::misbehavior_timeout)) {
                        spdlog::warn("[peer_provider] Peer {} reached misbehavior threshold, banning",
                            issue->peer->authority_with_agent());
                        network.ban_peer(issue->peer, std::chrono::hours{24}, network::ban_reason::node_misbehaving);
                    }

                    // Broadcast updated (filtered) list immediately
                    broadcast_peers();
                }
            }
        } catch (std::exception const& e) {
            spdlog::error("[peer_provider] Exception: {}", e.what());
        }

        // Network stopped - signal all tasks to stop
        spdlog::info("[peer_provider] Network stopped, signaling all tasks to stop");
        header_download_input.try_send(std::error_code{}, stop_request{});
        header_validation_input.try_send(std::error_code{}, stop_request{});
        block_download_input.try_send(std::error_code{}, stop_request{});
        block_validation_input.try_send(std::error_code{}, stop_request{});
        stop_signal.close();

        spdlog::debug("[peer_provider] Task ended");
    });

    // -------------------------------------------------------------------------
    // 2. Header download task (1 input, 1 output)
    // -------------------------------------------------------------------------
    all_tasks.spawn(header_download_task(
        header_download_input,
        header_download_output
    ));

    // -------------------------------------------------------------------------
    // 3. Header validation bridge (forwards headers to validation input)
    // -------------------------------------------------------------------------
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[header_bridge] Started, forwarding to validation");
        try {
            while (true) {
                auto [ec, msg] = co_await header_download_output.async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                if (ec) {
                    spdlog::debug("[header_bridge] Download channel closed: {}", ec.message());
                    break;
                }
                // Forward variant contents to validation input
                if (auto* headers = std::get_if<downloaded_headers>(&msg)) {
                    header_validation_input.try_send(std::error_code{}, *headers);
                } else if (auto* failure = std::get_if<peer_failure_report>(&msg)) {
                    header_validation_input.try_send(std::error_code{}, *failure);
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
    // 6. Block validation bridge (forwards downloaded blocks to validation input)
    // -------------------------------------------------------------------------
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        spdlog::debug("[block_bridge] Started, forwarding blocks to validation");
        try {
            while (true) {
                auto [ec, block] = co_await downloaded_blocks.async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                if (ec) {
                    spdlog::debug("[block_bridge] Download channel closed: {}", ec.message());
                    break;
                }
                block_validation_input.try_send(std::error_code{}, std::move(block));
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
    uint32_t const checkpoint_height = static_cast<uint32_t>(chain.chain_settings().max_checkpoint_height);
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
        header_download_input.try_send(std::error_code{}, header_request{
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

                    // Inform peer_provider about the failed peer
                    if (result.source_peer) {
                        peer_provider_input.try_send(std::error_code{}, peer_issue{
                            .peer = result.source_peer,
                            .error = result.result
                        });
                    }

                    // Ban peer if they sent invalid data (not network errors)
                    // Invalid data = 100 points = instant ban
                    if (result.source_peer && is_bannable_error(result.result)) {
                        result.source_peer->misbehave(network::peer_session::misbehavior_invalid_data);
                        spdlog::debug("[sync_coordinator] Banning peer {} for invalid headers: {}",
                            result.source_peer->authority_with_agent(), result.result.message());
                        network.ban_peer(result.source_peer, std::chrono::hours{24 * 365}, network::ban_reason::node_misbehaving);
                    }

                    // Retry header sync from current position with another peer
                    auto next_hash = organizer.index().get_hash(
                        static_cast<blockchain::header_index::index_t>(headers_synced_to));

                    spdlog::debug("[sync_coordinator] Retrying header sync from height {} with another peer",
                        headers_synced_to);

                    bool sent = header_download_input.try_send(std::error_code{}, header_request{
                        .from_height = headers_synced_to,
                        .from_hash = next_hash
                    });
                    spdlog::debug("[sync_coordinator] Header request sent (retry): {}", sent ? "ok" : "FAILED");
                } else if (result.count > 0) {
                    headers_synced_to = result.height;

                    // Log progress every 5000 headers
                    auto const headers_downloaded = headers_synced_to - headers_at_start;
                    if (headers_downloaded % 5000 < max_headers_per_batch) {
                        auto const elapsed = std::chrono::steady_clock::now() - header_sync_start;
                        auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                        auto const rate = elapsed_secs > 0 ? headers_downloaded / elapsed_secs : 0;

                        // Note: we don't know the exact tip until header sync completes
                        spdlog::info("[header_sync] height {} ({}/s)", headers_synced_to, rate);
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

                            block_download_input.try_send(std::error_code{}, block_range_request{
                                .start_height = blocks_synced_to + 1,
                                .end_height = headers_synced_to
                            });
                        }
                    } else {
                        // Continue header sync - more headers to fetch
                        auto next_hash = organizer.index().get_hash(
                            static_cast<blockchain::header_index::index_t>(headers_synced_to));

                        bool sent = header_download_input.try_send(std::error_code{}, header_request{
                            .from_height = headers_synced_to,
                            .from_hash = next_hash
                        });
                        spdlog::debug("[sync_coordinator] Header request sent (continue): {}", sent ? "ok" : "FAILED");
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

                        block_download_input.try_send(std::error_code{}, block_range_request{
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

                        header_download_input.try_send(std::error_code{}, header_request{
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

                    // Ban peer if they sent invalid block (not network errors)
                    // Invalid data = 100 points = instant ban
                    if (result.source_peer && is_bannable_error(result.result)) {
                        result.source_peer->misbehave(network::peer_session::misbehavior_invalid_data);
                        spdlog::debug("[sync_coordinator] Banning peer {} for invalid block: {}",
                            result.source_peer->authority_with_agent(), result.result.message());
                        network.ban_peer(result.source_peer, std::chrono::hours{24 * 365}, network::ban_reason::node_misbehaving);
                    }
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
