// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/parallel_sync_v2.hpp>

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
// Peer Download Loop (Lock-Free)
// =============================================================================

::asio::awaitable<void> peer_download_loop_v2(
    block_download_coordinator_v2& coordinator,
    network::peer_session::ptr peer,
    parallel_download_config_v2 config)  // Pass by value to ensure lifetime
{
    spdlog::info("[parallel_sync_v2] Download loop ENTERING for peer [{}], coordinator at {}",
        peer->authority(), static_cast<void const*>(&coordinator));

    auto executor = co_await ::asio::this_coro::executor;

    spdlog::info("[parallel_sync_v2] Download loop got executor for peer [{}], coordinator at {}",
        peer->authority(), static_cast<void const*>(&coordinator));

    coordinator.peer_started();
    spdlog::info("[parallel_sync_v2] Download loop STARTED for peer [{}] (active peers: {})",
        peer->authority(), coordinator.get_progress().active_peers);

    // RAII guard to decrement peer count on exit
    struct peer_guard {
        block_download_coordinator_v2& coord;
        std::string addr;
        ~peer_guard() {
            coord.peer_stopped();
            spdlog::info("[parallel_sync_v2] Download loop ENDED for peer [{}] (active peers: {})",
                addr, coord.get_progress().active_peers);
        }
    } guard{coordinator, peer->authority().to_string()};

    while (!coordinator.is_complete() && !coordinator.has_failed() && !peer->stopped()) {
        // Claim a chunk (lock-free!)
        auto chunk_opt = coordinator.claim_chunk();

        if (!chunk_opt) {
            // No chunks available - wait a bit and retry
            spdlog::debug("[parallel_sync_v2] Peer [{}] - no chunks available, waiting...",
                peer->authority());
            ::asio::steady_timer timer(executor);
            timer.expires_after(std::chrono::milliseconds(100));
            co_await timer.async_wait(::asio::use_awaitable);
            continue;
        }

        uint32_t chunk_id = *chunk_opt;
        auto [block_start, block_end] = coordinator.chunk_range(chunk_id);

        spdlog::debug("[parallel_sync_v2] Peer [{}] claimed chunk {} (blocks {}-{})",
            peer->authority(), chunk_id, block_start, block_end);

        // Build block request list
        std::vector<std::pair<uint32_t, hash_digest>> blocks;
        for (uint32_t h = block_start; h <= block_end; ++h) {
            auto hash = coordinator.get_block_hash(h);
            if (hash == null_hash) {
                spdlog::error("[parallel_sync_v2] No hash for height {}", h);
                coordinator.chunk_failed(chunk_id);
                continue;
            }
            blocks.emplace_back(h, hash);
        }

        if (blocks.empty()) {
            coordinator.chunk_completed(chunk_id);
            continue;
        }

        // Request blocks from peer
        auto batch_result = co_await network::request_blocks_batch(
            *peer, blocks, config.stall_timeout);

        if (!batch_result) {
            spdlog::warn("[parallel_sync_v2] Failed to get blocks from peer [{}]: {}",
                peer->authority(), batch_result.error().message());
            coordinator.chunk_failed(chunk_id);
            peer->stop(batch_result.error());
            co_return;
        }

        spdlog::debug("[parallel_sync_v2] Peer [{}] received {} blocks for chunk {}",
            peer->authority(), batch_result->size(), chunk_id);

        // Report received blocks
        for (auto& block_with_h : *batch_result) {
            auto block_ptr = std::make_shared<domain::message::block const>(
                std::move(block_with_h.block));
            coordinator.block_received(block_with_h.height, block_ptr);
        }

        // Mark chunk as completed
        coordinator.chunk_completed(chunk_id);
    }
    // peer_guard destructor will log exit and decrement peer count
}

// =============================================================================
// Validation Pipeline
// =============================================================================

::asio::awaitable<void> validation_pipeline_v2(
    block_download_coordinator_v2& coordinator,
    blockchain::block_chain& chain)
{
    spdlog::debug("[parallel_sync_v2] Starting validation pipeline");

    while (true) {
        auto block_opt = co_await coordinator.next_block_to_validate();

        if (!block_opt) {
            break;
        }

        auto const& [height, block] = *block_opt;

        spdlog::trace("[parallel_sync_v2] Validating block at height {}", height);

        auto result = co_await chain.organize(block, /*headers_pre_validated=*/true);

        coordinator.validation_complete(height, result);

        if (result != error::success) {
            spdlog::error("[parallel_sync_v2] Block validation failed at height {}: {}",
                height, result.message());
            break;
        }

        // Log progress periodically
        if (height % 1000 == 0) {
            auto progress = coordinator.get_progress();
            auto now = std::chrono::steady_clock::now();
            auto elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(
                now - progress.start_time).count();

            double blocks_per_sec = elapsed_secs > 0
                ? static_cast<double>(progress.blocks_validated) / static_cast<double>(elapsed_secs)
                : 0.0;

            uint32_t total_blocks = progress.target_height - progress.start_height + 1;
            uint32_t remaining = total_blocks - progress.blocks_validated;
            uint32_t eta_secs = blocks_per_sec > 0
                ? static_cast<uint32_t>(static_cast<double>(remaining) / blocks_per_sec)
                : 0;

            uint32_t eta_hours = eta_secs / 3600;
            uint32_t eta_mins = (eta_secs % 3600) / 60;
            uint32_t eta_s = eta_secs % 60;

            double percent = 100.0 * static_cast<double>(progress.blocks_validated) /
                             static_cast<double>(total_blocks);

            spdlog::info("[parallel_sync_v2] Height: {} ({:.1f}%) | {:.0f} blk/s | ETA: {:02d}:{:02d}:{:02d} | peers: {} | in-flight: {} | pending: {}",
                height, percent, blocks_per_sec, eta_hours, eta_mins, eta_s,
                progress.active_peers, progress.chunks_in_progress, progress.blocks_pending);
        }
    }

    spdlog::debug("[parallel_sync_v2] Validation pipeline completed");
}

// =============================================================================
// Timeout Checker
// =============================================================================

::asio::awaitable<void> timeout_checker_v2(
    block_download_coordinator_v2& coordinator,
    parallel_download_config_v2 const& config)
{
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);

    spdlog::debug("[parallel_sync_v2] Starting timeout checker");

    while (!coordinator.is_complete() && !coordinator.has_failed() && !coordinator.is_stopped()) {
        timer.expires_after(config.timeout_check_interval);
        auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            break;
        }

        coordinator.check_timeouts();
    }

    spdlog::debug("[parallel_sync_v2] Timeout checker completed");
}

} // anonymous namespace

// =============================================================================
// Main Parallel Sync V2 Function
// =============================================================================

::asio::awaitable<parallel_sync_result_v2> parallel_block_sync_v2(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    network::p2p_node& network,
    uint32_t start_height,
    uint32_t target_height,
    parallel_download_config_v2 const& config)
{
    auto executor = co_await ::asio::this_coro::executor;

    spdlog::info("[parallel_sync_v2] Starting parallel block sync from {} to {} ({} blocks)",
        start_height, target_height, target_height - start_height + 1);

    // Create coordinator
    block_download_coordinator_v2 coordinator(
        chain, organizer, start_height, target_height, executor, config);

    spdlog::info("[parallel_sync_v2] Coordinator created at address {}", static_cast<void const*>(&coordinator));

    // Get available peers
    auto peers = co_await network.peers().all();
    if (peers.empty()) {
        spdlog::error("[parallel_sync_v2] No peers available for block download");
        co_return parallel_sync_result_v2{
            .error = error::channel_stopped,
            .blocks_downloaded = 0,
            .blocks_validated = 0,
            .final_height = start_height
        };
    }

    spdlog::info("[parallel_sync_v2] Using {} peers for parallel download", peers.size());

    kth::task_group all_tasks(executor);

    // Spawn download supervisor
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        kth::task_group download_tasks(executor);

        boost::unordered_flat_set<std::string> active_peers;

        // Start initial peers
        for (auto const& peer : peers) {
            auto addr = peer->authority().to_string();
            active_peers.insert(addr);
            spdlog::info("[parallel_sync_v2] Starting download loop for initial peer [{}]", addr);
            download_tasks.spawn(peer_download_loop_v2(coordinator, peer, config));
        }

        // Peer watcher - monitors for new peer connections and adds them to download pool
        download_tasks.spawn([&]() -> ::asio::awaitable<void> {
            auto exec = co_await ::asio::this_coro::executor;
            ::asio::steady_timer timer(exec);

            spdlog::info("[parallel_sync_v2] Peer watcher STARTED");

            while (true) {
                // Check exit conditions with logging
                if (coordinator.is_complete()) {
                    spdlog::info("[parallel_sync_v2] Peer watcher exiting: coordinator complete");
                    break;
                }
                if (coordinator.has_failed()) {
                    spdlog::info("[parallel_sync_v2] Peer watcher exiting: coordinator failed");
                    break;
                }
                if (coordinator.is_stopped()) {
                    spdlog::info("[parallel_sync_v2] Peer watcher exiting: coordinator stopped");
                    break;
                }
                if (network.stopped()) {
                    spdlog::info("[parallel_sync_v2] Peer watcher exiting: network stopped");
                    break;
                }

                timer.expires_after(std::chrono::seconds(2));
                auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
                if (ec) {
                    spdlog::info("[parallel_sync_v2] Peer watcher exiting: timer cancelled (ec={})", ec.message());
                    break;
                }

                auto current_peers = co_await network.peers().all();

                boost::unordered_flat_set<std::string> live_addrs;
                size_t stopped_count = 0;
                for (auto const& peer : current_peers) {
                    if (!peer->stopped()) {
                        live_addrs.insert(peer->authority().to_string());
                    } else {
                        ++stopped_count;
                    }
                }

                spdlog::info("[parallel_sync_v2] Peer watcher check: {} total, {} live, {} stopped, {} tracked",
                    current_peers.size(), live_addrs.size(), stopped_count, active_peers.size());

                // Remove disconnected peers from tracking
                auto removed = erase_if(active_peers, [&live_addrs](auto const& addr) {
                    return !live_addrs.contains(addr);
                });
                if (removed > 0) {
                    spdlog::info("[parallel_sync_v2] Removed {} disconnected peers from tracking", removed);
                }

                // Add new peers
                for (auto const& peer : current_peers) {
                    if (peer->stopped()) continue;
                    auto addr = peer->authority().to_string();
                    auto [it, inserted] = active_peers.insert(addr);
                    if (inserted) {
                        spdlog::info("[parallel_sync_v2] Adding new peer [{}] to download pool (tracked: {})",
                            addr, active_peers.size());
                        try {
                            download_tasks.spawn(peer_download_loop_v2(coordinator, peer, config));
                            spdlog::info("[parallel_sync_v2] Spawn completed for peer [{}]", addr);
                        } catch (std::exception const& e) {
                            spdlog::error("[parallel_sync_v2] Exception spawning peer [{}]: {}", addr, e.what());
                        }
                    }
                }
            }
        }());

        co_await download_tasks.join();

        spdlog::debug("[parallel_sync_v2] All download loops exited, stopping coordinator");
        coordinator.stop();
    }());

    // Spawn validation pipeline
    all_tasks.spawn(validation_pipeline_v2(coordinator, chain));

    // Spawn timeout checker
    all_tasks.spawn(timeout_checker_v2(coordinator, config));

    // Wait for all tasks
    co_await all_tasks.join();

    // Build result
    auto progress = coordinator.get_progress();
    parallel_sync_result_v2 result{
        .error = coordinator.has_failed() ? coordinator.failure_reason() : error::success,
        .blocks_downloaded = progress.chunks_completed * static_cast<uint32_t>(config.chunk_size),
        .blocks_validated = progress.blocks_validated,
        .final_height = start_height + progress.blocks_validated - 1
    };

    if (coordinator.is_complete()) {
        spdlog::info("[parallel_sync_v2] Parallel sync completed: {} blocks validated",
            progress.blocks_validated);
    } else if (coordinator.has_failed()) {
        spdlog::error("[parallel_sync_v2] Parallel sync failed: {}",
            coordinator.failure_reason().message());
    } else {
        spdlog::warn("[parallel_sync_v2] Parallel sync interrupted: {} blocks validated",
            progress.blocks_validated);
    }

    co_return result;
}

} // namespace kth::node
