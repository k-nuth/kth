// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/orchestrator.hpp>

#include <kth/node/sync/reorg.hpp>

#include <chrono>
#include <thread>

#include <boost/unordered/unordered_flat_set.hpp>
#include <spdlog/spdlog.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <kth/infrastructure/utility/task_group.hpp>
// #include <kth/blockchain/utxo_builder.hpp>
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
        case error::stale_chain:            // Stale/duplicate batch, not malicious
            return false;
        default:
            // Any other error is a validation failure -> BAN
            return true;
    }
}

// The hash of the block at `height` on the active chain.
//
// A locator is built from a height, and the header index numbers its entries in
// arrival order — so once a side branch is stored an entry's index is no longer
// its height. Casting one to the other would ask peers to continue from whatever
// block happens to sit at that index, which after a fork is a block the node did
// not take.
static
hash_digest active_hash_at(blockchain::header_index const& index, uint32_t height) {
    auto const idx = index.active_at(static_cast<int32_t>(height));
    if (idx == blockchain::header_index::null_index) {
        // Every height up to the header tip is on the active chain, so this means
        // the chain is shorter than the coordinator believes. Say so: the request
        // below will start from genesis, which is recoverable but wasteful.
        spdlog::error("[sync_coordinator] Height {} is not on the active chain", height);
        return null_hash;
    }
    return index.get_hash(idx);
}

// =============================================================================
// May the post-checkpoint range start? (#663)
// =============================================================================

slow_sync_admission may_start_slow_sync(
    uint32_t start_height,
    std::expected<uint32_t, database::result_code> const& built) {

    if ( ! built) {
        // Fail-closed, and both reasons land here on purpose. `key_not_found`
        // means no marker has been published yet, which is a store that has not
        // built anything this side of a rebuild; a fault means the store would
        // not answer. Neither is "far enough", and reading either as permission
        // is how a validator ends up judging blocks against a set it cannot
        // describe.
        return slow_sync_admission::height_unavailable;
    }

    // H needs the set at H - 1, so the first block of the range needs
    // start_height - 1. Equality is enough and is the whole point: demanding
    // start_height would wait for the block the range has not downloaded yet, and
    // the range would never start.
    //
    // `start_height == 0` FIRST, so the subtraction below cannot underflow. It
    // cannot arrive today — every call site passes `blocks_synced_to + 1` — and a
    // range starting at genesis needs nothing built ahead of it, which is the
    // honest answer rather than a guard against a caller that does not exist.
    if (start_height == 0) {
        return slow_sync_admission::start;
    }

    // The arithmetic is on the CONSTANT side, not the measured one. `*built + 1`
    // wraps to 0 when the builder reports UINT32_MAX, which would report the
    // highest set representable as behind — fail-closed, and still wrong.
    if (*built >= start_height - 1) {
        return slow_sync_admission::start;
    }
    return slow_sync_admission::builder_behind;
}

utxo_progress_step utxo_progress_for_tick(
    std::optional<uint32_t> last_seen,
    std::expected<uint32_t, database::result_code> const& built) {

    if ( ! built) {
        // Nothing to announce, AND the memory is cleared. Keeping it would let a
        // transient read failure hide the next successful reading of the same
        // height — and the coordinator may well be holding on
        // `height_unavailable` at that moment, with no other event coming.
        return {std::nullopt, std::nullopt};
    }
    if (last_seen && *last_seen == *built) {
        return {std::nullopt, last_seen};
    }
    return {*built, *built};
}

char const* to_string(slow_sync_admission admission) {
    switch (admission) {
        case slow_sync_admission::start:              return "start";
        case slow_sync_admission::builder_behind:     return "builder_behind";
        case slow_sync_admission::height_unavailable: return "height_unavailable";
    }
    return "invalid slow_sync_admission";
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
    constexpr uint32_t chunk = 10000;

    // This task writes the by-height header table, which a switch rewrites, so it
    // takes part in the barrier. While it is registered and not parked a switch
    // waits — which is what stops a chunk already under way from committing the
    // old branch on top of the new one. Checking the generation alone could not:
    // the switch can land between the check and the commit.
    reorg_participation const participation(chain);

    uint32_t height = start_height;
    while (height <= end_height) {
        auto const chunk_end = std::min(height + chunk - 1, end_height);

        // Park, then give up: the switch rewrites the range it replaced itself,
        // and a later header sync re-persists whatever is above it.
        if (chain.reorg_pause_requested()) {
            spdlog::info("[header_persist] A reorg is executing; stopping at height {}", height);
            chain.enter_reorg_barrier();
            while (chain.reorg_pause_requested() && ! chain.stopped()) {
                ::asio::steady_timer timer(co_await ::asio::this_coro::executor);
                timer.expires_after(std::chrono::milliseconds(50));
                co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            }
            chain.leave_reorg_barrier();
            co_return;
        }

        if ( ! persist_active_headers(chain, index, height, chunk_end)) {
            co_return;
        }
        height = chunk_end + 1;

        // Yield to allow other coroutines to run
        co_await ::asio::post(co_await ::asio::this_coro::executor, ::asio::use_awaitable);
    }

    auto const elapsed = std::chrono::steady_clock::now() - start_time;
    auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    auto const rate = elapsed_secs > 0 ? total / elapsed_secs : total;
    spdlog::info("[header_persist] Complete: {} headers in {}s ({}/s)", total, elapsed_secs, rate);
}

// =============================================================================
// Sync Orchestrator
// =============================================================================

::asio::awaitable<void> sync_orchestrator(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    kth::node::p2p_node& network,
    domain::config::network network_type,
    std::function<void(std::string const&)> on_fatal
) {
    auto executor = co_await ::asio::this_coro::executor;

    // 2026-02-07: Log which executor is being used for debugging
    spdlog::info("[sync_orchestrator] Starting CSP-based sync system (thread_id={})",
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    spdlog::debug("[sync_orchestrator] this_coro::executor type: {}", typeid(executor).name());
    spdlog::debug("[sync_orchestrator] Network threadpool executor type: {}",
        typeid(network.thread_pool().get_executor()).name());
    spdlog::debug("[sync_orchestrator] Network threadpool has {} threads", network.thread_pool().size());

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

    // Fast validation pipeline (chunk-based, bypasses supervisor→bridge→validation)
    // Capacity 256: storage task is slower than validation (serial disk I/O),
    // so we need buffer to absorb the speed difference. 256×16=4096 blocks max buffered.
    fast_validation_input_channel fast_val_input(executor, 256);
    chunk_validated_channel validated_chunks(executor, 32);

    // Block storage pipeline (writes validated chunks to flat files)
    // Capacity 128: buffer between fast parallel validation and serial disk writes
    block_storage_input_channel block_storage_input(executor, 128);
    chunk_validated_channel stored_chunks(executor, 32);

    // Control
    stop_channel stop_signal(executor, 1);

    // Sync coordinator unified event channel (created here so peer_provider can close it during shutdown)
    sync_coordinator_event_channel coordinator_events(executor, 100);

    // =========================================================================
    // TASKS - All independent, communicate only via channels
    // =========================================================================

    task_group all_tasks("orchestrator_tasks", executor);

    // -------------------------------------------------------------------------
    // 1. Peer provider - receives connected peers from network and distributes
    // -------------------------------------------------------------------------
    // Unified input channel for peer_provider (CSP pattern - single channel)
    peer_provider_input_channel peer_provider_input(executor, 100);

    // Bridge task: forwards from network.peer_events() to unified channel
    // 2026-02-07: Use simple timer polling instead of || operator to avoid potential issues
    all_tasks.spawn("peer_bridge", [&]() -> ::asio::awaitable<void> {
        spdlog::info("[peer_bridge] Started, forwarding peer events to peer_provider");
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
                if (notification.event == kth::node::peer_event_type::connected) {
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
            spdlog::error("[peer_bridge] EXCEPTION: {}", e.what());
        } catch (...) {
            spdlog::error("[peer_bridge] UNKNOWN EXCEPTION");
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
    // 2026-02-07: Added comprehensive exception handling for debugging
    all_tasks.spawn("peer_provider", [&]() -> ::asio::awaitable<void> {
        spdlog::info("[peer_provider] Task started, waiting for peers...");

        // Local peer list for broadcasting to sync tasks
        std::vector<network::peer_session::ptr> peers;

        // Broadcast current peer list to download tasks
        auto broadcast_peers = [&]() {
            // Remove stopped peers
            std::erase_if(peers, [](auto const& p) { return p->stopped(); });

            // Filter out slow peers for block download (threshold: 500ms/block, min 3 samples)
            std::vector<network::peer_session::ptr> fast_peers;
            fast_peers.reserve(peers.size());
            size_t slow_count = 0;
            for (auto const& p : peers) {
                if (network.is_slow_peer(p->authority())) {
                    ++slow_count;
                    auto speed = network.get_peer_speed(p->authority());
                    spdlog::debug("[peer_provider] Excluding slow peer {} ({:.1f}ms/block)",
                        p->authority(), speed);
                } else {
                    fast_peers.push_back(p);
                }
            }

            if (slow_count > 0) {
                spdlog::info("[peer_provider] Broadcasting {} peers ({} slow excluded) to sync tasks",
                    fast_peers.size(), slow_count);
            } else {
                spdlog::info("[peer_provider] Broadcasting {} peers to sync tasks", peers.size());
            }

            // Headers: send all peers (headers are small, slow is less of an issue)
            if (!header_download_input.try_send(std::error_code{}, peers_updated{peers})) {
                spdlog::warn("[peer_provider] Channel full, peers_updated dropped for header_download");
            }
            // Blocks: send only fast peers (slow peers degrade throughput significantly)
            if (!block_download_input.try_send(std::error_code{}, peers_updated{fast_peers})) {
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
                    bool found = false;
                    for (auto const& p : peers) {
                        if (p->nonce() == perf->peer_nonce) {
                            network.record_peer_performance(p, perf->blocks_downloaded, perf->download_time_ms);
                            auto speed = network.get_peer_speed(p->authority());
                            spdlog::debug("[peer_provider] Perf recorded: {} blocks={}  time={}ms  avg={:.1f}ms/blk",
                                p->authority(), perf->blocks_downloaded, perf->download_time_ms, speed);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        spdlog::debug("[peer_provider] Perf for unknown nonce {}", perf->peer_nonce);
                    }

                } else if (auto* hperf = std::get_if<header_performance>(&msg)) {
                    // Header performance - just log, not used for eviction
                    spdlog::debug("[peer_provider] Header perf: peer={}, headers={}, time={}ms",
                        hperf->peer_nonce, hperf->headers_downloaded, hperf->download_time_ms);
                }
            }
        } catch (std::exception const& e) {
            spdlog::error("[peer_provider] EXCEPTION: {}", e.what());
        } catch (...) {
            spdlog::error("[peer_provider] UNKNOWN EXCEPTION");
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
        fast_val_input.cancel();
        fast_val_input.close();
        validated_chunks.cancel();
        validated_chunks.close();
        block_storage_input.cancel();
        block_storage_input.close();
        stored_chunks.cancel();
        stored_chunks.close();
        coordinator_events.cancel();
        coordinator_events.close();

        spdlog::info("[peer_provider] Task ended");
    });

    // -------------------------------------------------------------------------
    // 2. Header download task (1 input, 1 output)
    //    Sequential header download with sticky peer selection
    // -------------------------------------------------------------------------
    auto const max_header_height = 0u;  // 0 = unlimited, download all headers from peers
    all_tasks.spawn("header_download_task", header_download_task(
        header_download_input,
        header_download_output,
        max_header_height
    ));

    // -------------------------------------------------------------------------
    // 3. Header download output bridge (demultiplexes headers, failures, and performance)
    //    - downloaded_headers → header_validation_input
    //    - peer_failure_report → header_validation_input
    //    - header_performance → peer_provider_input
    // -------------------------------------------------------------------------
    all_tasks.spawn("header_bridge", [&]() -> ::asio::awaitable<void> {
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
    all_tasks.spawn("header_validation_task", header_validation_task(
        organizer,
        header_validation_input,
        validated_headers
    ));

    // -------------------------------------------------------------------------
    // 5. Block download supervisor (1 input, 1 output)
    // -------------------------------------------------------------------------
    all_tasks.spawn("block_download_supervisor", block_download_supervisor(
        block_download_input,
        downloaded_blocks,
        organizer,
        &fast_val_input  // chunk-based fast validation
    ));

    // -------------------------------------------------------------------------
    // 6. Block supervisor output bridge (demultiplexes blocks and performance)
    //    - downloaded_block → block_validation_input
    //    - peer_performance → peer_provider_input
    // -------------------------------------------------------------------------
    // 2026-02-07: Note - with block validation disabled (#if 0 in block_tasks.cpp),
    // this task will wait forever on downloaded_blocks.async_receive() because
    // block_supervisor never sends to the output channel. This is expected behavior
    // when benchmarking download speed only. The task will be unblocked when
    // peer_provider closes the channel during shutdown.
    all_tasks.spawn("block_bridge", [&]() -> ::asio::awaitable<void> {
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
            spdlog::error("[block_bridge] EXCEPTION: {}", e.what());
        } catch (...) {
            spdlog::error("[block_bridge] UNKNOWN EXCEPTION");
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
    // max_checkpoint_height comes from the checkpoint list in parser.hpp
    uint32_t const checkpoint_height = uint32_t(chain.chain_settings().max_checkpoint_height);
    spdlog::info("[sync_orchestrator] Fast IBD mode up to checkpoint height {}", checkpoint_height);

    // UTXO build now happens incrementally inside block_storage_task as
    // each block lands; no startup-time bulk build is needed.

    all_tasks.spawn("block_validation_task", block_validation_task(
        chain,
        block_validation_input,
        validated_blocks,
        initial_block_height + 1,
        checkpoint_height
    ));

    // -------------------------------------------------------------------------
    // 7b. Fast validation task (chunk-based, parallel merkle)
    //     Forwards valid chunks to block_storage_task (with block data)
    //     Sends invalid chunks directly to coordinator via validated_chunks
    // -------------------------------------------------------------------------
    all_tasks.spawn("fast_validation_task", fast_validation_task(
        chain,
        fast_val_input,
        validated_chunks,
        &block_storage_input  // forward valid chunks to storage
    ));

    // -------------------------------------------------------------------------
    // 7c. Block storage task (writes validated blocks to flat files)
    //     Receives from fast_validation_task, stores sequentially, notifies coordinator
    // -------------------------------------------------------------------------
    auto contiguous_height = std::make_shared<std::atomic<uint32_t>>(initial_block_height + 1);
    all_tasks.spawn("block_storage_task", block_storage_task(
        chain,
        block_storage_input,
        stored_chunks,
        initial_block_height + 1,
        organizer,
        contiguous_height.get()
    ));

    // 7d. Incremental UTXO build task (reads from disk as contiguous blocks become available)
    all_tasks.spawn("utxo_build_task", utxo_build_task(
        chain,
        *contiguous_height,
        initial_block_height + 1,
        network_type,
        [&network]() { return network.stopped(); },
        on_fatal
    ));

    // Bridge: validated_chunks -> coordinator_events (carries validation errors only)
    all_tasks.spawn("coordinator_chunks_bridge", [&]() -> ::asio::awaitable<void> {
        spdlog::debug("[coordinator:chunks_bridge] Started");
        while (true) {
            auto [ec, result] = co_await validated_chunks.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[coordinator:chunks_bridge] Channel closed: {}", ec.message());
                break;
            }
            if (!coordinator_events.try_send(std::error_code{}, result)) {
                spdlog::warn("[coordinator:chunks_bridge] Channel full, chunk_validated dropped");
                break;
            }
        }
        spdlog::info("[coordinator:chunks_bridge] Task ended");
    });

    // Bridge: stored_chunks -> coordinator_events (carries storage results: validated + stored)
    // Bridge: the UTXO builder's published height -> coordinator_events (#663)
    //
    // The coordinator holds the post-checkpoint range until the set describes the
    // state below it, and the builder has nothing to announce when it advances —
    // it publishes a height and carries on. Without this the coordinator would sit
    // on its receive point until some UNRELATED event arrived, which is how the
    // range came to start on a peer connecting or a header batch landing. In the
    // observed run it did not arrive for over ten minutes.
    //
    // A poll and not a signal, deliberately: the builder is in another task with
    // its own window, and having it notify would mean either sending from under
    // that window or threading a channel through it. One LMDB key read per second
    // costs nothing next to what it unblocks, and it keeps the builder unaware of
    // the coordinator entirely.
    //
    // Sends only on CHANGE, so a steady state is silent. A read that fails sends
    // nothing: the coordinator asks the store itself when it evaluates, and
    // fail-closed there is the decision — this task's job is to make it look
    // again, not to decide anything.
    all_tasks.spawn("coordinator_utxo_bridge", [&]() -> ::asio::awaitable<void> {
        spdlog::debug("[coordinator:utxo_bridge] Started");
        auto exec = co_await ::asio::this_coro::executor;
        ::asio::steady_timer poll(exec);

        std::optional<uint32_t> last_seen;
        while ( ! network.stopped()) {
            poll.expires_after(std::chrono::seconds(1));
            auto [timer_ec] = co_await poll.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (timer_ec) {
                break;
            }

            auto const step = utxo_progress_for_tick(last_seen,
                chain.get_utxo_built_height());
            last_seen = step.next_last_seen;
            if ( ! step.announce) {
                continue;
            }

            if ( ! coordinator_events.try_send(std::error_code{},
                    utxo_build_advanced{*step.announce})) {
                // Dropped, and not fatal: the next tick reports the height again
                // because `last_seen` is only updated here, so a full channel
                // delays the wake-up rather than losing it.
                spdlog::debug("[coordinator:utxo_bridge] Channel full, utxo_build_advanced dropped");
                last_seen.reset();
            }
        }
        spdlog::info("[coordinator:utxo_bridge] Task ended");
    });

    all_tasks.spawn("coordinator_stored_bridge", [&]() -> ::asio::awaitable<void> {
        spdlog::debug("[coordinator:stored_bridge] Started");
        while (true) {
            auto [ec, result] = co_await stored_chunks.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[coordinator:stored_bridge] Channel closed: {}", ec.message());
                break;
            }
            if (!coordinator_events.try_send(std::error_code{}, result)) {
                spdlog::warn("[coordinator:stored_bridge] Channel full, chunk_validated dropped");
                break;
            }
        }
        spdlog::info("[coordinator:stored_bridge] Task ended");
    });

    // -------------------------------------------------------------------------
    // 8. Sync coordinator - orchestrates the sync flow
    // Uses unified event channel to avoid || operator between multiple channels
    // -------------------------------------------------------------------------

    // Bridge: stop_signal -> coordinator_events
    // 2026-02-07: Simplified to only poll network.stopped() via timer.
    // The || operator with stop_signal channel was suspected of causing the timer to
    // stop firing mysteriously. Since stop_signal is never used (nobody sends to it),
    // we remove it from the || expression and just poll network.stopped().
    all_tasks.spawn("coordinator_stop_bridge", [&]() -> ::asio::awaitable<void> {
        spdlog::info("[coordinator:stop_bridge] Started");
        auto exec = co_await ::asio::this_coro::executor;
        ::asio::steady_timer check_timer(exec);

        spdlog::debug("[coordinator:stop_bridge] Entering while loop, network.stopped()={}", network.stopped());
        uint64_t loop_count = 0;

        // 2026-02-07: Set to 1 to log every iteration for debugging, 100 for normal operation
        constexpr uint64_t log_every_n = 1;

        try {
            // Simple timer-based polling - no || operator with channels
            while (!network.stopped()) {
                ++loop_count;
                if (loop_count % log_every_n == 0) {
                    spdlog::debug("[coordinator:stop_bridge] iter={} Setting timer", loop_count);
                }

                check_timer.expires_after(std::chrono::milliseconds(100));

                if (loop_count % log_every_n == 0) {
                    spdlog::debug("[coordinator:stop_bridge] iter={} Awaiting timer", loop_count);
                }

                auto [ec] = co_await check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable));

                if (loop_count % log_every_n == 0) {
                    spdlog::debug("[coordinator:stop_bridge] iter={} Timer returned ec={}",
                        loop_count, ec ? ec.message() : "ok");
                }

                if (ec) {
                    spdlog::debug("[coordinator:stop_bridge] Timer cancelled: {}", ec.message());
                    break;
                }

                if (loop_count % log_every_n == 0) {
                    spdlog::debug("[coordinator:stop_bridge] iter={} Looping back", loop_count);
                }
            }
            // 2026-02-07: If we exit the while loop, log why
            spdlog::info("[coordinator:stop_bridge] Exited while loop - network.stopped()={}", network.stopped());
        } catch (std::exception const& e) {
            spdlog::error("[coordinator:stop_bridge] EXCEPTION: {}", e.what());
        } catch (...) {
            spdlog::error("[coordinator:stop_bridge] UNKNOWN EXCEPTION");
        }

        spdlog::info("[coordinator:stop_bridge] Exited while loop after {} iterations, network.stopped()={}",
            loop_count, network.stopped());

        // 2026-02-07: FIX - Always send stop_request when exiting
        if (!coordinator_events.try_send(std::error_code{}, stop_request{})) {
            spdlog::debug("[coordinator:stop_bridge] Channel full on exit, stop_request dropped (may be duplicate)");
        }

        check_timer.cancel();
        spdlog::info("[coordinator:stop_bridge] Task ended");
    });

    // Bridge: validated_headers -> coordinator_events
    all_tasks.spawn("coordinator_headers_bridge", [&]() -> ::asio::awaitable<void> {
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
    all_tasks.spawn("coordinator_blocks_bridge", [&]() -> ::asio::awaitable<void> {
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

    // 2026-02-07: Added comprehensive exception handling for debugging
    all_tasks.spawn("sync_coordinator", [&]() -> ::asio::awaitable<void> {
        spdlog::info("[sync_coordinator] Task started");
        try {

        uint32_t blocks_synced_to = initial_block_height;
        uint32_t headers_synced_to = initial_header_height;
        bool header_sync_complete = false;
        // Tracks whether the post-checkpoint (SLOW) block_range_request has
        // been sent. Without this guard the FAST → SLOW transition would
        // either never fire (if `blocks_synced_to < headers_synced_to` at
        // FAST SYNC COMPLETE — chain stalls forever at the checkpoint) or
        // fire on every subsequent chunk/block event.
        bool slow_sync_started = false;

        // The last answer reported, so a hold is logged when it changes rather
        // than on every event. `start` means "not holding".
        auto last_slow_sync_hold = slow_sync_admission::start;

        // For ETA calculation
        auto header_sync_start = std::chrono::steady_clock::now();
        uint32_t headers_at_start = headers_synced_to;

        // Buffer for out-of-order chunk validation results (keyed by start_height)
        boost::unordered_flat_map<uint32_t, chunk_validated> pending_chunks;

        spdlog::info("[sync_coordinator] Initial state: blocks={}, headers={}",
            blocks_synced_to, headers_synced_to);

        auto exec = co_await ::asio::this_coro::executor;

        // Trigger parallel header sync (supervisor manages chunk coordination)
        auto from_hash = active_hash_at(organizer.index(), headers_synced_to);

        spdlog::debug("[sync_coordinator] Starting parallel header sync from height {}", headers_synced_to);
        if (!header_download_input.try_send(std::error_code{}, header_request{
            .from_height = headers_synced_to,
            .from_hash = from_hash
        })) {
            spdlog::warn("[sync_coordinator] Channel full, initial header_request dropped");
        }

        // Reliable delivery of a block_range_request: every trigger site is
        // one-shot (guarded by header_sync_complete / slow_sync_started), so a
        // dropped try_send would leave the download coordinator uncreated and the
        // node stalled forever with peers idle. Retry until the channel accepts it.
        // Returns false only if we are shutting down. (async_send is intentionally
        // avoided here — it has its own issues on this channel type.)
        auto send_block_range = [&](uint32_t start, uint32_t end) -> ::asio::awaitable<bool> {
            block_range_request const req{.start_height = start, .end_height = end};
            size_t retries = 0;
            while ( ! block_download_input.try_send(std::error_code{}, req)) {
                // Log the first stall and then periodically, so a genuinely wedged
                // channel (no consumer draining) is visible rather than silent.
                if (retries == 0 || retries % 50 == 0) {
                    spdlog::warn("[sync_coordinator] block_download_input full — retrying block_range_request {}-{} (retry {})",
                        start, end, retries);
                }
                ++retries;
                ::asio::steady_timer timer(exec);
                timer.expires_after(std::chrono::milliseconds(20));
                auto [tec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
                if (tec || network.stopped()) {
                    spdlog::info("[sync_coordinator] block_range_request {}-{} abandoned (stopping) after {} retries",
                        start, end, retries);
                    co_return false;
                }
            }
            spdlog::info("[sync_coordinator] block_range_request {}-{} sent ({} retries)", start, end, retries);
            co_return true;
        };

        // Ask, log once per answer, and send only on `start`.
        //
        // One place, called from the three events that can change the answer, so
        // the three cannot drift apart — which is how the two existing copies of
        // this trigger came to be identical by hand.
        //
        // The height is read, not held. `get_utxo_built_height()` may well take a
        // read lease of its own internally — the property that matters is that it
        // returns a VALUE and nothing outlives the call, so no lease and no
        // capability is alive across the co_await inside send_block_range.
        auto try_start_slow_sync = [&]() -> ::asio::awaitable<void> {
            if (slow_sync_started || blocks_synced_to < checkpoint_height ||
                headers_synced_to <= blocks_synced_to) {
                co_return;
            }

            auto const start_height = blocks_synced_to + 1;
            auto const built = chain.get_utxo_built_height();
            auto const admission = may_start_slow_sync(start_height, built);

            if (admission != slow_sync_admission::start) {
                // Throttled: the builder is minutes behind at the start of a long
                // range, and one line per event would bury everything else.
                if (admission != last_slow_sync_hold) {
                    if (admission == slow_sync_admission::height_unavailable) {
                        spdlog::error("[sync_coordinator] SLOW block sync held at {}: the UTXO "
                            "built height could not be read ({}); refusing to validate against a "
                            "set that cannot say how far it describes",
                            start_height, database::result_code_name(built.error()));
                    } else {
                        spdlog::info("[sync_coordinator] SLOW block sync held at {}: the UTXO set "
                            "is at {} and block {} needs it at {}; waiting for the builder",
                            start_height, *built, start_height, start_height - 1);
                    }
                    last_slow_sync_hold = admission;
                }
                co_return;
            }

            if (last_slow_sync_hold != slow_sync_admission::start) {
                spdlog::info("[sync_coordinator] The UTXO set reached {}; SLOW block sync released",
                    *built);
                last_slow_sync_hold = slow_sync_admission::start;
            }

            spdlog::info("[sync_coordinator] Starting SLOW block sync: {} to {} ({} blocks)",
                start_height, headers_synced_to, headers_synced_to - blocks_synced_to);
            slow_sync_started = co_await send_block_range(start_height, headers_synced_to);
        };

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

                // A heavier branch was stored: switch the chain onto it. Handled
                // before the error branch below, since a non-advancing batch also
                // reports stale_chain.
                if (result->reorg_candidate && result->reorg_fork_height >= 0) {
                    auto const fork_height = uint32_t(result->reorg_fork_height);
                    spdlog::warn("[sync_coordinator] Reorg requested: fork at height {}", fork_height);

                    auto const reorg = co_await execute_reorg(
                        chain, organizer, result->reorg_branch_head, fork_height,
                        [&network] { return network.stopped(); },
                        [&chain](domain::chain::header::list const& headers, size_t start_height) {
                            return chain.replace_headers_from(headers, start_height);
                        });

                    if (reorg.fatal) {
                        // The chain moved but its persisted description did not.
                        // Syncing on would build on a view a restart will not come
                        // back to, so stop here: no counters, no new ranges, and
                        // wind the node down. Stopping the network is what every
                        // other task is watching, and the stop bridge turns it
                        // into the stop_request this loop exits on.
                        // Report and stop taking work. Winding the process down
                        // is the node owner's — it holds the lifecycle, and half
                        // of a shutdown started from here would be a second,
                        // partial copy of it.
                        on_fatal("a reorganization left the persisted chain and the active chain "
                            "describing different branches");
                        break;
                    }

                    auto const outcome = reorg.result;
                    auto const switched = outcome.ok;

                    // Resync from the tip the switch actually reports, so a partially
                    // disconnected range is re-downloaded instead of stranded. When it
                    // reports nothing the tip could not be read and nothing was
                    // touched — leave the counters alone rather than treat that as
                    // height 0, which would rewind the whole sync to genesis.
                    if (outcome.validated_tip) {
                        blocks_synced_to = *outcome.validated_tip;
                        // The shared contiguous counter still holds the pre-switch
                        // value; leaving it would make the UTXO build request
                        // headers-only heights on every poll.
                        contiguous_height->store(blocks_synced_to + 1, std::memory_order_release);
                    }

                    auto const new_tip = switched ? chain.headers().active_tip_height() : -1;
                    if (switched && new_tip > int32_t(blocks_synced_to)) {
                        // The new branch is headers-only from the fork up, so block
                        // download refills from the rewound tip.
                        headers_synced_to = uint32_t(new_tip);
                        spdlog::warn("[sync_coordinator] Reorg complete: blocks rewound to {}, "
                            "headers now at {}", blocks_synced_to, headers_synced_to);

                        if ( ! co_await send_block_range(blocks_synced_to + 1, headers_synced_to)) {
                            spdlog::error("[sync_coordinator] Reorg: failed to re-drive block download");
                        }
                    } else if (switched) {
                        // Switched, but the new tip is not above the fork — nothing
                        // to download. Leaves the chain shorter than it was, so make
                        // it loud rather than silently idling.
                        spdlog::error("[sync_coordinator] Reorg switched to a tip at {} which is not "
                            "above the fork at {}", new_tip, fork_height);
                    } else {
                        spdlog::error("[sync_coordinator] Reorg aborted; staying on the current chain");
                    }
                    continue;
                }

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
                    auto retry_hash = active_hash_at(organizer.index(), headers_synced_to);

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
                    auto next_hash = active_hash_at(organizer.index(), headers_synced_to);

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
                        auto retry_hash = active_hash_at(organizer.index(), headers_synced_to);

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
                        if (blocks_synced_to < checkpoint_height) {
                            // Fast sync stage: download up to the checkpoint. Merkle
                            // only, so the UTXO set is not consulted and there is
                            // nothing to wait for.
                            //
                            // NOTE: We should only reach here if headers_synced_to >= checkpoint_height
                            // because header_sync should not complete until we have checkpoint headers
                            spdlog::info("[sync_coordinator] Starting FAST block sync: {} to {} ({} blocks)",
                                blocks_synced_to + 1, checkpoint_height,
                                checkpoint_height - blocks_synced_to);
                            co_await send_block_range(blocks_synced_to + 1, checkpoint_height);
                        } else {
                            // Slow sync stage. THIS is the third door onto the
                            // post-checkpoint range, and it used to send directly:
                            // its own comment said "requires UTXO" and nothing
                            // checked that the set had it. It also left
                            // `slow_sync_started` false, so the block and chunk
                            // paths could send the same range again.
                            //
                            // Same guarded admission as the other two (#663).
                            co_await try_start_slow_sync();
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

                        auto next_hash = active_hash_at(organizer.index(), headers_synced_to);

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
                        // Nothing to build here. `utxo_build_task` applies each
                        // block's delta as it goes, following the contiguous
                        // height `block_storage_task` publishes, and there is no
                        // range-builder call left for the checkpoint to make.
                        // It runs on its own schedule, so reaching the checkpoint
                        // does not mean it has arrived — this only says the
                        // coordinator has nothing to do about it.
                        spdlog::info("[sync_coordinator] UTXO set built incrementally by utxo_build_task");
                    }

                    // Trigger SLOW block sync for the post-checkpoint range.
                    // FAST SYNC COMPLETE only stops the in-flight chunk_coordinator
                    // (whose end_height was the checkpoint); without this trigger
                    // the orchestrator stalls forever at the checkpoint when
                    // headers are already ahead of the bloom height. The
                    // `slow_sync_started` guard keeps subsequent block_validated
                    // events from re-sending the request.
                    //
                    // Held until the UTXO set describes the state below the first
                    // block of the range — see may_start_slow_sync (#663).
                    co_await try_start_slow_sync();

                    // Check if we've caught up to headers
                    if (blocks_synced_to >= headers_synced_to && header_sync_complete) {
                        // Whether before or after checkpoint, we need to keep looking for more headers
                        // to stay in sync with the network tip
                        spdlog::info("[sync_coordinator] Block sync caught up to headers at {} (checkpoint={}), restarting header sync",
                            blocks_synced_to, checkpoint_height);

                        header_sync_complete = false;
                        header_sync_start = std::chrono::steady_clock::now();
                        headers_at_start = headers_synced_to;
                        slow_sync_started = false;  // allow next FAST→SLOW transition after a full re-sync

                        auto next_hash = active_hash_at(organizer.index(), headers_synced_to);

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

            if (auto* advanced = std::get_if<utxo_build_advanced>(&event)) {
                // A WAKE-UP, not an authority on the height. The value it carries
                // is logged and nothing else: the message can sit in the channel
                // while a reorg, a startup reconciliation or a rebuild moves the
                // height under it, and a decision made on the stale number would
                // send the range against a set that no longer describes what the
                // message claimed.
                //
                // `try_start_slow_sync` takes no arguments precisely so the
                // payload cannot reach the decision; it reads the height itself.
                spdlog::debug("[sync_coordinator] UTXO built height reported as {}; re-checking",
                    advanced->built_height);
                co_await try_start_slow_sync();
                continue;
            }

            if (auto* result = std::get_if<chunk_validated>(&event)) {
                if (!result->result) {
                    // Buffer the chunk result (chunks may arrive out of order)
                    pending_chunks[result->start_height] = *result;

                    // Advance blocks_synced_to through consecutive completed chunks
                    while (true) {
                        auto it = pending_chunks.find(blocks_synced_to + 1);
                        if (it == pending_chunks.end()) break;
                        blocks_synced_to = it->second.start_height + it->second.block_count - 1;
                        pending_chunks.erase(it);
                    }

                    // Log progress periodically (every 1000 blocks)
                    static uint32_t last_logged_k = 0;
                    uint32_t current_k = blocks_synced_to / 1000;
                    if (current_k > last_logged_k) {
                        last_logged_k = current_k;
                        spdlog::info("[sync_coordinator:fast] {}/{} validated (pending_chunks: {})",
                            blocks_synced_to, checkpoint_height, pending_chunks.size());
                    }

                    // Check if fast sync stage is complete
                    if (blocks_synced_to >= checkpoint_height) {
                        spdlog::info("[sync_coordinator] *** FAST SYNC COMPLETE at checkpoint {} ***",
                            checkpoint_height);
                        // Nothing to build here; `utxo_build_task` applies each
                        // block's delta as it goes. Reaching the checkpoint does
                        // not mean it has arrived — which is why the range below
                        // asks rather than assumes.
                        spdlog::info("[sync_coordinator] UTXO set built incrementally by utxo_build_task");
                    }

                    // Trigger SLOW block sync for the post-checkpoint range.
                    // See the matching call in the block_validated branch above —
                    // same decision on the chunk path, which is the one that
                    // fires during the FAST IBD itself.
                    co_await try_start_slow_sync();

                    // Check if we've caught up to headers
                    if (blocks_synced_to >= headers_synced_to && header_sync_complete) {
                        spdlog::info("[sync_coordinator] Block sync caught up to headers at {} (checkpoint={}), restarting header sync",
                            blocks_synced_to, checkpoint_height);

                        header_sync_complete = false;
                        header_sync_start = std::chrono::steady_clock::now();
                        headers_at_start = headers_synced_to;
                        slow_sync_started = false;

                        auto next_hash = active_hash_at(organizer.index(), headers_synced_to);

                        if (!header_download_input.try_send(std::error_code{}, header_request{
                            .from_height = headers_synced_to,
                            .from_hash = next_hash
                        })) {
                            spdlog::warn("[sync_coordinator] Channel full, restart header_request dropped");
                        }
                    }
                } else {
                    spdlog::error("[sync_coordinator] Chunk validation failed at height {}: {}",
                        result->start_height, result->result.message());

                    if (result->source_peer) {
                        if (!peer_provider_input.try_send(std::error_code{}, peer_error{
                            .peer = result->source_peer,
                            .error = result->result
                        })) {
                            spdlog::warn("[sync_coordinator] Channel full, peer_error dropped");
                        }
                    }
                    break;
                }
                continue;
            }
        }

        spdlog::info("[sync_coordinator] Event loop ended, task completing...");
        } catch (std::exception const& e) {
            spdlog::error("[sync_coordinator] EXCEPTION: {}", e.what());
        } catch (...) {
            spdlog::error("[sync_coordinator] UNKNOWN EXCEPTION");
        }
        spdlog::info("[sync_coordinator] Task ended");
    });

    // Wait for all tasks
    spdlog::info("[sync_orchestrator] All {} tasks spawned, running...", all_tasks.active_count());
    co_await all_tasks.join();

    spdlog::info("[sync_orchestrator:shutdown] All tasks completed - orchestrator exiting");
}

} // namespace kth::node::sync
