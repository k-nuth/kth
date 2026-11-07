// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/body_range.hpp>
#include <kth/node/sync/orchestrator.hpp>
#include <kth/node/sync/tip_announcement.hpp>

#include <kth/node/detail/header_ingest.hpp>
#include <kth/node/detail/header_walk.hpp>
#include <kth/node/detail/header_walk_tasks.hpp>
#include <kth/node/detail/fee_filter.hpp>
#include <kth/node/detail/tx_announcements.hpp>
#include <kth/node/detail/local_admission_tasks.hpp>
#include <kth/node/detail/tx_relay.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <kth/node/sync/reorg.hpp>

#include <chrono>
#include <thread>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <spdlog/spdlog.h>

#include <kth/infrastructure/formats/base_16.hpp>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/channel_select.hpp>

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
        case body_range_trigger::admission_retry:      return "admission_retry";
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
// waits until the channel accepts it. Returns false only when we are shutting
// down: the channel was closed (broken_pipe) or the network stopped
// (operation_aborted, through its stop token).
//
// A suspended send. When the channel has room it commits at once, in this
// frame; when it is full the coroutine suspends with the request still its
// own, and is resumed by the supervisor's receive, by the close, or by the
// stop. Nothing polls and nothing is rebuilt: a send that fails keeps its
// payload, which is simply dropped with the shutdown.
::asio::awaitable<bool> send_block_range(body_range_deps deps, uint32_t start, uint32_t end) {
    auto exec = co_await ::asio::this_coro::executor;
    using input_message = block_download_input_channel::value_type;

    // Named storage: the completion owns the request while it waits.
    auto completion = co_await ::asio::experimental::make_select(
        ::asio::experimental::send(deps.block_download_input,
            input_message{block_range_request{.start_height = start, .end_height = end}})
    ).async_wait(exec, {}, deps.network.stop_token(), ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;

    if ( ! ec && selected.index == 0) {
        spdlog::info("[sync_coordinator] block_range_request {}-{} sent", start, end);
        co_return true;
    }
    // broken_pipe: the supervisor's input was closed; operation_aborted: the
    // network stopped. Both are shutdown.
    spdlog::info("[sync_coordinator] block_range_request {}-{} abandoned ({})",
        start, end, ec ? ec.message() : "not taken");
    co_return false;
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
    auto built = deps.chain.get_utxo_built_height();

    // An absent marker over an empty set is height 0: the rule the startup
    // reconciliation already applies (blockchain::reconcile_tip). It is the state
    // of every new store, and with the checkpoint at genesis it is also the state
    // the first post-checkpoint range starts from — the only writer of the marker
    // is a UTXO batch, which needs the bodies this range would fetch. Any other
    // failure, or an absent marker over a populated set, stays unreadable.
    if ( ! built && built.error() == database::result_code::key_not_found) {
        auto const count = deps.chain.utxo_count();
        if (count && *count == 0) {
            built = 0u;
        }
    }

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

::asio::awaitable<headers_progress> on_headers_advanced(
    body_range_deps deps,
    body_range_log_memory& memory,
    headers_validated const& result,
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    std::optional<uint32_t> range_end,
    uint32_t range_end_cap) {

    if (result.count == 0) {
        co_return headers_progress{headers_synced_to, std::nullopt};
    }

    // The tip moves to where the batch put it; what peers are ASKED for stops
    // at the cap, below which the bodies the coordinator already holds begin.
    auto const moved_to = result.height;

    co_return headers_progress{
        moved_to,
        co_await open_body_range_if_owed(
            deps, memory, body_range_trigger::headers_advanced,
            blocks_synced_to, std::min(moved_to, range_end_cap), range_end)};
}

uint32_t trim_held_suffix(uint32_t blocks_synced_to, uint32_t headers_synced_to,
                          std::function<bool(uint32_t)> const& held) {
    auto end = headers_synced_to;
    while (end > blocks_synced_to && held(end)) {
        --end;
    }
    return end;
}

uint32_t trim_held_suffix(blockchain::header_index const& index,
    uint32_t blocks_synced_to, uint32_t headers_synced_to,
    std::function<bool(uint32_t)> const& held) {
    return trim_held_suffix(blocks_synced_to, headers_synced_to, [&](uint32_t height) {
        if (held(height)) {
            return true;
        }
        auto const idx = index.active_at(int32_t(height));
        return idx != blockchain::header_index::null_index && index.has_block_data(idx);
    });
}

bool refresh_validated_tip(blockchain::block_chain const& chain,
                           blockchain::header_organizer& organizer) {
    auto const view = chain.chain_view();
    if ( ! view || view->connected_tip_height > size_t(max_int32)) {
        return false;
    }
    auto const height = static_cast<int32_t>(view->connected_tip_height);
    auto const idx = organizer.index().active_at(height);
    if (idx == blockchain::header_index::null_index ||
        organizer.index().get_hash(idx) != view->tip_hash) {
        return false;
    }
    organizer.note_block_validated(height);
    return true;
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
    co_await sync_orchestrator(chain, organizer, network, network_type,
        std::move(on_fatal), node_header_walk_timing);
}

::asio::awaitable<void> sync_orchestrator(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    kth::node::p2p_node& network,
    domain::config::network network_type,
    std::function<void(std::string const&)> on_fatal,
    header_walk_timing timing
) {
    co_await sync_orchestrator(chain, organizer, network, network_type,
        std::move(on_fatal), timing, nullptr);
}

::asio::awaitable<void> sync_orchestrator(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    kth::node::p2p_node& network,
    domain::config::network network_type,
    std::function<void(std::string const&)> on_fatal,
    header_walk_timing timing,
    blockchain::block_submission_channel* submissions
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

    // Header pipeline - single input channels (CSP pattern). Headers come in
    // through the node's header ingest, whoever sent them and for whatever reason.
    header_validation_input_channel header_validation_input(executor, 100);
    header_validated_channel validated_headers(executor, 100);


    // Block pipeline - single input channels (CSP pattern)
    // A select_channel: the supervisor selects over it together with its
    // workers' output and its timer. No executor; close() is its stop signal.
    block_download_input_channel block_download_input(100);
    block_download_channel downloaded_blocks(executor, 1000);  // Buffer for out-of-order blocks
    block_validation_input_channel block_validation_input(executor, 1000);
    block_validated_channel validated_blocks(executor, 100);

    // Fast validation pipeline (chunk-based, bypasses supervisor→bridge→validation)
    // Capacity 256: storage task is slower than validation (serial disk I/O),
    // so we need buffer to absorb the speed difference. 256×16=4096 blocks max buffered.
    // A select_channel: download workers suspend on it when full, keeping their
    // chunk; the validation task's receive resumes them (see block_tasks.cpp).
    fast_validation_input_channel fast_val_input(256);
    chunk_validated_channel validated_chunks(executor, 32);

    // Block storage pipeline (writes validated chunks to flat files)
    // Capacity 128: buffer between fast parallel validation and serial disk writes
    block_storage_input_channel block_storage_input(executor, 128);
    chunk_validated_channel stored_chunks(executor, 32);

    // Control
    stop_channel stop_signal(executor, 1);

    // Sync coordinator unified event channel (created here so peer_provider can close it during shutdown)
    sync_coordinator_event_channel coordinator_events(executor, 100);
    utxo_build_progress_channel utxo_progress(executor, 1);

    // The builder's contextual refusal and the coordinator's answer to it.
    // Capacity one each: one refusal is outstanding at most (messages.hpp).
    block_rejection_channel utxo_rejections(executor, 1);
    block_rejection_ack_channel rejection_ack(executor, 1);

    // =========================================================================
    // TASKS - All independent, communicate only via channels
    // =========================================================================

    task_group all_tasks("orchestrator_tasks", executor);

    // The header walk's operations, all owned by the coordinator: the stimulus
    // being sent, the deadline of the stimulus that is out, and the retry of a
    // parked walk. Each runs as a task of this group on a strand of its own,
    // and is replaced or cancelled by the coordinator through that strand —
    // nothing polls, and nothing is shared but through it.
    auto spawn_walk_task = [&all_tasks](::asio::awaitable<void> task) {
        all_tasks.spawn("header_walk_operation", std::move(task));
    };
    detail::stimulus_sender header_stimulus(executor, coordinator_events, spawn_walk_task);
    detail::walk_timer header_deadline(executor, coordinator_events, spawn_walk_task);
    detail::walk_timer header_retry(executor, coordinator_events, spawn_walk_task);
    detail::walk_timer body_retry(executor, coordinator_events, spawn_walk_task);

    // From the ingest's owner to its forwarder: one batch, answering one pull.
    detail::ingest_handoff_channel ingest_handoff(executor, 1);

    // -------------------------------------------------------------------------
    // 1. Peer provider - receives connected peers from network and distributes
    // -------------------------------------------------------------------------
    // Unified input channel for peer_provider (CSP pattern - single channel).
    // A select_channel: no executor; the consumer waits on it with the
    // network's stop token.
    peer_provider_input_channel peer_provider_input(100);

    // Bridge task: forwards from network.peer_events() to unified channel.
    // One selection on the (select_channel) event channel, ended by the
    // network's stop token or by the channel closing: no timer race.
    all_tasks.spawn("peer_bridge", [&]() -> ::asio::awaitable<void> {
        spdlog::info("[peer_bridge] Started, forwarding peer events to peer_provider");
        auto exec = co_await ::asio::this_coro::executor;

        try {
            while (!network.stopped()) {
                auto completion = co_await ::asio::experimental::make_select(
                    ::asio::experimental::receive(network.peer_events())
                ).async_wait(exec, {}, network.stop_token(), ::asio::as_tuple(::asio::deferred));
                auto& [ec, selected] = completion;
                if (ec) {
                    spdlog::debug("[peer_bridge] Network channel closed: {}", ec.message());
                    break;
                }
                auto notification = std::move(*selected.value<0>());

                // Demultiplex based on event type
                if (notification.event == kth::node::peer_event_type::connected) {
                    if (!peer_provider_input.try_send(
                            peer_provider_input_channel::value_type{new_peer{notification.peer}})) {
                        spdlog::warn("[peer_bridge] Channel full, new_peer dropped for {}",
                            notification.peer->authority_with_agent());
                    }
                } else {
                    if (!peer_provider_input.try_send(
                            peer_provider_input_channel::value_type{peer_disconnected{notification.peer}})) {
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
    // Local block admission bridge
    // -------------------------------------------------------------------------
    //
    // The endpoint block_chain::submit_block feeds, drained here and handed to
    // the coordinator, which owns each submission until it answers. The wait is
    // armed with the network's stop token; the forward is AWAITED, since a
    // submission dropped here is a requester that never hears anything. When
    // this ends — the network stopped, or the coordinator is gone — the
    // endpoint is closed so later submitters are refused at once, and whatever
    // was queued is answered, because nobody else will.
    if (submissions != nullptr) {
        all_tasks.spawn("local_admission_bridge", [&, submissions]() -> ::asio::awaitable<void> {
            spdlog::info("[local_admission] Bridge started");
            auto exec = co_await ::asio::this_coro::executor;
            while (true) {
                auto completion = co_await ::asio::experimental::make_select(
                    ::asio::experimental::receive(*submissions)
                ).async_wait(exec, {}, network.stop_token(), ::asio::as_tuple(::asio::deferred));
                auto& [ec, selected] = completion;
                if (ec) {
                    spdlog::debug("[local_admission] Endpoint wait ended: {}", ec.message());
                    break;
                }
                auto submission = std::move(*selected.value<0>());
                auto const reply = submission.reply;
                auto [send_ec] = co_await coordinator_events.async_send(std::error_code{},
                    local_block_submitted{std::move(submission)},
                    ::asio::as_tuple(::asio::use_awaitable));
                if (send_ec) {
                    if (reply) {
                        (void)reply->try_send(blockchain::block_submission_result(error::service_stopped));
                    }
                    break;
                }
            }

            // Terminal. Closed first, so a submitter racing this gets
            // broken_pipe rather than a slot nobody will drain.
            submissions->close();
            std::error_code drain_ec;
            while (auto queued = submissions->try_receive(drain_ec)) {
                if (queued->reply) {
                    (void)queued->reply->try_send(blockchain::block_submission_result(error::service_stopped));
                }
            }
            spdlog::info("[local_admission] Bridge ended");
        });
    }

    // -------------------------------------------------------------------------
    // peer_provider - Minimal peer distributor for sync tasks
    //
    // Responsibilities:
    // - Receive peer connect/disconnect events from network
    // - Broadcast peer list to the coordinator (header walk) and block_download
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

        // Broadcast current peer list to the coordinator and block download
        auto broadcast_peers = [&]() -> ::asio::awaitable<void> {
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

            // Headers: send all peers (headers are small, slow is less of an
            // issue) to the coordinator, which owns the header walk. Awaited: a
            // peer set dropped is a walk that never learns who it may ask. The
            // coordinator never waits on this task, and closes its channel when
            // it ends, so this cannot wait forever.
            auto [send_ec] = co_await coordinator_events.async_send(std::error_code{},
                peers_updated{peers}, ::asio::as_tuple(::asio::use_awaitable));
            if (send_ec) {
                spdlog::debug("[peer_provider] Coordinator channel closed, peers_updated not delivered");
            }
            // Blocks: send only fast peers (slow peers degrade throughput significantly)
            if (!block_download_input.try_send(
                    block_download_input_channel::value_type{peers_updated{fast_peers}})) {
                spdlog::warn("[peer_provider] Channel full, peers_updated dropped for block_download");
            }
        };

        auto exec = co_await ::asio::this_coro::executor;

        // One task-owned selector for the whole loop, armed with the network's
        // stop token: the wait ends on a message, or with operation_aborted the
        // moment the network stops. No timer re-reads stopped() any more.
        auto inbox = ::asio::experimental::make_selector(exec,
            ::asio::experimental::channel_select_options{}, network.stop_token(),
            ::asio::experimental::receive(peer_provider_input));

        try {
            while (!network.stopped()) {
                auto [ec, index] = co_await inbox.wait(::asio::as_tuple(::asio::deferred));
                if (ec) {
                    spdlog::debug("[peer_provider] Input wait ended: {}", ec.message());
                    break;
                }
                (void)index;  // One case.
                // Lives in the selector's storage until the next wait begins;
                // nothing below waits on the selector.
                auto& msg = *inbox.value<0>();

                if (auto* np = std::get_if<new_peer>(&msg)) {
                    if (np->peer->stopped()) continue;
                    spdlog::info("[peer_provider] New peer: {} (nonce={})",
                        np->peer->authority_with_agent(), np->peer->nonce());
                    peers.push_back(np->peer);
                    co_await broadcast_peers();

                } else if (auto* dp = std::get_if<peer_disconnected>(&msg)) {
                    spdlog::info("[peer_provider] Peer disconnected: {} (nonce={})",
                        dp->peer->authority_with_agent(), dp->peer->nonce());
                    std::erase_if(peers, [&](auto const& p) { return p->nonce() == dp->peer->nonce(); });
                    co_await broadcast_peers();

                } else if (auto* err = std::get_if<peer_error>(&msg)) {
                    spdlog::info("[peer_provider] Peer error: {} - {}",
                        err->peer->authority_with_agent(), err->error.message());

                    // Report to reputation system - it decides if peer should be banned
                    int score = is_bannable_error(err->error) ? 100 : 10;
                    bool banned = network.report_misbehavior(err->peer, score, err->error.message());

                    if (banned) {
                        // Peer was banned - remove from local list
                        std::erase_if(peers, [&](auto const& p) { return p->nonce() == err->peer->nonce(); });
                        co_await broadcast_peers();
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

        // Cancel and close all data channels to unblock pending operations
        // Note: cancel() wakes up pending async ops, close() alone does NOT!
        spdlog::info("[peer_provider:shutdown] Closing channels...");
        header_validation_input.cancel();
        header_validation_input.close();
        validated_headers.cancel();
        validated_headers.close();
        // select_channels have no cancel(): close() completes a pending receive
        // with eof and a pending send with broken_pipe, which is the wake-up.
        block_download_input.close();
        downloaded_blocks.cancel();
        downloaded_blocks.close();
        block_validation_input.cancel();
        block_validation_input.close();
        validated_blocks.cancel();
        validated_blocks.close();
        fast_val_input.close();
        validated_chunks.cancel();
        validated_chunks.close();
        block_storage_input.cancel();
        block_storage_input.close();
        stored_chunks.cancel();
        stored_chunks.close();
        utxo_progress.cancel();
        utxo_progress.close();
        // The builder may be parked waiting for an answer to a refusal; a
        // closed ack is the answer that ends it.
        utxo_rejections.cancel();
        utxo_rejections.close();
        rejection_ack.cancel();
        rejection_ack.close();
        coordinator_events.cancel();
        coordinator_events.close();

        spdlog::info("[peer_provider] Task ended");
    });

    // -------------------------------------------------------------------------
    // 2. Header ingest task: the node's header ingest -> validation
    //    Every header that arrives goes this way, asked for or not; the
    //    coordinator's header walk only decides whom to stimulate next.
    // -------------------------------------------------------------------------
    all_tasks.spawn("header_ingest_task", detail::header_ingest_task(
        network.header_ingest(),
        ingest_handoff
    ));
    all_tasks.spawn("header_ingest_forwarder", detail::header_ingest_forwarder(
        network.header_ingest(),
        ingest_handoff,
        header_validation_input
    ));
    all_tasks.spawn("header_debt_bridge", detail::header_debt_bridge(
        network.header_ingest(),
        coordinator_events
    ));

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
                    if (!peer_provider_input.try_send(peer_provider_input_channel::value_type{*perf})) {
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

    // The bodies the store HOLDS, which is where their download resumes. The
    // connected tip is where their validation resumes, and the two part company
    // the moment the download outruns the build — on a node stopped partway
    // through a sync, by hundreds of thousands of blocks. Resuming both from the
    // connected tip asked the network for everything in between again and
    // stored every block of it a second time. Read once, here, from the index
    // the flat-file scan at open filled in; the tasks below are given the value.
    uint32_t const initial_stored_height = stored_bodies_from(chain.headers(), initial_block_height);
    if (initial_stored_height > initial_block_height) {
        spdlog::info("[sync_orchestrator] Bodies stored to {} above the connected tip {}: "
            "the download resumes above them, the validation from disk",
            initial_stored_height, initial_block_height);
    }

    // Two different heights come out of the checkpoint list. The ANCHOR is the
    // highest checkpoint: headers must match it, in either mode. The TRUSTED
    // height is the highest block that may be connected merkle-only, with no
    // script, signature or UTXO validation; everything that decides what the
    // fast and the full ranges own (below) is keyed to it. In full-validation
    // mode it is 0 whatever the anchor is.
    auto const& chain_cfg = chain.chain_settings();
    uint32_t const anchor_height = uint32_t(chain_cfg.max_checkpoint_height);
    uint32_t const checkpoint_height = uint32_t(chain_cfg.trusted_checkpoint_height());
    spdlog::info("[sync_orchestrator] Validation mode: {}; checkpoint anchor height {}; "
        "script/signature/UTXO validation starts at height {}",
        chain_cfg.full_validation ? "FULL (every block from the first)"
                                  : "optimized (merkle-only up to the checkpoint)",
        anchor_height, checkpoint_height + 1);

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
    // Published from the stored height, so the UTXO build sees the bodies on
    // disk as available from the start rather than after the first chunk the
    // storage task happens to write.
    auto contiguous_height = std::make_shared<std::atomic<uint32_t>>(initial_stored_height + 1);

    all_tasks.spawn("block_storage_task", block_storage_task(
        chain,
        block_storage_input,
        stored_chunks,
        initial_stored_height + 1,
        organizer,
        contiguous_height.get()
    ));

    // The bridge is not a reorg participant. It can wait for coordinator room
    // while the builder reaches the barrier; one pending channel wake coalesces
    // later publications. Shutdown closes both channels before joining tasks.
    all_tasks.spawn("coordinator_utxo_bridge", [&]() -> ::asio::awaitable<void> {
        while (true) {
            auto [ec, progress] = co_await utxo_progress.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                co_return;
            }
            auto [send_ec] = co_await coordinator_events.async_send(
                std::error_code{}, progress, ::asio::as_tuple(::asio::use_awaitable));
            if (send_ec) {
                co_return;
            }
        }
    });

    // 7d. Incremental UTXO build task (reads from disk as contiguous blocks become available)
    all_tasks.spawn("utxo_build_task", utxo_build_task(
        chain,
        *contiguous_height,
        initial_block_height + 1,
        network_type,
        [&network]() { return network.stopped(); },
        on_fatal,
        &utxo_progress,
        &utxo_rejections,
        &rejection_ack
    ));

    // Bridge: utxo_rejections -> coordinator_events. Awaited: a refusal
    // dropped is a builder parked forever and a requester never answered.
    all_tasks.spawn("coordinator_rejection_bridge", [&]() -> ::asio::awaitable<void> {
        while (true) {
            auto [ec, rejected] = co_await utxo_rejections.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                co_return;
            }
            auto [send_ec] = co_await coordinator_events.async_send(
                std::error_code{}, rejected, ::asio::as_tuple(::asio::use_awaitable));
            if (send_ec) {
                co_return;
            }
        }
    });

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

    // Storage results report body availability only. The builder delivers its
    // own progress after publishing a complete connected state; no timer bridge
    // or unrelated peer event is needed to wake the coordinator.
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

    // Mempool expiry (BCHN -mempoolexpiry): a transaction still unconfirmed
    // `mempool_expiry_hours` after the node first saw it leaves the pool with
    // its descendants. BCHN also checks at each admission, walking its
    // first-seen order; kth keeps no such order, so the sweep runs every hour,
    // and at least twice per expiry period, which bounds how long a transaction
    // outstays it. Ends with the network.
    if (auto const hours = chain.chain_settings().mempool_expiry_hours; hours > 0) {
        std::chrono::seconds const expiry = std::chrono::hours(hours);
        auto const period = std::min<std::chrono::steady_clock::duration>(
            std::chrono::hours(1), expiry / 2);
        all_tasks.spawn("mempool_expiry", [&, expiry, period]() -> ::asio::awaitable<void> {
            co_await chain.run_mempool_expiry(expiry, period, network.stop_token());
        });
    }

    // Transaction relay: what peers announce is asked for and offered to the
    // mempool (detail/tx_relay.hpp). The relay's state is the owner
    // coroutine's own; each getdata it sends is a task of this group. Both end with the network: stop()
    // closes the relay's channel, and the owner closes the admission channel.
    detail::tx_admission_channel tx_admission(detail::tx_admission_capacity);
    if (network.network_settings().relay_transactions) {
        auto relay_view = detail::chain_relay_view(chain);
        auto spawn_relay_task = [&all_tasks](::asio::awaitable<void> task) {
            all_tasks.spawn("tx_relay_request", std::move(task));
        };
        all_tasks.spawn("tx_relay", detail::tx_relay_task(network.tx_relay(),
            std::move(relay_view), tx_admission, std::move(spawn_relay_task)));
        // Admission takes the validation lock to commit: on the chain's own
        // executor, as sendrawtransaction's callers and the tests run it, not
        // on a network thread.
        all_tasks.spawn("tx_admission", detail::tx_admission_task(tx_admission,
            [&chain](domain::message::transaction::const_ptr tx) {
                return ::asio::co_spawn(chain.executor(), chain.submit_transaction(std::move(tx)),
                    ::asio::use_awaitable);
            },
            network.tx_relay()));
    }

    // Fee filter watch: when the mempool's minimum may have moved, each peer's
    // service hears the new value and decides whether its next fee filter
    // (BIP133) is due sooner. Only when the node relays transactions, as the
    // fee filter itself. Ends with the network.
    if (network.network_settings().relay_transactions) {
        all_tasks.spawn("fee_filter_watch", detail::fee_filter_watch(chain.mempool_fee_events(),
            detail::fee_filter_sources{
                .minimum = [&chain] { return chain.mempool_minimum_fee(); },
                .peers = [&network] { return network.peers().all(); },
            },
            network.stop_token()));

        // Transaction announcement watch: when transactions are admitted,
        // each peer's service hears how far the mempool's order reaches and
        // when the inbound peers are next due. Ends with the network.
        all_tasks.spawn("tx_announcement_watch", detail::tx_announcement_watch(chain.mempool_admission_events(),
            detail::tx_announcement_watch_sources{
                .last = [&chain] { return chain.mempool_last_admission(); },
                .peers = [&network] { return network.peers().all(); },
                .draw = detail::unit_draw,
            },
            network.stop_token()));
    }

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

        // The network's stop, as an event. A selection with nothing but a
        // never-due `after` case waits for its stop token and nothing else:
        // operation_aborted here means the network stopped. A network that was
        // never started counts as stopped already, as before, and is not waited
        // on. (This replaces a 100ms poll of network.stopped().)
        try {
            if ( ! network.stopped()) {
                auto completion = co_await ::asio::experimental::make_select(
                    ::asio::experimental::after(::asio::steady_timer::time_point::max())
                ).async_wait(exec, {}, network.stop_token(), ::asio::as_tuple(::asio::deferred));
                auto& [ec, selected] = completion;
                (void)selected;
                spdlog::info("[coordinator:stop_bridge] Woken ({}), network.stopped()={}",
                    ec ? ec.message() : "no error", network.stopped());
            }
        } catch (std::exception const& e) {
            spdlog::error("[coordinator:stop_bridge] EXCEPTION: {}", e.what());
        } catch (...) {
            spdlog::error("[coordinator:stop_bridge] UNKNOWN EXCEPTION");
        }

        // 2026-02-07: FIX - Always send stop_request when exiting
        if (!coordinator_events.try_send(std::error_code{}, stop_request{})) {
            spdlog::debug("[coordinator:stop_bridge] Channel full on exit, stop_request dropped (may be duplicate)");
        }

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
            // Awaited: a result dropped here is a walk that never hears how a
            // stimulus went, and a tip that moved without the coordinator
            // knowing.
            auto [send_ec] = co_await coordinator_events.async_send(std::error_code{},
                std::move(result), ::asio::as_tuple(::asio::use_awaitable));
            if (send_ec) {
                spdlog::debug("[coordinator:headers_bridge] Coordinator channel closed");
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
        refresh_validated_tip(chain, organizer);
        spdlog::info("[sync_coordinator] Task started");
        try {

        // The bodies held, not the bodies connected: the range this opens starts
        // above what is on disk, and the admission holds it until the UTXO set
        // has caught up with the disk.
        uint32_t blocks_synced_to = initial_stored_height;
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
        bool body_retry_armed = false;
        uint64_t body_retry_generation = 0;
        uint32_t body_retry_attempt = 0;


        // For ETA calculation
        auto header_sync_start = std::chrono::steady_clock::now();
        uint32_t headers_at_start = headers_synced_to;

        // Buffer for out-of-order chunk validation results (keyed by start_height)
        boost::unordered_flat_map<uint32_t, chunk_validated> pending_chunks;

        spdlog::info("[sync_coordinator] Initial state: blocks={}, headers={}",
            blocks_synced_to, headers_synced_to);

        // One place, called from every event that can change the answer, so they
        // cannot drift apart — which is how two copies of this trigger once came
        // to be identical by hand.
        // The ONE place an opened range is recorded, so the rule has a single
        // statement rather than one per call site.
        auto record = [&](std::optional<uint32_t> opened) {
            if (opened) {
                slow_sync_end = opened;
            }
            auto const owed = next_slow_sync_range(blocks_synced_to, headers_synced_to,
                checkpoint_height, slow_sync_end);
            if (owed && range_log.last_hold == slow_sync_admission::height_unavailable) {
                if ( ! body_retry_armed) {
                    // Only failed reads owe a deadline: ordinary builder lag is
                    // driven by publication, and an idle store is never polled.
                    auto const exponent = std::min(body_retry_attempt, uint32_t{5});
                    body_retry_attempt = std::min(body_retry_attempt + 1, uint32_t{5});
                    body_retry_armed = true;
                    body_retry.arm(std::chrono::seconds(uint32_t{1} << exponent),
                        body_range_retry{++body_retry_generation});
                }
            } else {
                if (body_retry_armed) {
                    body_retry.cancel();
                    body_retry_armed = false;
                    ++body_retry_generation;
                }
                body_retry_attempt = 0;
            }
        };

        // ---------------------------------------------------------------------
        // Local block admission
        // ---------------------------------------------------------------------
        //
        // A block handed in through the endpoint goes the way a peer's does:
        // its header to the organizer through the validation task, its body
        // through fast validation and storage, its verdict from the builder.
        // What this owns is the record of each submission until it is answered,
        // and two memories the ranges and the switch consult: the heights whose
        // bodies are local and handed over, which a range must not ask peers
        // for; and the hashes the builder refused, which the chain is
        // not switched back onto — the body would only be judged again.
        struct local_admission {
            enum class stage_t : uint8_t { header, body, connect };
            uint64_t id;
            hash_digest hash;
            uint32_t height;
            block_const_ptr block;
            std::shared_ptr<blockchain::block_submission_reply> reply;
            stage_t stage;
        };
        // Owner-scoped. The loop's own exit answers every submission still held
        // with service_stopped; an exception skips that exit, and then this
        // destructor closes the replies still open, which the requester reads
        // as service_stopped. On the container and not per element: the
        // vector moves its elements about, and a per-element destructor would
        // close the reply of a request that is still live.
        struct local_admissions {
            std::vector<local_admission> items;
            local_admissions() = default;
            local_admissions(local_admissions const&) = delete;
            local_admissions& operator=(local_admissions const&) = delete;
            ~local_admissions() {
                for (auto const& pending : items) {
                    if (pending.reply) {
                        pending.reply->close();
                    }
                }
            }
        } pending_local;
        auto& local_pending = pending_local.items;
        uint64_t next_local_id = 1;
        boost::unordered_flat_set<uint32_t> held_heights;
        boost::unordered_flat_set<hash_digest> rejected_hashes;
        auto const persisted_rejections = co_await chain.rejected_blocks();
        if ( ! persisted_rejections) {
            on_fatal("the persisted block rejection history could not be read");
            co_return;
        }
        rejected_hashes.insert(persisted_rejections->begin(), persisted_rejections->end());

        // The one answer a submission gets. A requester that gave up is a
        // channel nobody reads, and the capacity-one try_send still succeeds.
        auto const answer = [](local_admission const& pending, blockchain::block_submission_result outcome) {
            if (pending.reply) {
                (void)pending.reply->try_send(std::move(outcome));
            }
        };
        auto resolve_local = [&](size_t at, code outcome,
                                 blockchain::block_validity validity = blockchain::block_validity::unknown) {
            auto const& pending = local_pending[at];
            spdlog::info("[sync_coordinator] Local block {} at height {}: {}",
                encode_hash(pending.hash), pending.height,
                outcome ? outcome.message() : std::string("connected"));
            held_heights.erase(pending.height);
            answer(pending, blockchain::block_submission_result{outcome,
                outcome ? validity : blockchain::block_validity::valid});
            local_pending.erase(local_pending.begin() + long(at));
        };
        auto find_local = [&](auto const& predicate) -> std::optional<size_t> {
            for (size_t i = 0; i < local_pending.size(); ++i) {
                if (predicate(local_pending[i])) {
                    return i;
                }
            }
            return std::nullopt;
        };
        auto is_active = [&](hash_digest const& hash, uint32_t height) {
            auto const idx = organizer.index().find(hash);
            return idx != blockchain::header_index::null_index &&
                organizer.index().active_at(int32_t(height)) == idx;
        };
        // The highest height peers may be asked for. A height is this node's
        // own — not peers' to supply — while a local body is on its way to
        // storage (held) and from the moment the active entry at that height
        // has its data. The second half is read from the index each time, not
        // remembered: a switch replaces what sits at a height, and a cache of
        // heights would describe the branch that was left.
        auto trimmed_end = [&](uint32_t end) {
            return trim_held_suffix(organizer.index(), blocks_synced_to, end,
                [&](uint32_t height) { return held_heights.contains(height); });
        };

        // The body, serialized as a peer would send it and parsed as the
        // download parses it, handed to fast validation by a task of this
        // group (detail::local_body_handoff) that suspends on the send with the
        // network's stop token, then waits on the body's own outcome slot and
        // carries the outcome here. Stamped with the submission's id, so the
        // outcome answers this submission and no other at the same height, and
        // with the current generation, so a switch in between makes storage
        // report it stale rather than store it.
        auto hand_off_body = [&](size_t at) {
            auto& pending = local_pending[at];
            domain::chain::block const& body = *pending.block;
            data_chunk raw(body.serialized_size());
            byte_writer writer(raw);
            if ( ! body.to_data(writer)) {
                resolve_local(at, error::operation_failed);
                return;
            }
            byte_reader reader(raw);
            auto light = domain::chain::light_block::from_data(reader, true);
            if ( ! light) {
                resolve_local(at, error::operation_failed);
                return;
            }
            downloaded_chunk chunk{
                .start_height = pending.height,
                .chunk_id = 0,
                .blocks = {std::make_shared<domain::chain::light_block const>(std::move(*light))},
                .source_peer = nullptr,
                .generation = chain.chain_generation(),
                .submission = pending.id,
                .local_result = std::make_shared<local_body_result_channel>(1, 1)};
            pending.stage = local_admission::stage_t::body;
            held_heights.insert(pending.height);
            spdlog::info("[sync_coordinator] Local block {} at height {}: body handed to validation",
                encode_hash(pending.hash), pending.height);
            all_tasks.spawn("local_body_handoff", detail::local_body_handoff(
                fast_val_input, std::move(chunk), network.stop_token(), coordinator_events));
        };

        // What the organizer made of a submitted header.
        auto settle_local_header = [&](headers_validated const& result) {
            if (result.submission == 0) {
                return;
            }
            auto const at = find_local([&](local_admission const& p) {
                return p.id == result.submission && p.stage == local_admission::stage_t::header;
            });
            if ( ! at) {
                return;
            }
            auto& pending = local_pending[*at];
            bool const missing_parent = result.result.value() == error::store_block_missing_parent;
            bool const stale = result.result.value() == error::stale_chain;
            if (result.result && ! missing_parent && ! stale) {
                resolve_local(*at, result.result);
                return;
            }
            if (missing_parent) {
                resolve_local(*at, error::orphan_block);
                return;
            }
            auto const& index = organizer.index();
            auto const idx = index.find(pending.hash);
            if (idx == blockchain::header_index::null_index) {
                // Judged and not kept: a header the organizer had no use for
                // is a branch it did not take, not a block it never saw.
                resolve_local(*at, error::insufficient_work);
                return;
            }
            pending.height = uint32_t(index.get_height(idx));
            if (index.active_at(int32_t(pending.height)) != idx) {
                // A side branch, valid as far as its header goes. Its body
                // has no place in this pipeline until the branch wins, as for
                // any peer's branch.
                resolve_local(*at, error::insufficient_work);
                return;
            }
            if (index.has_block_data(idx)) {
                pending.stage = local_admission::stage_t::connect;
                return;
            }
            hand_off_body(*at);
        };

        // After the chain moved under them: a submission whose block is no
        // longer active will never be connected by this pipeline.
        auto settle_moved = [&]() {
            for (size_t i = 0; i < local_pending.size();) {
                auto const& pending = local_pending[i];
                if (pending.stage != local_admission::stage_t::header &&
                    ! is_active(pending.hash, pending.height)) {
                    resolve_local(i, error::insufficient_work);
                } else {
                    ++i;
                }
            }
        };

        // After the builder published: connected, at last.
        auto settle_connected = [&]() {
            auto const view = chain.chain_view();
            if ( ! view) {
                return;
            }
            // The view must describe the chain that is active NOW — the same
            // reading refresh_validated_tip takes. A view published for a branch
            // since abandoned names a height, and a different block may sit at
            // that height on the branch that replaced it, not yet connected.
            auto const& index = organizer.index();
            if (view->connected_tip_height > size_t(max_int32)) {
                return;
            }
            auto const view_tip = index.find(view->tip_hash);
            if (view_tip == blockchain::header_index::null_index ||
                index.active_at(int32_t(view->connected_tip_height)) != view_tip) {
                return;
            }
            for (size_t i = 0; i < local_pending.size();) {
                auto const& pending = local_pending[i];
                if (pending.stage == local_admission::stage_t::connect &&
                    view->connected_tip_height >= pending.height &&
                    is_active(pending.hash, pending.height)) {
                    resolve_local(i, error::success);
                } else {
                    ++i;
                }
            }
        };

        auto admit_local = [&](blockchain::block_submission submission) {
            local_admission pending{
                next_local_id++, null_hash, 0, submission.block, submission.reply,
                local_admission::stage_t::header};
            if ( ! submission.block) {
                answer(pending, error::operation_failed);
                return;
            }
            pending.hash = submission.block->hash();
            if (rejected_hashes.contains(pending.hash)) {
                answer(pending, blockchain::block_submission_result{error::duplicate_block,
                    blockchain::block_validity::invalid, true});
                return;
            }
            if (std::ranges::any_of(local_pending, [&](local_admission const& other) {
                    return other.hash == pending.hash;
                })) {
                answer(pending, blockchain::block_submission_result{error::duplicate_block,
                    blockchain::block_validity::unknown, true});
                return;
            }
            auto const& index = organizer.index();
            auto const idx = index.find(pending.hash);
            if (idx != blockchain::header_index::null_index) {
                if (index.has_block_data(idx)) {
                    auto const view = chain.chain_view();
                    auto const height = index.get_height(idx);
                    auto const valid = view && height >= 0 &&
                        uint64_t(height) <= view->connected_tip_height &&
                        index.active_at(height) == idx;
                    answer(pending, blockchain::block_submission_result{error::duplicate_block,
                        valid ? blockchain::block_validity::valid : blockchain::block_validity::unknown, true});
                    return;
                }
                // A header we hold without its body: the body is what is
                // being offered, and it goes the way any body does.
                pending.height = uint32_t(index.get_height(idx));
                if (index.active_at(int32_t(pending.height)) != idx) {
                    answer(pending, error::insufficient_work);
                    return;
                }
                if (local_pending.size() >= max_pending_local_submissions) {
                    answer(pending, error::operation_failed);
                    return;
                }
                local_pending.push_back(std::move(pending));
                hand_off_body(local_pending.size() - 1);
                return;
            }
            if (index.find(submission.block->header().previous_block_hash())
                    == blockchain::header_index::null_index) {
                answer(pending, error::orphan_block);
                return;
            }
            if (local_pending.size() >= max_pending_local_submissions) {
                answer(pending, error::operation_failed);
                return;
            }
            // Never waited on: the validation task's input drains into the
            // channel this loop reads, so a wait here could close a cycle. A
            // refusal is the submitter's to retry.
            if ( ! header_validation_input.try_send(std::error_code{}, downloaded_headers{
                    .headers = {submission.block->header()},
                    .source_peer = nullptr,
                    .submission = pending.id})) {
                answer(pending, error::operation_failed);
                return;
            }
            spdlog::info("[sync_coordinator] Local block {}: header handed to validation",
                encode_hash(pending.hash));
            local_pending.push_back(std::move(pending));
        };

        // Whether a branch carries a block the builder refused this run.
        auto branch_rejected = [&](blockchain::header_index::index_t head, uint32_t fork_height) {
            if (rejected_hashes.empty()) {
                return false;
            }
            auto const& index = organizer.index();
            for (auto idx = head;
                 idx != blockchain::header_index::null_index && index.get_height(idx) > int32_t(fork_height);
                 idx = index.get_ancestor(idx, index.get_height(idx) - 1)) {
                if (rejected_hashes.contains(index.get_hash(idx))) {
                    return true;
                }
            }
            return false;
        };

        auto const truncate_active = [&chain](uint32_t retained_height) {
            return chain.truncate_headers_after(retained_height);
        };

        auto try_start_slow_sync = [&](body_range_trigger trigger) -> ::asio::awaitable<void> {
            record(co_await open_body_range_if_owed(
                deps, range_log, trigger, blocks_synced_to, trimmed_end(headers_synced_to),
                slow_sync_end));
        };

        // Whether the chain already holds a hash. An announcement of something we
        // have is nothing to act on, whether it arrived before the walk or was
        // brought in by it.
        auto already_held = [&](hash_digest const& hash) {
            return organizer.index().find(hash) != blockchain::header_index::null_index;
        };

        // ---------------------------------------------------------------------
        // The header walk (#710)
        // ---------------------------------------------------------------------
        //
        // Who gets the next getheaders, when a stimulus is over, and when the
        // tip is confirmed: all decided by `walk`, from what the organizer says
        // about the headers that arrive — from anyone, asked or not. A
        // `headers` is never matched to a request; see detail/header_walk.hpp.
        detail::header_walk walk;
        std::vector<network::peer_session::ptr> header_peers;

        // The height a peer is told in the version message is the connected
        // tip's. The node sets it once at startup; from then on it follows
        // each tip connected, during initial block download too.
        auto record_connected_tip = [&] {
            if (auto const view = chain.chain_view()) {
                network.set_top_block({view->tip_hash, view->connected_tip_height});
            }
        };
        // The last header of the latest held batch, and the peer that sent it:
        // where a `from_last_batch` stimulus to that peer starts its locator.
        std::optional<std::pair<uint64_t, hash_digest>> held_batch_end;

        // Each newly connected tip is announced to the peers once, whoever
        // supplied the block: a peer, or a miner through submitblock. Not
        // during initial block download, and not the tip the node started at.
        std::optional<hash_digest> last_announced;
        if (auto const view = chain.chain_view()) {
            last_announced = view->tip_hash;
        }
        auto announce_connected_tip = [&] {
            auto const view = chain.chain_view();
            if ( ! view || view->tip_hash == last_announced) {
                return;
            }
            auto const& index = organizer.index();
            auto const tip_idx = index.find(view->tip_hash);
            auto const timestamp = tip_idx == blockchain::header_index::null_index
                ? std::nullopt : std::optional<uint32_t>(index.get_timestamp(tip_idx));
            last_announced = view->tip_hash;
            if (blockchain::recency_is_stale(timestamp, announcement_max_tip_age_seconds)) {
                return;
            }
            // Owed to each peer, never sent from here: each peer's service
            // takes the latest owed tip and decides between headers and an
            // inv from what it last announced to that peer.
            spdlog::debug("[sync_coordinator] Announcing block {} to {} peer(s)",
                encode_hash(view->tip_hash), header_peers.size());
            for (auto const& peer : header_peers) {
                peer->announce_tip(view->tip_hash);
            }
        };


        auto find_header_peer = [&](uint64_t nonce) -> network::peer_session::ptr {
            for (auto const& peer : header_peers) {
                if (peer->nonce() == nonce) {
                    return peer;
                }
            }
            return nullptr;
        };

        // The walk ran out of peers to ask, and every one that answered while
        // being asked had nothing past our tip. What they claim in their
        // version messages is not consulted. Provisional: progress or a debt
        // starts a new walk.
        auto on_tip_confirmed = [&]() -> ::asio::awaitable<void> {
            spdlog::info("[sync_coordinator] No eligible peer left at height {} ({} connected, "
                "best claimed {}): confirming the tip",
                headers_synced_to, header_peers.size(), walk.best_known_height());

            if (header_sync_complete) {
                // Confirmed again after a walk that moved the tip: the bodies
                // already follow it through on_headers_advanced.
                co_return;
            }

            if (headers_synced_to < checkpoint_height) {
                // Every peer we have is behind the checkpoint. Not complete, and
                // nothing to retry on a timer: a peer claiming more starts the
                // next walk when it connects, and progress from anyone does too.
                spdlog::warn("[sync_coordinator] All peers at height {} but checkpoint is {} - "
                    "waiting for a peer that has more", headers_synced_to, checkpoint_height);
                co_return;
            }

            header_sync_complete = true;

            auto const elapsed = std::chrono::steady_clock::now() - header_sync_start;
            auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            auto const total_headers = headers_synced_to - headers_at_start;
            auto const rate = elapsed_secs > 0 ? total_headers / elapsed_secs : total_headers;

            spdlog::info("[sync_coordinator] Header sync COMPLETE: {} headers in {}s ({}/s)",
                total_headers, elapsed_secs, rate);

            // Owned by the task group, not detached. It holds `chain` and the
            // organizer's index BY REFERENCE and it suspends between chunks, so
            // a detached one outlives the join below: the final drain would
            // overlap a writer it believed had stopped, and the orchestrator
            // could return and let the organizer be destroyed while this still
            // held a reference into it.
            //
            // In the group, join() waits for it, which is what makes the drain
            // after that join a quiescence barrier rather than a hope.
            if (headers_synced_to > initial_header_height) {
                all_tasks.spawn("header_persist",
                    persist_headers_to_db(chain, organizer.index(),
                        initial_header_height + 1, headers_synced_to));
            }

            if (headers_synced_to <= blocks_synced_to) {
                // Nothing owed to the bodies. Nothing polls either: the next
                // block reaches us as a header, a cmpctblock or an inv.
                spdlog::info("[sync_coordinator] Fully synced at height {}", blocks_synced_to);
                co_return;
            }

            if (blocks_synced_to < checkpoint_height) {
                // Fast sync stage: download up to the checkpoint. Merkle only,
                // so the UTXO set is not consulted and there is nothing to wait
                // for.
                spdlog::info("[sync_coordinator] Starting FAST block sync: {} to "
                    "{} ({} blocks)", blocks_synced_to + 1, checkpoint_height,
                    checkpoint_height - blocks_synced_to);

                // The result is not consulted: it answers false only when the
                // node is winding down, and a node that is winding down owes no
                // range.
                co_await send_block_range(
                    deps, blocks_synced_to + 1, checkpoint_height);
                // The supervisor replaces the range it was working on with
                // this one. A range a switch opened before the headers were
                // complete is therefore no longer in flight, and its end, left
                // recorded, would read as one past the checkpoint: the bodies
                // above the checkpoint would never be asked for.
                slow_sync_end.reset();
            } else {
                // Slow sync stage, through the same guarded admission as the
                // other doors onto the post-checkpoint range (#663).
                co_await try_start_slow_sync(
                    body_range_trigger::header_sync_complete);
            }
        };

        // Carry out what the walk decided. A stimulus is sent by its own task,
        // which waits for the peer's outbound queue to take it; its deadline
        // runs from now, so a queue that never makes room ends it like a peer
        // that never answers. A stimulus the walk moves past is withdrawn, and
        // one still waiting for room is never sent afterwards. A peer already
        // gone is a failed send, decided on the spot.
        auto apply = [&](detail::walk_step step, char const* why) -> ::asio::awaitable<void> {
            while (true) {
                if (step.confirm_tip) {
                    co_await on_tip_confirmed();
                }
                if ( ! step.stimulate) {
                    // The deadline belongs to the active stimulus. With none,
                    // nothing is due.
                    if ( ! walk.active()) {
                        header_deadline.cancel();
                        header_stimulus.cancel();
                    }
                    if (step.retry) {
                        // Parked with peers connected and nothing else bound
                        // to happen: try again later, later each time nobody
                        // answers. Replaces any retry for an earlier park.
                        auto const delay = detail::walk_retry_delay(
                            timing.retry_base, timing.retry_cap, step.retry->attempt);
                        spdlog::info("[sync_coordinator] Header walk parked at height {} with {} "
                            "peer(s) and none left to ask; retrying in {}ms (attempt {})",
                            headers_synced_to, header_peers.size(), delay.count(),
                            step.retry->attempt + 1);
                        header_retry.arm(delay, header_walk_retry{.generation = step.retry->generation});
                    } else if ( ! walk.parked()) {
                        header_retry.cancel();
                    }
                    co_return;
                }

                auto const stimulus = *step.stimulate;
                auto peer = find_header_peer(stimulus.peer);
                if ( ! peer || peer->stopped()) {
                    step = walk.send_failed(stimulus.seq, headers_synced_to);
                    continue;
                }

                spdlog::debug("[sync_coordinator] Header stimulus {} ({}) to {} from height {}",
                    stimulus.seq, why, peer->authority_with_agent(), headers_synced_to);
                // Replaces — and withdraws — whatever stimulus was being sent.
                // A peer may not know our tip after a fork. Include recent
                // ancestors, exponential backoff and genesis so it can find
                // the shared branch instead of restarting at genesis forever.
                auto const heights = domain::chain::block::locator_heights(headers_synced_to);
                hash_list locator;
                locator.reserve(heights.size());
                // After a full batch of headers it already held, the peer is
                // asked from the end of that batch first: it answered from a
                // point the two chains share, below where they part, and the
                // tip's own entries would only bring the same batch again.
                bool const from_batch = stimulus.from_last_batch && held_batch_end &&
                    held_batch_end->first == stimulus.peer;
                locator.reserve(heights.size() + (from_batch ? 1 : 0));
                if (from_batch) {
                    locator.push_back(held_batch_end->second);
                }
                for (auto const height : heights) {
                    locator.push_back(active_hash_at(organizer.index(), static_cast<uint32_t>(height)));
                }
                if (locator.size() > kth::max_locator) {
                    // Genesis stays last; one entry above it gives way.
                    locator.erase(locator.end() - 2);
                }
                header_stimulus.send(peer, std::move(locator), stimulus.seq);
                header_deadline.arm(timing.stimulus_timeout, header_walk_timeout{.seq = stimulus.seq});
                // Walking again: whatever retry was armed is moot.
                header_retry.cancel();
                co_return;
            }
        };

        spdlog::debug("[sync_coordinator] Starting header walk from height {}", headers_synced_to);
        co_await apply(walk.restart(headers_synced_to), "initial");

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

            if (auto* update = std::get_if<peers_updated>(&event)) {
                header_peers.clear();
                std::vector<detail::walk_peer> walk_peers;
                for (auto const& peer : update->peers) {
                    if (peer->stopped()) continue;
                    auto const version = peer->peer_version();
                    header_peers.push_back(peer);
                    walk_peers.push_back(detail::walk_peer{
                        .nonce = peer->nonce(),
                        .claimed_height = version ? version->start_height() : 0u});
                }
                spdlog::debug("[sync_coordinator] Header peers updated: {} connected",
                    header_peers.size());
                co_await apply(walk.peers_changed(std::move(walk_peers), headers_synced_to), "peers");
                continue;
            }

            if (std::holds_alternative<headers_owed_event>(event)) {
                spdlog::debug("[sync_coordinator] Headers were refused under local pressure; "
                    "a follow-up from height {} is owed", headers_synced_to);
                co_await apply(walk.owe(headers_synced_to), "ingest debt");
                continue;
            }

            if (auto* accepted = std::get_if<header_stimulus_accepted>(&event)) {
                // Taken by the peer's outbound queue: as far as the walk's debt
                // is concerned, sent.
                walk.delivered(accepted->seq);
                continue;
            }

            if (auto* failed = std::get_if<header_stimulus_failed>(&event)) {
                co_await apply(walk.send_failed(failed->seq, headers_synced_to), "send failed");
                continue;
            }

            if (auto* due = std::get_if<header_walk_retry>(&event)) {
                co_await apply(walk.retry_due(due->generation, headers_synced_to), "retry");
                continue;
            }

            if (auto* expired = std::get_if<header_walk_timeout>(&event)) {
                spdlog::debug("[sync_coordinator] Stimulus {} reached its deadline", expired->seq);
                co_await apply(walk.timed_out(expired->seq, headers_synced_to), "deadline");
                continue;
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

                if (unknown_hashes.empty() && ! announced.overflowed) {
                    spdlog::debug("[sync_coordinator] {} announced block(s), all already held",
                        announced.hashes.size());
                    continue;
                }

                // Unknown, or unknowable because the registry refused some: a
                // debt either way. Nothing already in flight can be relied on
                // to bring it — it may have been mined after the last
                // getheaders left — so only a stimulus sent after this settles
                // it.
                spdlog::debug("[sync_coordinator] {} announced block(s) unknown{}; "
                    "a follow-up from height {} is owed", unknown_hashes.size(),
                    announced.overflowed ? " (registry overflowed)" : "", headers_synced_to);
                co_await apply(walk.owe(headers_synced_to), "announcement");
                continue;
            }

            if (auto* result = std::get_if<headers_validated>(&event)) {
                // A batch from the ingest, which may or may not answer the
                // stimulus that is out. Only the walk interprets it, and only
                // through who sent it and what the organizer made of it.
                spdlog::debug("[sync_coordinator] Received: height={}, count={}, result={}",
                    result->height, result->count, result->result ? result->result.message() : "ok");

                // A heavier branch was stored: switch the chain onto it. Handled
                // before the error branch below, since a non-advancing batch also
                // reports stale_chain.
                if (result->reorg_candidate && result->reorg_fork_height >= 0) {
                    auto const fork_height = uint32_t(result->reorg_fork_height);
                    spdlog::warn("[sync_coordinator] Reorg requested: fork at height {}", fork_height);

                    if (branch_rejected(result->reorg_branch_head, fork_height)) {
                        // The builder already judged a body on this branch and
                        // refused it. Switching back would only judge it again.
                        spdlog::warn("[sync_coordinator] Reorg refused: the branch carries a block "
                            "previously rejected");
                        settle_local_header(*result);
                        continue;
                    }

                    auto const reorg = co_await execute_reorg(
                        chain, organizer, result->reorg_branch_head, fork_height,
                        [&network] { return network.stopped(); },
                        [&chain](domain::chain::header::list const& headers, size_t start_height) {
                            return chain.replace_headers_from(headers, start_height);
                        }, reorg_pipeline{&block_storage_input, contiguous_height.get()});

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
                        // execute_reorg reset the shared storage cursor while
                        // every writer was parked. Repeating that reset here
                        // could overwrite progress storage already published.

                        // A result buffered above the fork describes a body of
                        // the branch just left. Left in place, the next advance
                        // through its height would count it as a body the new
                        // branch holds — which it does not — and move the
                        // counter past a block nothing will download.
                        for (auto it = pending_chunks.begin(); it != pending_chunks.end();) {
                            it = it->second.start_height + it->second.block_count - 1 > blocks_synced_to
                                ? pending_chunks.erase(it) : std::next(it);
                        }

                        // HERE, before any branching: the branches below that do
                        // not re-drive the download are exactly the ones that
                        // would otherwise carry a range end describing blocks the
                        // switch just took away.
                        slow_sync_end = range_end_after_rewind(slow_sync_end, blocks_synced_to);
                    }

                    // Before the re-drive below: a local block the switch put on
                    // the active chain hands its body over now, and the range is
                    // trimmed around it; one it took off the chain is answered.
                    settle_local_header(*result);
                    settle_moved();

                    auto const new_tip = switched ? chain.headers().active_tip_height() : -1;
                    if (switched && new_tip > int32_t(blocks_synced_to)) {
                        // The new branch is headers-only from the fork up, so block
                        // download refills from the rewound tip.
                        headers_synced_to = uint32_t(new_tip);
                        spdlog::warn("[sync_coordinator] Reorg complete: blocks rewound to {}, "
                            "headers now at {}", blocks_synced_to, headers_synced_to);

                        auto const asked_to = trimmed_end(headers_synced_to);

                        // Recorded like any other range. Left unrecorded, a
                        // stale end from before the switch would read as a range
                        // still in flight over heights the reorg just rewound,
                        // and the remainder above it would never be opened.
                        log_range_opening(
                            slow_sync_range{blocks_synced_to + 1, asked_to},
                            body_range_trigger::reorg, headers_synced_to, blocks_synced_to,
                            slow_sync_end, std::nullopt);

                        if (asked_to <= blocks_synced_to) {
                            // Everything above the rewound tip is a body this
                            // node holds itself; nothing is peers' to supply.
                            spdlog::info("[sync_coordinator] Reorg: every owed body is local; "
                                "no range asked for");
                        } else if (co_await send_block_range(deps, blocks_synced_to + 1, asked_to)) {
                            // The only place a new end is recorded, and only once
                            // the request was actually sent. A send that failed
                            // leaves nothing recorded, and the rewind above has
                            // already dropped what described the old branch, so
                            // the next event re-derives the range from the
                            // heights instead of holding behind it.
                            slow_sync_end = asked_to;
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

                    // A switch moved the tip as surely as a forward batch does,
                    // so the walk adopts it and asks from the new tip. One that
                    // did not leaves the walk where it was: the stimulus out, if
                    // any, ends by its deadline.
                    if (switched && result->source_peer) {
                        co_await apply(walk.progress(result->source_peer->nonce(), headers_synced_to),
                            "reorg");
                    }
                    continue;
                }

                // ONE progress point, ahead of the success/error split, because the
                // progress is the same fact either way: `count` is what the
                // organizer accepted and `result` is why it stopped, and a batch
                // that added a valid prefix before hitting a bad header moved the
                // tip by that prefix. Splitting first is how the error path came
                // to advance the tip and tell nobody.
                // A submitted header, first: if the organizer took it as the tip,
                // its body is handed over here, and the range opened below is
                // trimmed around it rather than asking peers for a block only
                // this node has.
                settle_local_header(*result);

                auto const prev_headers_synced = headers_synced_to;
                {
                    auto const progress = co_await on_headers_advanced(
                        deps, range_log, *result,
                        blocks_synced_to, headers_synced_to, slow_sync_end,
                        trimmed_end(result->height));
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

                // Every batch comes from the ingest with the session it arrived
                // on. Without one there is nobody for the walk to credit.
                if ( ! result->source_peer) {
                    continue;
                }
                auto const source = result->source_peer->nonce();
                auto const source_name = result->source_peer->authority_with_agent();

                bool const missing_parent = result->result.value() == error::store_block_missing_parent;
                bool const stale = result->result.value() == error::stale_chain;
                bool const rejected = result->result && ! missing_parent && ! stale;

                // A rejection is reported on the path it always had, whatever
                // else the batch did: a valid prefix ahead of the bad header
                // does not excuse it.
                if (rejected) {
                    // Header validation failed - normal network behavior (peer on wrong chain)
                    spdlog::debug("[sync_coordinator] Header validation failed: {} from peer {}",
                        result->result.message(), source_name);

                    // Report error to peer_provider - it decides what to do (ban, exclude, etc.)
                    if (!peer_provider_input.try_send(peer_provider_input_channel::value_type{peer_error{
                        .peer = result->source_peer,
                        .error = result->result
                    }})) {
                        spdlog::warn("[sync_coordinator] Channel full, peer_error dropped for header validation");
                    }
                }

                // ONE progress point for the walk, ahead of the split on why
                // the batch stopped — the same rule as the tip update above
                // (#685). A batch that added a valid prefix moved the tip
                // whoever sent it and however it ended, and the walk has to
                // start over from there: left to the error branch, a prefix
                // from a peer that was not being asked would move the tip and
                // nothing else, and a parked walk would stay parked on a tip it
                // no longer has.
                if (result->count > 0) {
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

                    if (rejected) {
                        spdlog::debug("[sync_coordinator] Headers synced to {} before the failure "
                            "from {}", headers_synced_to, source_name);
                    }
                    // One walk input, so one stimulus at most: progress adopted,
                    // and the source spent when the rest of its batch was bad.
                    co_await apply(rejected
                        ? walk.progress_then_failure(source, headers_synced_to)
                        : walk.progress(source, headers_synced_to), "progress");
                    continue;
                }

                // Nothing moved. Why, decides what the walk hears.
                if (missing_parent) {
                    // Headers that do not connect to anything we hold. That is
                    // missing information, not proof of an invalid chain: the
                    // usual cause is a block announced on top of one we have not
                    // heard about yet. No report against the peer; a request
                    // from our tip is owed, and it is what fills the gap.
                    spdlog::debug("[sync_coordinator] Headers from {} do not connect to the "
                        "index; a follow-up from height {} is owed", source_name, headers_synced_to);
                    co_await apply(walk.owe(headers_synced_to), "missing parent");
                } else if (stale) {
                    // Not a failure. `stale_chain` is the organizer saying the
                    // batch was valid and added nothing — which is what a peer
                    // at our tip sends when it answers our locator with headers
                    // we already hold. The wire cannot tell that apart from
                    // progress; only this verdict can (#705).
                    //
                    // Unless the batch was full and ends on a header we hold
                    // that is not the tip. Then the peer matched our locator
                    // below where its chain and ours part — after a fork deeper
                    // than the locator's dense entries, that is all the
                    // locator offers it — and has more past that point (#927).
                    auto const& index = organizer.index();
                    auto const last_idx = result->received == kth::max_get_headers
                        ? index.find(result->last_header) : blockchain::header_index::null_index;
                    if (last_idx != blockchain::header_index::null_index &&
                        result->last_header != organizer.header_tip_hash()) {
                        auto const last_height = uint32_t(index.get_height(last_idx));
                        spdlog::debug("[sync_coordinator] A full batch of held headers from {}, "
                            "ending at height {}; asking it again from there", source_name, last_height);
                        // Only the asked peer's answer moves the walk, so only
                        // its batch end is kept.
                        if (walk.active() && walk.active()->peer == source) {
                            held_batch_end = std::pair{source, result->last_header};
                        }
                        co_await apply(walk.held_batch(source, last_height, headers_synced_to),
                            "held batch");
                        continue;
                    }
                    spdlog::debug("[sync_coordinator] No new headers at height {} from {}",
                        headers_synced_to, source_name);
                    co_await apply(walk.no_progress(source, headers_synced_to), "no progress");
                } else if (rejected) {
                    co_await apply(walk.failure(source, headers_synced_to), "validation failed");
                } else {
                    // Nothing at all past our locator: the other way to say "no
                    // progress".
                    spdlog::debug("[sync_coordinator] Empty headers at height {} from {}",
                        headers_synced_to, source_name);
                    co_await apply(walk.no_progress(source, headers_synced_to), "empty");
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

                        co_await apply(walk.restart(headers_synced_to), "restart");
                    }
                } else {
                    spdlog::error("[sync_coordinator] Block validation failed at {}: {}",
                        result->height, result->result.message());

                    // Report error to peer_provider - it decides what to do
                    spdlog::debug("[sync_coordinator] Reporting error to peer_provider...");
                    if (result->source_peer) {
                        if (!peer_provider_input.try_send(peer_provider_input_channel::value_type{peer_error{
                            .peer = result->source_peer,
                            .error = result->result
                        }})) {
                            spdlog::warn("[sync_coordinator] Channel full, peer_error dropped");
                        }
                    }
                    spdlog::debug("[sync_coordinator] Breaking out of event loop...");
                    break;
                }
                continue;
            }

            if (auto* retry = std::get_if<body_range_retry>(&event)) {
                if ( ! body_retry_armed || retry->generation != body_retry_generation) {
                    continue;
                }
                body_retry_armed = false;
                co_await try_start_slow_sync(body_range_trigger::admission_retry);
                continue;
            }

            if (std::holds_alternative<utxo_build_advanced>(event)) {
                // A delayed wake-up may describe a branch since abandoned. Read
                // the immutable publication now; storage and event payloads are
                // not authority to advance validation or finalization.
                refresh_validated_tip(chain, organizer);
                settle_connected();
                record_connected_tip();
                announce_connected_tip();
                // The orphans a connected block resolves (detail/tx_relay.hpp).
                network.tx_relay().tip_moved();
                co_await try_start_slow_sync(body_range_trigger::utxo_build_advanced);
                continue;
            }

            if (auto* submitted = std::get_if<local_block_submitted>(&event)) {
                admit_local(std::move(submitted->submission));
                continue;
            }

            if (auto* rejected = std::get_if<block_rejected>(&event)) {
                // The builder's verdict on a body, whoever supplied it, and the
                // builder is parked until this answers. Remembered for the run
                // first, so no switch brings the same body back for judgement.
                spdlog::warn("[sync_coordinator] The builder rejected block {} at height {}: {}",
                    encode_hash(rejected->hash), rejected->height, rejected->reason.message());
                if (auto const ec = co_await chain.record_rejected_block(rejected->hash); ec) {
                    (void)rejection_ack.try_send(std::error_code{});
                    on_fatal("a contextual block rejection could not be persisted");
                    break;
                }
                rejected_hashes.insert(rejected->hash);

                auto const outcome = co_await execute_body_rejection(
                    chain, organizer, rejected->height, rejected->hash,
                    network.stop_token(), truncate_active,
                    reorg_pipeline{&block_storage_input, contiguous_height.get()});

                if (outcome.fatal) {
                    (void)rejection_ack.try_send(std::error_code{});
                    on_fatal("the persisted chain could not be stepped off a rejected block");
                    break;
                }

                if (outcome.stepped) {
                    headers_synced_to = uint32_t(std::max<int32_t>(0, organizer.index().active_tip_height()));
                    if (blocks_synced_to >= rejected->height) {
                        blocks_synced_to = rejected->height - 1;
                    }
                    for (auto it = pending_chunks.begin(); it != pending_chunks.end();) {
                        it = it->second.start_height + it->second.block_count - 1 >= rejected->height
                            ? pending_chunks.erase(it) : std::next(it);
                    }
                    slow_sync_end = range_end_after_rewind(slow_sync_end, blocks_synced_to);
                }

                if (auto const at = find_local([&](local_admission const& p) {
                        return p.hash == rejected->hash; })) {
                    resolve_local(*at, rejected->reason, blockchain::block_validity::invalid);
                }
                settle_moved();

                // Answered after the chain and the cursors moved: the builder
                // re-derives its own from them when it wakes.
                (void)rejection_ack.try_send(std::error_code{});

                if (outcome.stepped) {
                    co_await apply(walk.restart(headers_synced_to), "body rejected");
                }
                continue;
            }

            if (auto* result = std::get_if<chunk_validated>(&event)) {
                // A local body's outcome names its submission; it is matched by
                // that and nothing else. The submission may already be answered
                // — a switch answered it — in which case the outcome is nobody's.
                std::optional<size_t> local_at;
                if (result->submission != 0) {
                    local_at = find_local([&](local_admission const& p) {
                        return p.id == result->submission &&
                            p.stage == local_admission::stage_t::body;
                    });
                    if (result->result.value() == error::stale_chain) {
                        // Dropped by storage as stale: never stored, so no
                        // counter moves for it. The switch that made it stale
                        // answered the submission already; if it is still
                        // here, the block is on a branch the chain left.
                        if (local_at) {
                            resolve_local(*local_at, error::insufficient_work);
                        }
                        continue;
                    }
                    if ( ! local_at) {
                        // This submission was retired (for example by a reorg).
                        // Even a late successful store describes that retired
                        // request, not the current branch's body cursor.
                        continue;
                    }
                    auto const& pending = local_pending[*local_at];
                    if ( ! is_active(pending.hash, pending.height)) {
                        resolve_local(*local_at, error::insufficient_work);
                        continue;
                    }
                }

                if (!result->result) {
                    if (local_at) {
                        // Stored. Not held any more: the range may be asked up
                        // to it, and the builder decides the rest.
                        local_pending[*local_at].stage = local_admission::stage_t::connect;
                        held_heights.erase(local_pending[*local_at].height);
                        // Publication can reach the coordinator before this
                        // storage acknowledgement. Re-check now rather than
                        // waiting for an unrelated future publication.
                        settle_connected();
                    }

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

                        co_await apply(walk.restart(headers_synced_to), "restart");
                    }
                } else {
                    if (local_at) {
                        // A body that does not match its header, or could not
                        // be stored. Nothing was stored and nothing advanced;
                        // the header stays, since a wrong body says nothing
                        // about it, and the right one can still be offered.
                        resolve_local(*local_at, result->result);
                        // The valid header is still active and its body is now
                        // owed from peers. The rejection itself releases that
                        // debt; do not wait for an unrelated announcement.
                        co_await try_start_slow_sync(body_range_trigger::chunk_validated);
                        continue;
                    }

                    spdlog::error("[sync_coordinator] Chunk validation failed at height {}: {}",
                        result->start_height, result->result.message());

                    if (result->source_peer) {
                        if (!peer_provider_input.try_send(peer_provider_input_channel::value_type{peer_error{
                            .peer = result->source_peer,
                            .error = result->result
                        }})) {
                            spdlog::warn("[sync_coordinator] Channel full, peer_error dropped");
                        }
                    }
                    break;
                }
                continue;
            }
        }

        // Every submission still owned is answered before anything closes:
        // nobody else will, and a requester must never outwait a coordinator
        // that is gone.
        for (auto const& pending : local_pending) {
            answer(pending, error::service_stopped);
        }
        local_pending.clear();

        spdlog::info("[sync_coordinator] Event loop ended, task completing...");
        } catch (std::exception const& e) {
            spdlog::error("[sync_coordinator] EXCEPTION: {}", e.what());
        } catch (...) {
            spdlog::error("[sync_coordinator] UNKNOWN EXCEPTION");
        }

        // Nobody reads this channel from here on. Disarmed, so the clock has
        // nothing to report; closed, so every task waiting to tell the
        // coordinator something — a deadline, a peer set, a header result —
        // gets an answer instead of waiting for room that never comes.
        header_stimulus.cancel();
        header_deadline.cancel();
        header_retry.cancel();
        body_retry.cancel();
        utxo_progress.cancel();
        utxo_progress.close();
        // A builder parked on a refusal is woken by the closed ack.
        utxo_rejections.cancel();
        utxo_rejections.close();
        rejection_ack.cancel();
        rejection_ack.close();
        coordinator_events.cancel();
        coordinator_events.close();
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
