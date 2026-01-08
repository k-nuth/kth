// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/header_tasks.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <vector>

#include <boost/unordered/unordered_flat_set.hpp>
#include <spdlog/spdlog.h>

#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/network/protocols_coro.hpp>

namespace kth::node::sync {

using namespace ::asio::experimental::awaitable_operators;

// =============================================================================
// Header Download Task (per-peer worker)
// =============================================================================

::asio::awaitable<void> header_download_task(
    network::peer_session::ptr peer,
    header_chunk_coordinator& coordinator,
    blockchain::header_organizer& organizer,
    std::atomic<uint32_t>& active_peers,
    header_download_task_output_channel& output
) {
    auto const peer_nonce = peer->nonce();
    auto const peer_name = peer->authority_with_agent();

    spdlog::debug("[header_task:{}] Started for peer {}", peer_nonce, peer_name);
    active_peers.fetch_add(1, std::memory_order_relaxed);

    // Download chunks until none available or peer disconnects
    while (!peer->stopped() && !coordinator.is_stopped()) {
        // Try to claim a chunk
        auto chunk_height = coordinator.claim_chunk();
        if (!chunk_height) {
            // No chunks available (at speculation limit or tip reached)
            // Wait a bit and retry
            auto timer = ::asio::steady_timer(co_await ::asio::this_coro::executor);
            timer.expires_after(std::chrono::milliseconds(100));
            co_await timer.async_wait(::asio::use_awaitable);

            // Check if sync is complete
            if (coordinator.tip_reached()) {
                spdlog::debug("[header_task:{}] Tip reached, exiting", peer_nonce);
                break;
            }
            continue;
        }

        uint32_t start_height = *chunk_height;
        spdlog::debug("[header_task:{}] Claimed chunk at height {}", peer_nonce, start_height);

        // Get the hash at start_height to request headers after it
        auto from_hash = organizer.index().get_hash(
            static_cast<blockchain::header_index::index_t>(start_height));

        // Measure download time
        auto start_time = std::chrono::steady_clock::now();

        // Request headers from peer
        auto result = co_await network::request_headers_from(
            *peer, from_hash, std::chrono::seconds(30));

        auto end_time = std::chrono::steady_clock::now();
        auto download_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();

        if (!result) {
            spdlog::debug("[header_task:{}] Failed to get headers from height {}: {}",
                peer_nonce, start_height, result.error().message());

            coordinator.report_chunk_failed(start_height);

            // Don't exit immediately - peer might recover for other chunks
            // But if too many failures, the peer will be disconnected by network layer
            continue;
        }

        uint32_t headers_received = static_cast<uint32_t>(result->elements().size());
        spdlog::debug("[header_task:{}] Received {} headers from height {} in {}ms",
            peer_nonce, headers_received, start_height, download_ms);

        // Report chunk completed to coordinator
        coordinator.report_chunk_completed(start_height, headers_received);

        // Send headers to output (if we got any)
        if (headers_received > 0) {
            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                downloaded_headers{
                    .headers = result->elements(),
                    .start_height = start_height + 1,  // Headers start after from_hash
                    .source_peer = peer
                },
                ::asio::as_tuple(::asio::use_awaitable)
            );
            if (send_ec) {
                spdlog::warn("[header_task:{}] Failed to send headers: {}", peer_nonce, send_ec.message());
                break;
            }
        }

        // Send performance report
        output.try_send(std::error_code{}, header_performance{
            .peer_nonce = peer_nonce,
            .headers_downloaded = headers_received,
            .download_time_ms = static_cast<uint32_t>(download_ms)
        });

        // If we got fewer headers than a full chunk, we've hit the tip
        if (headers_received == 0) {
            spdlog::debug("[header_task:{}] Got empty response, tip reached", peer_nonce);
            break;
        }
    }

    // Task ending - report to supervisor
    active_peers.fetch_sub(1, std::memory_order_relaxed);
    output.try_send(std::error_code{}, header_download_task_ended{.peer_nonce = peer_nonce});
    spdlog::debug("[header_task:{}] Ended", peer_nonce);
}

// =============================================================================
// Header Download Supervisor
// =============================================================================

::asio::awaitable<void> header_download_supervisor(
    header_download_input_channel& input,
    header_download_channel& output,
    blockchain::header_organizer& organizer
) {
    spdlog::debug("[header_supervisor] Started");

    auto executor = co_await ::asio::this_coro::executor;

    // Current peer list (updated by peer_provider)
    std::vector<network::peer_session::ptr> available_peers;

    // Track which peers have spawned tasks (by nonce)
    boost::unordered_flat_set<uint64_t> spawned_peers;

    // Coordinator (created when start_header_sync arrives)
    std::unique_ptr<header_chunk_coordinator> coordinator;

    // Active peer counter for metrics
    std::atomic<uint32_t> active_peers{0};

    // Task output channel (all per-peer tasks send here)
    header_download_task_output_channel task_output(executor, 1000);

    // Unified event channel (combines input + task_output)
    header_supervisor_event_channel events(executor, 1000);

    // Timer for periodic checks
    ::asio::steady_timer timer(executor);
    bool timer_running = false;

    // Bridge: input -> events
    auto input_bridge = [&]() -> ::asio::awaitable<void> {
        spdlog::debug("[header_supervisor:input_bridge] Started");
        while (true) {
            auto [ec, msg] = co_await input.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[header_supervisor:input_bridge] Channel closed");
                break;
            }
            // Forward to unified events
            std::visit([&](auto&& m) {
                events.try_send(std::error_code{}, std::forward<decltype(m)>(m));
            }, msg);
        }
    };

    // Bridge: task_output -> events
    auto task_bridge = [&]() -> ::asio::awaitable<void> {
        spdlog::debug("[header_supervisor:task_bridge] Started");
        while (true) {
            auto [ec, msg] = co_await task_output.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[header_supervisor:task_bridge] Channel closed");
                break;
            }
            // Forward to unified events
            std::visit([&](auto&& m) {
                events.try_send(std::error_code{}, std::forward<decltype(m)>(m));
            }, msg);
        }
    };

    // Timer task
    auto timer_task = [&]() -> ::asio::awaitable<void> {
        spdlog::debug("[header_supervisor:timer] Started");
        while (!coordinator || !coordinator->is_stopped()) {
            timer.expires_after(std::chrono::seconds(5));
            auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                break;  // Timer cancelled
            }
            events.try_send(std::error_code{}, supervisor_timeout{});
        }
        spdlog::debug("[header_supervisor:timer] Ended");
    };

    // Task group for per-peer download tasks
    task_group peer_tasks(executor);

    // Start bridges (they run in parallel with main loop)
    task_group bridges(executor);
    bridges.spawn(input_bridge());
    bridges.spawn(task_bridge());

    // Helper to spawn tasks for new peers
    auto spawn_peer_tasks = [&]() {
        if (!coordinator) return;

        for (auto const& peer : available_peers) {
            if (peer->stopped()) continue;
            if (spawned_peers.contains(peer->nonce())) continue;

            // New peer - spawn download task
            spawned_peers.insert(peer->nonce());
            spdlog::debug("[header_supervisor] Spawning task for peer {}",
                peer->authority_with_agent());

            peer_tasks.spawn(header_download_task(
                peer,
                *coordinator,
                organizer,
                active_peers,
                task_output
            ));
        }
    };

    // Main event loop
    while (true) {
        auto [ec, event] = co_await events.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            spdlog::debug("[header_supervisor] Events channel closed");
            break;
        }

        // Process event
        if (std::holds_alternative<stop_request>(event)) {
            spdlog::debug("[header_supervisor] Stop request received");
            if (coordinator) {
                coordinator->stop();
            }
            break;
        }

        if (auto* peers = std::get_if<peers_updated>(&event)) {
            available_peers = peers->peers;
            spdlog::debug("[header_supervisor] Peers updated: {} available", available_peers.size());

            // Clean up spawned_peers - remove peers that are no longer available
            erase_if(spawned_peers, [&](uint64_t nonce) {
                return std::none_of(available_peers.begin(), available_peers.end(),
                    [nonce](auto const& p) { return p->nonce() == nonce; });
            });

            // Spawn tasks for new peers
            spawn_peer_tasks();
            continue;
        }

        if (auto* start = std::get_if<start_header_sync>(&event)) {
            spdlog::info("[header_supervisor] Starting header sync from height {}", start->from_height);

            // Create new coordinator
            coordinator = std::make_unique<header_chunk_coordinator>(
                start->from_height,
                header_chunk_config{
                    .chunk_size = 2000,
                    .speculative_chunks = 3,
                    .max_peers = 8,
                    .stall_timeout_secs = 30
                }
            );

            // Start timer task
            if (!timer_running) {
                timer_running = true;
                bridges.spawn(timer_task());
            }

            // Spawn tasks for available peers
            spawn_peer_tasks();
            continue;
        }

        if (auto* headers = std::get_if<downloaded_headers>(&event)) {
            // Forward headers to output
            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                *headers,
                ::asio::as_tuple(::asio::use_awaitable)
            );
            if (send_ec) {
                spdlog::warn("[header_supervisor] Failed to forward headers: {}", send_ec.message());
            }
            continue;
        }

        if (auto* perf = std::get_if<header_performance>(&event)) {
            // Forward performance to output
            output.try_send(std::error_code{}, *perf);
            continue;
        }

        if (auto* ended = std::get_if<header_download_task_ended>(&event)) {
            // Task ended - remove from spawned set so it can be respawned if peer reconnects
            spawned_peers.erase(ended->peer_nonce);
            spdlog::debug("[header_supervisor] Task ended for peer {}", ended->peer_nonce);

            // Check if sync is complete
            if (coordinator && coordinator->is_complete()) {
                spdlog::info("[header_supervisor] Header sync complete!");
                break;
            }
            continue;
        }

        if (std::holds_alternative<supervisor_timeout>(event)) {
            if (coordinator) {
                // Check for stalled chunks
                coordinator->check_timeouts();

                // Log progress
                auto progress = coordinator->get_progress();
                spdlog::debug("[header_supervisor] Progress: validated={}, discovered={}, "
                    "next={}, in_progress={}, tip={}",
                    progress.validated_height, progress.discovered_height,
                    progress.next_chunk_height, progress.chunks_in_progress,
                    progress.tip_reached ? "yes" : "no");

                // Spawn tasks for any new peers
                spawn_peer_tasks();
            }
            continue;
        }
    }

    // Shutdown
    spdlog::debug("[header_supervisor] Shutting down...");

    // Stop coordinator
    if (coordinator) {
        coordinator->stop();
    }

    // Cancel timer
    timer.cancel();

    // Close task output to stop bridges
    task_output.close();

    // Close events to unblock bridges
    events.close();

    // Wait for bridges
    co_await bridges.join();

    // Wait for peer tasks (they should exit when coordinator is stopped)
    co_await peer_tasks.join();

    // Close output
    output.close();

    spdlog::debug("[header_supervisor] Ended");
}

// =============================================================================
// Header Validation Task
// =============================================================================

::asio::awaitable<void> header_validation_task(
    blockchain::header_organizer& organizer,
    uint32_t start_height,
    header_validation_input_channel& input,
    header_validated_channel& output
) {
    spdlog::debug("[header_validation] Task started, expecting headers from height {}", start_height + 1);

    // Buffer for out-of-order headers (speculative chunks may arrive before sequential ones)
    // Key: start_height, Value: downloaded_headers
    std::map<uint32_t, downloaded_headers> buffer;

    uint32_t expected_height = start_height + 1;

    // Helper to process buffered headers in order
    auto process_buffer = [&]() -> ::asio::awaitable<bool> {
        while (true) {
            auto it = buffer.find(expected_height);
            if (it == buffer.end()) {
                co_return true;  // No more headers at expected height
            }

            auto& downloaded = it->second;
            spdlog::debug("[header_validation] Processing buffered headers at height {} ({} headers)",
                expected_height, downloaded.headers.size());

            // Validate and add headers
            auto result = organizer.add_headers(downloaded.headers);

            spdlog::debug("[header_validation] Added {} headers at height {}, total index: {}",
                result.headers_added, expected_height, result.index_size);

            uint32_t validated_to = expected_height + result.headers_added - 1;

            // Send validation result
            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                headers_validated{
                    .height = validated_to,
                    .count = result.headers_added,
                    .result = result.error,
                    .source_peer = downloaded.source_peer
                },
                ::asio::as_tuple(::asio::use_awaitable)
            );

            if (send_ec) {
                spdlog::warn("[header_validation] Failed to send result: {}", send_ec.message());
                co_return false;
            }

            // Move to next expected height
            expected_height += result.headers_added;

            // Remove processed entry
            buffer.erase(it);

            // If validation error, stop
            if (result.error) {
                spdlog::error("[header_validation] Validation error at height {}: {}",
                    expected_height, result.error.message());
                co_return false;
            }
        }
    };

    // Main loop
    while (true) {
        auto [ec, msg] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            spdlog::debug("[header_validation] Input channel closed");
            break;
        }

        if (std::holds_alternative<stop_request>(msg)) {
            spdlog::debug("[header_validation] Stop signal received");
            break;
        }

        if (auto* downloaded = std::get_if<downloaded_headers>(&msg)) {
            spdlog::debug("[header_validation] Received {} headers at height {}",
                downloaded->headers.size(), downloaded->start_height);

            if (downloaded->headers.empty()) {
                // Empty response - tip reached, send signal to coordinator
                auto [send_ec] = co_await output.async_send(
                    std::error_code{},
                    headers_validated{
                        .height = expected_height - 1,
                        .count = 0,
                        .result = error::success,
                        .source_peer = downloaded->source_peer
                    },
                    ::asio::as_tuple(::asio::use_awaitable)
                );
                continue;
            }

            // Check if this is the expected height
            if (downloaded->start_height == expected_height) {
                // Process immediately
                auto result = organizer.add_headers(downloaded->headers);

                spdlog::debug("[header_validation] Added {} headers at height {}, total index: {}",
                    result.headers_added, expected_height, result.index_size);

                uint32_t validated_to = expected_height + result.headers_added - 1;

                auto [send_ec] = co_await output.async_send(
                    std::error_code{},
                    headers_validated{
                        .height = validated_to,
                        .count = result.headers_added,
                        .result = result.error,
                        .source_peer = downloaded->source_peer
                    },
                    ::asio::as_tuple(::asio::use_awaitable)
                );

                if (send_ec) {
                    spdlog::warn("[header_validation] Failed to send result: {}", send_ec.message());
                    break;
                }

                expected_height += result.headers_added;

                if (result.error) {
                    spdlog::error("[header_validation] Validation error: {}", result.error.message());
                    break;
                }

                // Process any buffered headers that can now be validated
                if (!co_await process_buffer()) {
                    break;
                }
            } else if (downloaded->start_height > expected_height) {
                // Out of order - buffer it
                spdlog::debug("[header_validation] Buffering headers at height {} (expected {})",
                    downloaded->start_height, expected_height);
                buffer[downloaded->start_height] = std::move(*downloaded);
            } else {
                // Headers we already have - discard (duplicate from speculative download)
                spdlog::debug("[header_validation] Discarding duplicate headers at height {} (expected {})",
                    downloaded->start_height, expected_height);
            }
        }
    }

    // Close output
    output.close();
    spdlog::debug("[header_validation] Task ended");
}

} // namespace kth::node::sync
