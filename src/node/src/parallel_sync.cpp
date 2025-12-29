// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/parallel_sync.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <boost/unordered/unordered_flat_set.hpp>
#include <spdlog/spdlog.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/network/protocols_coro.hpp>

namespace kth::node {

namespace {

// =============================================================================
// Peer Download Loop
// =============================================================================

/// Download loop for a single peer
::asio::awaitable<void> peer_download_loop(
    block_download_coordinator& coordinator,
    network::peer_session::ptr peer,
    parallel_download_config const& config)
{
    auto executor = co_await ::asio::this_coro::executor;

    spdlog::info("[parallel_sync] Download loop STARTED for peer [{}]",
        peer->authority());

    // Check initial conditions
    spdlog::debug("[parallel_sync] Peer [{}] - checking initial conditions", peer->authority());
    if (coordinator.is_complete()) {
        spdlog::debug("[parallel_sync] Peer [{}] - sync already complete, exiting",
            peer->authority());
        co_return;
    }
    if (coordinator.has_failed()) {
        spdlog::debug("[parallel_sync] Peer [{}] - sync already failed, exiting",
            peer->authority());
        co_return;
    }
    if (peer->stopped()) {
        spdlog::debug("[parallel_sync] Peer [{}] - peer already stopped, exiting",
            peer->authority());
        co_return;
    }

    spdlog::debug("[parallel_sync] Peer [{}] - entering main loop", peer->authority());
    while (!coordinator.is_complete() && !coordinator.has_failed() && !peer->stopped()) {
        // Claim blocks to download
        spdlog::debug("[parallel_sync] Peer [{}] - calling claim_blocks", peer->authority());
        auto blocks = co_await coordinator.claim_blocks(peer, config.max_blocks_per_peer);
        spdlog::debug("[parallel_sync] Peer [{}] - claim_blocks returned {} blocks", peer->authority(), blocks.size());

        if (blocks.empty()) {
            // No blocks available - wait a bit and retry
            spdlog::debug("[parallel_sync] Peer [{}] - claim_blocks returned empty, waiting 100ms...",
                peer->authority());
            ::asio::steady_timer timer(executor);
            timer.expires_after(std::chrono::milliseconds(100));
            co_await timer.async_wait(::asio::use_awaitable);
            continue;
        }

        spdlog::info("[parallel_sync] Peer [{}] CLAIMED {} blocks ({}-{}), requesting...",
            peer->authority(), blocks.size(), blocks.front().first, blocks.back().first);

        // Request all blocks in ONE getdata message (batch mode)
        // This is MUCH faster than requesting one block at a time
        auto batch_result = co_await network::request_blocks_batch(*peer, blocks, config.block_timeout);

        if (!batch_result) {
            spdlog::warn("[parallel_sync] Failed to get blocks from peer [{}]: {}",
                peer->authority(), batch_result.error().message());
            co_await coordinator.peer_disconnected(peer);
            // Stop the peer so network can drop it and connect to new peers
            peer->stop(batch_result.error());
            co_return;
        }

        spdlog::debug("[parallel_sync] Peer [{}] received {} blocks, reporting to coordinator",
            peer->authority(), batch_result->size());

        // Report all received blocks to coordinator
        for (auto& block_with_h : *batch_result) {
            auto block_ptr = std::make_shared<domain::message::block const>(std::move(block_with_h.block));
            // Get the original hash from the blocks vector
            auto it = std::find_if(blocks.begin(), blocks.end(),
                [h = block_with_h.height](auto const& p) { return p.first == h; });
            if (it != blocks.end()) {
                co_await coordinator.block_received(block_with_h.height, it->second, block_ptr);
            }
        }

    }

    // Log why we exited
    if (coordinator.is_complete()) {
        spdlog::debug("[parallel_sync] Peer [{}] - exiting: sync complete", peer->authority());
    } else if (coordinator.has_failed()) {
        spdlog::debug("[parallel_sync] Peer [{}] - exiting: sync failed", peer->authority());
    } else if (peer->stopped()) {
        spdlog::debug("[parallel_sync] Peer [{}] - exiting: peer stopped", peer->authority());
    } else {
        spdlog::debug("[parallel_sync] Peer [{}] - exiting: unknown reason", peer->authority());
    }
}

// =============================================================================
// Validation Pipeline
// =============================================================================

/// Validation pipeline - validates blocks in order
::asio::awaitable<void> validation_pipeline(
    block_download_coordinator& coordinator,
    blockchain::block_chain& chain)
{
    spdlog::debug("[parallel_sync] Starting validation pipeline");

    while (true) {
        // Get next block to validate (blocks until available)
        auto block_opt = co_await coordinator.next_block_to_validate();

        if (!block_opt) {
            // No more blocks or sync failed
            break;
        }

        auto const& [height, block] = *block_opt;

        spdlog::trace("[parallel_sync] Validating block at height {}", height);

        // Validate and store block
        // Note: organize() validates the block and adds it to the chain.
        // headers_pre_validated=true because this is headers-first sync.
        auto result = co_await chain.organize(block, /*headers_pre_validated=*/true);

        coordinator.validation_complete(height, result);

        if (result != error::success) {
            spdlog::error("[parallel_sync] Block validation failed at height {}: {}",
                height, result.message());
            break;
        }

        // Log progress periodically
        if (height % 1000 == 0) {
            auto progress = coordinator.get_progress();
            auto now = std::chrono::steady_clock::now();
            auto elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(
                now - progress.start_time).count();

            // Calculate speed and ETA
            double blocks_per_sec = elapsed_secs > 0
                ? static_cast<double>(progress.blocks_validated) / static_cast<double>(elapsed_secs)
                : 0.0;

            uint32_t total_blocks = progress.target_height - progress.start_height + 1;
            uint32_t remaining = total_blocks - progress.blocks_validated;
            uint32_t eta_secs = blocks_per_sec > 0
                ? static_cast<uint32_t>(static_cast<double>(remaining) / blocks_per_sec)
                : 0;

            // Format ETA as HH:MM:SS
            uint32_t eta_hours = eta_secs / 3600;
            uint32_t eta_mins = (eta_secs % 3600) / 60;
            uint32_t eta_s = eta_secs % 60;

            double percent = 100.0 * static_cast<double>(progress.blocks_validated) /
                             static_cast<double>(total_blocks);

            spdlog::info("[parallel_sync] Height: {} ({:.1f}%) | {:.0f} blk/s | ETA: {:02d}:{:02d}:{:02d} | peers: {} | in-flight: {}",
                height, percent, blocks_per_sec, eta_hours, eta_mins, eta_s,
                progress.active_peers, progress.blocks_in_flight);
        }
    }

    spdlog::debug("[parallel_sync] Validation pipeline completed");
}

// =============================================================================
// Timeout Checker
// =============================================================================

/// Periodically check for stalled downloads
::asio::awaitable<void> timeout_checker(
    block_download_coordinator& coordinator,
    parallel_download_config const& config)
{
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);

    spdlog::debug("[parallel_sync] Starting timeout checker");

    while (!coordinator.is_complete() && !coordinator.has_failed() && !coordinator.is_stopped()) {
        timer.expires_after(config.timeout_check_interval);
        auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            break;  // Timer cancelled
        }

        coordinator.check_timeouts();
    }

    spdlog::debug("[parallel_sync] Timeout checker completed");
}

} // anonymous namespace

// =============================================================================
// Main Parallel Sync Function
// =============================================================================

::asio::awaitable<parallel_sync_result> parallel_block_sync(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    network::p2p_node& network,
    uint32_t start_height,
    uint32_t target_height,
    parallel_download_config const& config)
{
    auto executor = co_await ::asio::this_coro::executor;

    spdlog::info("[parallel_sync] Starting parallel block sync from {} to {} ({} blocks)",
        start_height, target_height, target_height - start_height + 1);

    // Create coordinator
    block_download_coordinator coordinator(
        chain, organizer, start_height, target_height, executor, config);

    // Get available peers (async version)
    auto peers = co_await network.peers().all();
    if (peers.empty()) {
        spdlog::error("[parallel_sync] No peers available for block download");
        co_return parallel_sync_result{
            .error = error::channel_stopped,
            .blocks_downloaded = 0,
            .blocks_validated = 0,
            .final_height = start_height
        };
    }

    spdlog::info("[parallel_sync] Using {} peers for parallel download", peers.size());

    // =========================================================================
    // Structured concurrency with proper shutdown handling:
    // - Download loops run in their own group
    // - When ALL download loops exit (peers disconnect or sync complete),
    //   we stop the coordinator to unblock validation/timeout tasks
    // =========================================================================

    kth::task_group all_tasks(executor);

    // Spawn a "download supervisor" that:
    // 1. Runs all download loops in a nested task group
    // 2. Watches for new peers and adds them dynamically
    // 3. When ALL download loops exit, stops the coordinator
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        kth::task_group download_tasks(executor);

        // Track which peers already have download loops (by address string)
        boost::unordered_flat_set<std::string> active_peers;

        // Start initial peers
        for (auto const& peer : peers) {
            auto addr = peer->authority().to_string();
            active_peers.insert(addr);
            spdlog::info("[parallel_sync] Starting download loop for initial peer [{}]", addr);
            download_tasks.spawn(peer_download_loop(coordinator, peer, config));
        }

        // Spawn a peer watcher that adds new peers as they connect
        download_tasks.spawn([&]() -> ::asio::awaitable<void> {
            auto exec = co_await ::asio::this_coro::executor;
            ::asio::steady_timer timer(exec);

            while (!coordinator.is_complete() && !coordinator.has_failed() && !coordinator.is_stopped() && !network.stopped()) {
                timer.expires_after(std::chrono::seconds(2));
                auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
                if (ec) {
                    break;  // Timer cancelled
                }

                // Check if network is stopping
                if (network.stopped()) {
                    spdlog::debug("[parallel_sync] Peer watcher - network stopped, exiting");
                    break;
                }

                // Check for new peers and clean up dead ones
                auto current_peers = co_await network.peers().all();

                // Build set of currently connected (live) peer addresses
                boost::unordered_flat_set<std::string> live_addrs;
                for (auto const& peer : current_peers) {
                    if (!peer->stopped()) {
                        live_addrs.insert(peer->authority().to_string());
                    }
                }

                // Remove dead peers from active_peers so they can reconnect
                erase_if(active_peers, [&live_addrs](auto const& addr) {
                    return !live_addrs.contains(addr);
                });

                // Add new peers
                for (auto const& peer : current_peers) {
                    if (peer->stopped()) {
                        continue;
                    }
                    auto addr = peer->authority().to_string();
                    auto [it, inserted] = active_peers.insert(addr);
                    if (inserted) {
                        spdlog::debug("[parallel_sync] Spawning download loop for new peer [{}]", addr);
                        download_tasks.spawn(peer_download_loop(coordinator, peer, config));
                        spdlog::info("[parallel_sync] Added new peer [{}] to download pool (total: {})",
                            addr, active_peers.size());
                    }
                }
            }
            spdlog::debug("[parallel_sync] Peer watcher exiting");
        }());

        // Wait for all download loops to complete
        co_await download_tasks.join();

        // All downloads done - stop coordinator to unblock validation pipeline
        spdlog::debug("[parallel_sync] All download loops exited, stopping coordinator");
        coordinator.stop();
    }());

    // Spawn validation pipeline
    all_tasks.spawn(validation_pipeline(coordinator, chain));

    // Spawn timeout checker
    all_tasks.spawn(timeout_checker(coordinator, config));

    // Wait for all tasks to complete
    co_await all_tasks.join();

    // Build result
    auto progress = coordinator.get_progress();
    parallel_sync_result result{
        .error = coordinator.has_failed() ? coordinator.failure_reason() : error::success,
        .blocks_downloaded = progress.blocks_downloaded,
        .blocks_validated = progress.blocks_validated,
        .final_height = start_height + progress.blocks_validated - 1
    };

    if (coordinator.is_complete()) {
        spdlog::info("[parallel_sync] Parallel sync completed: {} blocks validated",
            progress.blocks_validated);
    } else if (coordinator.has_failed()) {
        spdlog::error("[parallel_sync] Parallel sync failed: {}",
            coordinator.failure_reason().message());
    } else {
        spdlog::warn("[parallel_sync] Parallel sync interrupted: {} blocks validated",
            progress.blocks_validated);
    }

    co_return result;
}

} // namespace kth::node
