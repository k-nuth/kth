// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/body_range.hpp>
#include <kth/node/sync/orchestrator.hpp>

#include <kth/node/detail/header_request_delivery.hpp>

#include <algorithm>

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

range_quiet why_no_range(
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    uint32_t checkpoint_height,
    std::optional<uint32_t> range_end) {

    // Nothing owed: the bodies are level with the headers, or past them. This is
    // also what makes the `+ 1` in next_slow_sync_range safe without a guard of
    // its own — past this condition `blocks_synced_to < headers_synced_to <=
    // UINT32_MAX`, so `blocks_synced_to` cannot be UINT32_MAX and the increment
    // cannot wrap.
    if (headers_synced_to <= blocks_synced_to) {
        return range_quiet::no_advance;
    }

    // Below the checkpoint the FAST range owns the download, and its end is the
    // checkpoint rather than the header tip. Opening a post-checkpoint range over
    // the same heights would put two coordinators on the same blocks.
    if (blocks_synced_to < checkpoint_height) {
        return range_quiet::below_checkpoint;
    }

    // A range still in flight is left to finish. Its workers hold chunks; a new
    // coordinator stops the old one and those chunks are dropped, to be claimed
    // again from a range that starts below them. The remainder is opened when
    // this range drains, by the event that drains it.
    if (range_end && blocks_synced_to < *range_end) {
        return range_quiet::in_flight;
    }

    return range_quiet::none;
}

std::optional<slow_sync_range> next_slow_sync_range(
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    uint32_t checkpoint_height,
    std::optional<uint32_t> range_end) {

    // Asked, not repeated. The reason and the range are two readings of one rule,
    // and the log line below reads it the same way.
    if (why_no_range(blocks_synced_to, headers_synced_to, checkpoint_height, range_end)
            != range_quiet::none) {
        return std::nullopt;
    }
    return slow_sync_range{blocks_synced_to + 1, headers_synced_to};
}

std::optional<uint32_t> range_end_after_rewind(
    std::optional<uint32_t> range_end, uint32_t rewound_to) {

    // Kept when it still describes blocks the bodies actually hold: a range that
    // ended at or below the rewound tip was completed over blocks the switch did
    // not take away, and dropping it would reopen heights already downloaded.
    if (range_end && *range_end > rewound_to) {
        return std::nullopt;
    }
    return range_end;
}

char const* to_string(body_range_trigger trigger) {
    switch (trigger) {
        case body_range_trigger::headers_advanced:     return "headers_advanced";
        case body_range_trigger::header_sync_complete: return "header_sync_complete";
        case body_range_trigger::block_validated:      return "block_validated";
        case body_range_trigger::chunk_validated:      return "chunk_validated";
        case body_range_trigger::utxo_build_advanced:  return "utxo_build_advanced";
        case body_range_trigger::reorg:                return "reorg";
    }
    return "unknown";
}

namespace {

char const* quiet_name(range_quiet quiet) {
    switch (quiet) {
        case range_quiet::no_advance:       return "the headers have not moved past the bodies";
        case range_quiet::below_checkpoint: return "the FAST range still owns these heights";
        case range_quiet::in_flight:        return "the previous range is still in flight";
        case range_quiet::none:             return "none";
    }
    return "unknown";
}

std::string end_name(std::optional<uint32_t> end) {
    return end ? std::to_string(*end) : std::string("none");
}

// One place, so the two paths that open a range cannot drift into two formats —
// which is the same reason the trigger is an enum rather than a string.
//
// `utxo` is nullopt on the reorg path, and says so: that path does not consult
// the UTXO admission, because the admission is about a set that describes the
// chain BELOW the range and a switch has just moved what that means.
void log_range_opening(slow_sync_range range, body_range_trigger trigger,
    uint32_t headers_synced_to, uint32_t blocks_synced_to,
    std::optional<uint32_t> previous_end, std::optional<uint32_t> utxo) {

    spdlog::info("[sync_coordinator] Opening body range [{}..{}]: trigger={} headers={} "
        "bodies={} utxo={} previous_end={} ({} blocks)",
        range.start, range.end, to_string(trigger), headers_synced_to, blocks_synced_to,
        utxo ? std::to_string(*utxo) : std::string("not consulted"), end_name(previous_end),
        range.end - range.start + 1);
}

} // namespace

// Reliable delivery of a block_range_request: a dropped try_send would leave the
// download coordinator uncreated and the node stalled with peers idle, so this
// retries until the channel accepts it. Returns false only when we are shutting
// down. (async_send is intentionally avoided here — it has its own issues on this
// channel type.)
::asio::awaitable<bool> send_block_range(body_range_deps deps, uint32_t start, uint32_t end) {
    auto exec = co_await ::asio::this_coro::executor;
    block_range_request const req{.start_height = start, .end_height = end};

    size_t retries = 0;
    while ( ! deps.block_download_input.try_send(std::error_code{}, req)) {
        // Log the first stall and then periodically, so a genuinely wedged
        // channel (no consumer draining) is visible rather than silent.
        if (retries == 0 || retries % 50 == 0) {
            spdlog::warn("[sync_coordinator] block_download_input full — retrying "
                "block_range_request {}-{} (retry {})", start, end, retries);
        }
        ++retries;
        ::asio::steady_timer timer(exec);
        timer.expires_after(std::chrono::milliseconds(20));
        auto [tec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
        if (tec || deps.network.stopped()) {
            spdlog::info("[sync_coordinator] block_range_request {}-{} abandoned (stopping) "
                "after {} retries", start, end, retries);
            co_return false;
        }
    }
    spdlog::info("[sync_coordinator] block_range_request {}-{} sent ({} retries)",
        start, end, retries);
    co_return true;
}

::asio::awaitable<std::optional<uint32_t>> open_body_range_if_owed(
    body_range_deps deps,
    body_range_log_memory& memory,
    body_range_trigger trigger,
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    std::optional<uint32_t> range_end) {

    auto const range = next_slow_sync_range(
        blocks_synced_to, headers_synced_to, deps.checkpoint_height, range_end);

    if ( ! range) {
        auto const reason = why_no_range(
            blocks_synced_to, headers_synced_to, deps.checkpoint_height, range_end);
        if (reason != memory.last_quiet) {
            spdlog::debug("[sync_coordinator] No body range on {}: {} "
                "(headers={} bodies={} previous_end={})",
                to_string(trigger), quiet_name(reason), headers_synced_to, blocks_synced_to,
                end_name(range_end));
            memory.last_quiet = reason;
        }
        co_return std::nullopt;
    }
    memory.last_quiet = range_quiet::none;

    // The height is READ, not held. get_utxo_built_height() may take a read lease
    // of its own internally; what matters is that it returns a VALUE, so nothing
    // is alive across the co_await in send_block_range below.
    auto const built = deps.chain.get_utxo_built_height();
    auto const admission = may_start_slow_sync(range->start, built);

    if (admission != slow_sync_admission::start) {
        // Throttled: the builder is minutes behind at the start of a long range,
        // and one line per event would bury everything else.
        if (admission != memory.last_hold) {
            if (admission == slow_sync_admission::height_unavailable) {
                spdlog::error("[sync_coordinator] SLOW block sync held at {}: the UTXO built "
                    "height could not be read ({}); refusing to validate against a set that "
                    "cannot say how far it describes",
                    range->start, database::result_code_name(built.error()));
            } else {
                spdlog::info("[sync_coordinator] SLOW block sync held at {}: the UTXO set is at "
                    "{} and block {} needs it at {}; waiting for the builder",
                    range->start, *built, range->start, range->start - 1);
            }
            memory.last_hold = admission;
        }

        // A gap nothing is working on. There IS a range to open, which means the
        // previous one drained, which means no coordinator covers these heights;
        // the only thing that will move this is the builder. Named ONCE per
        // standstill — the same pair of heights re-read by the next event is the
        // same standstill, not a new one.
        auto const stall = std::pair{headers_synced_to, blocks_synced_to};
        if (memory.last_stall != stall) {
            spdlog::warn("[sync_coordinator] Bodies {}-{} are owed and nothing is downloading "
                "them: headers={} bodies={} previous_end={} trigger={} (held by the UTXO "
                "admission reported above)",
                range->start, range->end, headers_synced_to, blocks_synced_to,
                end_name(range_end), to_string(trigger));
            memory.last_stall = stall;
        }
        co_return std::nullopt;
    }
    memory.last_stall.reset();

    if (memory.last_hold != slow_sync_admission::start) {
        spdlog::info("[sync_coordinator] The UTXO set reached {}; SLOW block sync released",
            *built);
        memory.last_hold = slow_sync_admission::start;
    }

    // The one line that reconstructs the decision: which door was taken, what the
    // three heights were, and what the previous range covered.
    log_range_opening(*range, trigger, headers_synced_to, blocks_synced_to, range_end, *built);

    if ( ! co_await send_block_range(deps, range->start, range->end)) {
        // Shutting down, or a channel that would not take it. Nothing is recorded
        // either way, so the next event asks again rather than holding behind a
        // range that was never sent.
        co_return std::nullopt;
    }
    co_return range->end;
}

::asio::awaitable<headers_progress> on_headers_advanced(
    body_range_deps deps,
    body_range_log_memory& memory,
    headers_validated const& result,
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    std::optional<uint32_t> range_end) {

    // Nothing was added, so nothing moved. An error with no accepted headers is
    // a peer to retry with, not a tip that advanced.
    if (result.count == 0) {
        co_return headers_progress{headers_synced_to, std::nullopt};
    }

    // Deliberately NOT gated on `result.result`. A batch that added a valid
    // prefix and then hit a bad header moved the tip by that prefix, exactly as a
    // clean batch would, and the bodies are owed the same thing either way. What
    // the error decides is which peer to ask next, and that is the caller's.
    auto const moved_to = result.height;

    co_return headers_progress{
        moved_to,
        co_await open_body_range_if_owed(
            deps, memory, body_range_trigger::headers_advanced,
            blocks_synced_to, moved_to, range_end)};
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

        // Through the same single entry point the build task uses, so the bulk
        // catch-up and the per-batch barrier cannot both write the same range.
        if ( ! ensure_headers_persisted(chain, index, chunk_end)) {
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
    // Block announcement bridge
    // -------------------------------------------------------------------------
    //
    // The node rings a doorbell when a block has been announced; the hashes stay
    // registered on the node until the coordinator takes them. This forwards the
    // ring, and the forward is AWAITED rather than try_send: the doorbell can
    // afford to be coalesced because what it wakes drains everything, but a ring
    // that never reaches the coordinator wakes nobody at all.
    all_tasks.spawn("announcement_bridge", [&]() -> ::asio::awaitable<void> {
        spdlog::info("[announcement_bridge] Started");
        while (true) {
            auto [ec, ring] = co_await network.block_announcements().async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                spdlog::debug("[announcement_bridge] Channel closed: {}", ec.message());
                break;
            }

            auto [send_ec] = co_await coordinator_events.async_send(
                std::error_code{}, blocks_announced_event{},
                ::asio::as_tuple(::asio::use_awaitable));
            if (send_ec) {
                spdlog::debug("[announcement_bridge] Coordinator channel closed");
                break;
            }
        }
        spdlog::info("[announcement_bridge] Task ended");
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
        // The END of the post-checkpoint range last sent, or nothing if none has
        // been. NOT a "was it started" flag: a flag cannot say which range, and
        // the range's end is the header tip as it stood when it was opened. A
        // tip that moves afterwards left the flag true, the coordinator complete
        // and the remaining bodies never requested — see next_slow_sync_range.
        std::optional<uint32_t> slow_sync_end;

        // What the range decision needs, and what its logs remember. Both live
        // outside the loop so the decision itself is a function a control can
        // drive with real channels — see open_body_range_if_owed.
        body_range_deps deps{chain, network, block_download_input, checkpoint_height};
        body_range_log_memory range_log;


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

        // Whether a header request is outstanding, and whether a block was
        // announced that no outstanding request can be expected to bring in.
        //
        // A block announced WHILE a request is in flight cannot simply be
        // dropped: it may have been mined after that getheaders left, so its
        // response cannot contain it, and with nothing polling anywhere that
        // announcement was the only event able to reopen the walk. It is
        // coalesced instead, and reconsidered when the walk settles — by which
        // time the response may have brought it in, and then there is nothing
        // to do (#706).
        bool header_request_in_flight = false;

        // Every announced hash still unknown when a walk was already running,
        // not just the first of them. Peers set the rate here, so it is bounded
        // and the overflow is remembered: with only one hash kept, a burst of
        // different blocks would leave the last one asked about and the rest
        // forgotten, and a walk that brought in the one we remembered would
        // clear the debt for all of them.
        static constexpr size_t max_announcement_pending = 64;
        std::vector<hash_digest> announcement_pending;
        bool announcement_pending_overflow = false;

        auto remember_announcement = [&](hash_digest const& hash) {
            if (std::find(announcement_pending.begin(), announcement_pending.end(), hash)
                != announcement_pending.end()) {
                return;
            }
            if (announcement_pending.size() >= max_announcement_pending) {
                announcement_pending_overflow = true;
                return;
            }
            announcement_pending.push_back(hash);
        };

        // Awaited, not try_send. A request dropped for want of room is a walk
        // that never happens, and for an announcement it is worse: its hashes
        // have already been taken off the node, so nothing is left to raise it
        // again. In flight is recorded only once the request has been accepted,
        // so a delivery that never happened cannot look like one that did.
        auto ask_for_headers = [&](uint32_t from_height, hash_digest const& hash,
                                   std::optional<uint64_t> spent, char const* why)
            -> ::asio::awaitable<void> {
            auto const delivered = co_await detail::deliver_header_request(
                header_download_input,
                header_request{
                    .from_height = from_height,
                    .from_hash = hash,
                    .spent_peer = spent});

            if ( ! delivered) {
                // The channel is closed, which happens on shutdown and nowhere
                // else. There is nobody left to ask.
                spdlog::debug("[sync_coordinator] {} header_request abandoned; shutting down", why);
                co_return;
            }
            header_request_in_flight = true;
        };

        spdlog::debug("[sync_coordinator] Starting parallel header sync from height {}", headers_synced_to);
        co_await ask_for_headers(headers_synced_to, from_hash, std::nullopt, "initial");


        // One place, called from every event that can change the answer, so they
        // cannot drift apart — which is how two copies of this trigger once came
        // to be identical by hand.
        // The ONE place an opened range is recorded, so the rule has a single
        // statement rather than one per call site.
        auto record = [&](std::optional<uint32_t> opened) {
            if (opened) {
                slow_sync_end = opened;
            }
        };

        auto try_start_slow_sync = [&](body_range_trigger trigger) -> ::asio::awaitable<void> {
            record(co_await open_body_range_if_owed(
                deps, range_log, trigger, blocks_synced_to, headers_synced_to, slow_sync_end));
        };

        // Whether the chain already holds a hash. An announcement of something we
        // have is nothing to act on, whether it arrived before the walk or was
        // brought in by it.
        auto already_held = [&](hash_digest const& hash) {
            return organizer.index().find(hash) != blockchain::header_index::null_index;
        };

        // Consulted on the way back to the channel, so every path out of every
        // handler passes through it once and none has to remember to.
        auto settle_pending_announcement = [&]() -> ::asio::awaitable<void> {
            if (header_request_in_flight) {
                co_return;
            }
            if (announcement_pending.empty() && ! announcement_pending_overflow) {
                co_return;
            }

            // Overflowed means some announcement was not kept, so "everything I
            // remember is known" is not "nothing new was announced": ask.
            bool still_unknown = announcement_pending_overflow;
            for (auto const& hash : announcement_pending) {
                if ( ! already_held(hash)) {
                    still_unknown = true;
                    break;
                }
            }

            announcement_pending.clear();
            announcement_pending_overflow = false;

            if ( ! still_unknown) {
                // The walk that was in flight brought them all in. Nothing owed.
                spdlog::debug("[sync_coordinator] Announced block already in the index; "
                    "no follow-up needed");
                co_return;
            }

            spdlog::debug("[sync_coordinator] Announced block still unknown after the walk "
                "settled; asking once from height {}", headers_synced_to);
            co_await ask_for_headers(headers_synced_to,
                active_hash_at(organizer.index(), headers_synced_to), std::nullopt,
                "announcement follow-up");
        };

        // Main loop: ONLY receives from unified channel (no || operator)
        while (true) {
            co_await settle_pending_announcement();

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
                // Nothing is owed to anybody once we are stopping: the walk is
                // abandoned and a coalesced announcement has nowhere to go.
                announcement_pending.clear();
                announcement_pending_overflow = false;
                header_request_in_flight = false;
                break;
            }

            if (std::holds_alternative<blocks_announced_event>(event)) {
                // The doorbell. The hashes were registered on the node before it
                // rang, so this takes ALL of them — a ring dropped because one
                // was already queued announced nothing this drain will miss.
                auto const announced = network.take_announced_blocks();

                std::vector<hash_digest> unknown_hashes;
                for (auto const& hash : announced.hashes) {
                    if ( ! already_held(hash)) {
                        unknown_hashes.push_back(hash);
                    }
                }

                std::optional<hash_digest> unknown;
                if ( ! unknown_hashes.empty()) {
                    unknown = unknown_hashes.front();
                }

                if ( ! unknown && announced.overflowed) {
                    // The registry refused hashes for want of room, so "all of
                    // these are known" is not the same as "nothing new was
                    // announced". Asking is the one action any announcement
                    // produces, so ask; the walk starts from the tip and takes
                    // whatever is above it, refused hashes included.
                    spdlog::debug("[sync_coordinator] Announcement registry overflowed; "
                        "asking rather than assuming");
                    unknown = active_hash_at(organizer.index(), headers_synced_to);
                }

                if ( ! unknown) {
                    spdlog::debug("[sync_coordinator] {} announced block(s), all already held",
                        announced.hashes.size());
                    continue;
                }

                if (header_request_in_flight) {
                    // Coalesced, not dropped: this block may have been mined
                    // after the outstanding getheaders left, so its response
                    // cannot be relied on to contain it. Reconsidered when the
                    // walk settles, and only asked for if it is still missing.
                    spdlog::debug("[sync_coordinator] {} block(s) announced while a header "
                        "request is in flight; coalescing until it settles",
                        unknown_hashes.empty() ? 1u : unsigned(unknown_hashes.size()));

                    if (announced.overflowed) {
                        // The registry refused hashes, so what was remembered is
                        // not the whole of what was announced — and that is true
                        // whether or not some hashes were remembered too. Losing
                        // the flag here would let a walk that brought in the
                        // remembered ones settle the debt for the refused ones
                        // as well.
                        announcement_pending_overflow = true;
                    }
                    for (auto const& hash : unknown_hashes) {
                        remember_announcement(hash);
                    }
                    continue;
                }

                spdlog::debug("[sync_coordinator] Block announced and unknown; asking from height {}",
                    headers_synced_to);
                co_await ask_for_headers(headers_synced_to,
                    active_hash_at(organizer.index(), headers_synced_to), std::nullopt,
                    "announcement");
                continue;
            }

            if (auto* result = std::get_if<headers_validated>(&event)) {
                // The outcome of the request that was outstanding. Whatever the
                // branches below decide, the request itself is over.
                header_request_in_flight = false;

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

                        // HERE, before any branching: the branches below that do
                        // not re-drive the download are exactly the ones that
                        // would otherwise carry a range end describing blocks the
                        // switch just took away.
                        slow_sync_end = range_end_after_rewind(slow_sync_end, blocks_synced_to);
                    }

                    auto const new_tip = switched ? chain.headers().active_tip_height() : -1;
                    if (switched && new_tip > int32_t(blocks_synced_to)) {
                        // The new branch is headers-only from the fork up, so block
                        // download refills from the rewound tip.
                        headers_synced_to = uint32_t(new_tip);
                        spdlog::warn("[sync_coordinator] Reorg complete: blocks rewound to {}, "
                            "headers now at {}", blocks_synced_to, headers_synced_to);

                        // Recorded like any other range. Left unrecorded, a
                        // stale end from before the switch would read as a range
                        // still in flight over heights the reorg just rewound,
                        // and the remainder above it would never be opened.
                        log_range_opening(
                            slow_sync_range{blocks_synced_to + 1, headers_synced_to},
                            body_range_trigger::reorg, headers_synced_to, blocks_synced_to,
                            slow_sync_end, std::nullopt);

                        if (co_await send_block_range(deps, blocks_synced_to + 1, headers_synced_to)) {
                            // The only place a new end is recorded, and only once
                            // the request was actually sent. A send that failed
                            // leaves nothing recorded, and the rewind above has
                            // already dropped what described the old branch, so
                            // the next event re-derives the range from the
                            // heights instead of holding behind it.
                            slow_sync_end = headers_synced_to;
                        } else {
                            spdlog::error("[sync_coordinator] Reorg: failed to re-drive block "
                                "download; the next event asks again");
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

                // ONE progress point, ahead of the success/error split, because the
                // progress is the same fact either way: `count` is what the
                // organizer accepted and `result` is why it stopped, and a batch
                // that added a valid prefix before hitting a bad header moved the
                // tip by that prefix. Splitting first is how the error path came
                // to advance the tip and tell nobody.
                auto const prev_headers_synced = headers_synced_to;
                {
                    auto const progress = co_await on_headers_advanced(
                        deps, range_log, *result,
                        blocks_synced_to, headers_synced_to, slow_sync_end);
                    headers_synced_to = progress.headers_synced_to;
                    record(progress.opened);
                }

                // Persisted as the header tip moves, not only when a batch of
                // bodies reaches it. The per-batch barrier keeps the UTXO set
                // from outrunning the durable table, but it is driven by bodies:
                // headers can arrive for a long stretch with no batch behind
                // them -- which is the whole distance between the connected tip
                // and the header tip -- and nothing would write them.
                //
                // Gap-only and serialized, so this is the same single writer: a
                // range a batch has already made durable costs a cursor read,
                // and the two never overlap.
                //
                // A failure is logged and not fatal. Nothing durable depends on
                // these headers yet -- no batch has published a height that
                // needs them -- so the cost is re-fetching them after a restart,
                // and the batch that eventually reaches them has its own barrier
                // that does fail.
                if (headers_synced_to > prev_headers_synced) {
                    if ( ! ensure_headers_persisted(chain, organizer.index(),
                                                    headers_synced_to)) {
                        spdlog::warn("[header_persist] Could not persist headers through {}; "
                            "they will be re-fetched after a restart", headers_synced_to);
                    }
                }

                if (result->result.value() == error::stale_chain) {
                    // Not a failure, and this is the distinction the node did not
                    // make. `stale_chain` is the organizer saying the batch was
                    // valid and added nothing — which is what a peer sends when
                    // it is at the same tip we are, and answers our locator with
                    // headers we already hold. The wire cannot tell that apart
                    // from progress; only this verdict can.
                    //
                    // Treated as a peer failure it produced an identical request
                    // to the same peer, eleven times in 1.43 s, and never let the
                    // tip be confirmed (#705). The peer is spent for this walk
                    // instead, and the download task asks the next eligible one.
                    std::optional<uint64_t> spent;
                    if (result->source_peer) {
                        spent = result->source_peer->nonce();
                    }

                    spdlog::debug("[sync_coordinator] No new headers at height {} from peer {}; "
                        "asking the next eligible peer",
                        headers_synced_to,
                        result->source_peer ? result->source_peer->authority_with_agent() : "unknown");

                    co_await ask_for_headers(headers_synced_to,
                        active_hash_at(organizer.index(), headers_synced_to), spent, "no-progress");
                } else if (result->result) {
                    // Header validation failed - normal network behavior (peer on wrong chain)
                    spdlog::debug("[sync_coordinator] Header validation failed: {} from peer {}",
                        result->result.message(),
                        result->source_peer ? result->source_peer->authority_with_agent() : "unknown");

                    if (result->count > 0) {
                        spdlog::debug("[sync_coordinator] Headers synced to {} before failure",
                            headers_synced_to);
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

                    co_await ask_for_headers(headers_synced_to, retry_hash, std::nullopt, "retry");
                } else if (result->count > 0) {
                    // The tip has already moved above; what is left here is this
                    // branch's own policy — the progress log and asking the same
                    // peer for the next batch.

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

                    // Sequential header sync: ask for the next batch. Exactly one
                    // header_request leaves this handler — the error branch above
                    // sends its own, to a different peer, and the two are
                    // mutually exclusive.
                    auto const next_hash = active_hash_at(organizer.index(), headers_synced_to);
                    co_await ask_for_headers(headers_synced_to, next_hash, std::nullopt, "next batch");
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

                        co_await ask_for_headers(headers_synced_to, retry_hash, std::nullopt, "retry");
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

                    // Owned by the task group, not detached. It holds `chain` and
                    // the organizer's index BY REFERENCE and it suspends between
                    // chunks, so a detached one outlives the join below: the final
                    // drain would overlap a writer it believed had stopped, and
                    // the orchestrator could return and let the organizer be
                    // destroyed while this still held a reference into it.
                    //
                    // In the group, join() waits for it, which is what makes the
                    // drain after that join a quiescence barrier rather than a
                    // hope.
                    if (headers_synced_to > initial_header_height) {
                        all_tasks.spawn("header_persist",
                            persist_headers_to_db(chain, organizer.index(),
                                initial_header_height + 1, headers_synced_to));
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
                            spdlog::info("[sync_coordinator] Starting FAST block sync: {} to "
                                "{} ({} blocks)", blocks_synced_to + 1, checkpoint_height,
                                checkpoint_height - blocks_synced_to);

                            // The result is not consulted, and that is correct
                            // again now that the send is unbounded: it answers
                            // false only when the node is winding down, and a
                            // node that is winding down owes no range.
                            co_await send_block_range(
                                deps, blocks_synced_to + 1, checkpoint_height);
                        } else {
                            // Slow sync stage. THIS is the third door onto the
                            // post-checkpoint range, and it used to send directly:
                            // its own comment said "requires UTXO" and nothing
                            // checked that the set had it. It also left
                            // no range recorded, so the block and chunk paths
                            // could send the same range again.
                            //
                            // Same guarded admission as the other two (#663).
                            co_await try_start_slow_sync(
                                body_range_trigger::header_sync_complete);
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

                        co_await ask_for_headers(headers_synced_to, next_hash, std::nullopt, "new cycle");
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
                    // The recorded range end keeps subsequent block_validated
                    // events from re-sending a range already in flight.
                    //
                    // Held until the UTXO set describes the state below the first
                    // block of the range — see may_start_slow_sync (#663).
                    co_await try_start_slow_sync(body_range_trigger::block_validated);

                    // Check if we've caught up to headers
                    if (blocks_synced_to >= headers_synced_to && header_sync_complete) {
                        // Whether before or after checkpoint, we need to keep looking for more headers
                        // to stay in sync with the network tip
                        spdlog::info("[sync_coordinator] Block sync caught up to headers at {} (checkpoint={}), restarting header sync",
                            blocks_synced_to, checkpoint_height);

                        header_sync_complete = false;
                        header_sync_start = std::chrono::steady_clock::now();
                        headers_at_start = headers_synced_to;
                        // The range is DROPPED, not reopened: the bodies caught
                        // up to it, so it covered everything it was asked to.
                        // What comes next is decided from the heights.
                        slow_sync_end.reset();

                        auto next_hash = active_hash_at(organizer.index(), headers_synced_to);

                        co_await ask_for_headers(headers_synced_to, next_hash, std::nullopt, "restart");
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
                // The trigger names the door; the payload's height is not passed
                // and cannot reach the decision. `try_start_slow_sync` reads the
                // height itself, from the store, at the moment it evaluates.
                spdlog::debug("[sync_coordinator] UTXO built height reported as {}; re-checking",
                    advanced->built_height);
                co_await try_start_slow_sync(body_range_trigger::utxo_build_advanced);
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
                    co_await try_start_slow_sync(body_range_trigger::chunk_validated);

                    // Check if we've caught up to headers
                    if (blocks_synced_to >= headers_synced_to && header_sync_complete) {
                        spdlog::info("[sync_coordinator] Block sync caught up to headers at {} (checkpoint={}), restarting header sync",
                            blocks_synced_to, checkpoint_height);

                        header_sync_complete = false;
                        header_sync_start = std::chrono::steady_clock::now();
                        headers_at_start = headers_synced_to;
                        slow_sync_end.reset();

                        auto next_hash = active_hash_at(organizer.index(), headers_synced_to);

                        co_await ask_for_headers(headers_synced_to, next_hash, std::nullopt, "restart");
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

    // AFTER the join, and that is the whole point. Draining before the channels
    // close reads a tip that producers can still move: the walk persists through
    // H, a header validation already in flight admits H+1, the channels shut, and
    // H+1 is left accepted in memory and nowhere else -- the same shape as the
    // defect this change exists to remove, arrived at through the exit instead of
    // through the sync.
    //
    // Rereading the tip in a loop would not fix it either, because the producers
    // are what has to stop, not the reader. The join IS the quiescence barrier:
    // every task that could admit a header has ended, so the tip read here cannot
    // move afterwards.
    if (auto const tip = organizer.index().active_tip_height(); tip > 0) {
        if ( ! ensure_headers_persisted(chain, organizer.index(), uint32_t(tip))) {
            spdlog::warn("[header_persist:shutdown] Could not drain headers through {}; "
                "they will be re-fetched after a restart", tip);
        }
    }

    spdlog::info("[sync_orchestrator:shutdown] All tasks completed - orchestrator exiting");
}

} // namespace kth::node::sync
