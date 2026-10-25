// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_HEADER_WALK_TASKS_HPP
#define KTH_NODE_DETAIL_HEADER_WALK_TASKS_HPP

// Not installed. The tasks and the operations around the header walk. None of
// them decides anything — the decisions are header_walk's, made by the
// coordinator — and none shares state with another except through a channel.
//
// The ingest: one owner task holds the queues; a forwarder pulls batches from it
// into validation; a bridge carries the debt doorbell to the coordinator. See
// header_ingest.hpp.
//
// The coordinator's operations: a stimulus being sent, the deadline of the
// stimulus that is out, the retry of a parked walk. Each is started, replaced
// and cancelled by the coordinator alone, and each runs as a task in the
// orchestrator's group, on a strand of its own. Everything shared between the
// coordinator and such a task — whether it was cancelled, the timer, the
// cancellation slot — is touched only on that strand: the coordinator reaches
// it by posting there. That is the whole of the serialization; no lock is
// involved. A task the group has not started yet when it is cancelled finds
// the cancellation already recorded, and does not start its operation.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>

#include <kth/node/detail/header_ingest.hpp>
#include <kth/node/sync/messages.hpp>

namespace kth::node::detail {

// -----------------------------------------------------------------------------
// The ingest
// -----------------------------------------------------------------------------

using ingest_handoff_channel = concurrent_channel<ingested_headers>;

/// The owner of the ingest queues. Takes submissions and pulls from
/// `channels.commands` until it closes; never waits on anything else. A batch
/// it cannot queue rings the debt doorbell. A pull is answered on `handoff` —
/// at once if a batch is queued, or with the next one to arrive. Closes
/// `handoff` when it ends.
::asio::awaitable<void> header_ingest_task(
    header_ingest_channels& channels,
    ingest_handoff_channel& handoff);

/// Pulls batches from the owner, one at a time, and hands each to validation,
/// waiting for validation to take it. Ends when either side closes.
::asio::awaitable<void> header_ingest_forwarder(
    header_ingest_channels& channels,
    ingest_handoff_channel& handoff,
    sync::header_validation_input_channel& validation);

/// Carries each ring of the debt doorbell to the coordinator as a
/// `headers_owed_event`, waiting for room rather than dropping it.
::asio::awaitable<void> header_debt_bridge(
    header_ingest_channels& channels,
    sync::sync_coordinator_event_channel& events);

// -----------------------------------------------------------------------------
// The coordinator's operations
// -----------------------------------------------------------------------------

/// Hands a task to the caller's task group.
using spawner = std::function<void(::asio::awaitable<void>)>;

/// One event in the future: armed, re-armed or cancelled by its owner, and
/// delivered at most once per arming. The deadline of the stimulus that is out
/// is one; the retry of a parked walk is another.
///
/// An arming that is replaced or cancelled ends without delivering, whether
/// its task was waiting, had not started yet, or had just been woken. One
/// woken in the instant before the cancellation reached its strand may still
/// deliver; the events are keyed — a stimulus's sequence number, a park's
/// generation — and the walk ignores one that is no longer current.
///
/// Owned by one coroutine; arm() and cancel() are called by it alone.
class walk_timer {
public:
    walk_timer(::asio::any_io_executor const& executor,
               sync::sync_coordinator_event_channel& events,
               spawner spawn);

    walk_timer(walk_timer const&) = delete;
    walk_timer& operator=(walk_timer const&) = delete;

    /// Deliver `event` after `delay`, replacing whatever was armed.
    void arm(std::chrono::milliseconds delay, sync::sync_coordinator_event event);

    /// Deliver nothing; the armed task, if any, ends now.
    void cancel();

private:
    struct arming;

    ::asio::strand<::asio::any_io_executor> strand_;
    sync::sync_coordinator_event_channel& events_;
    spawner spawn_;
    std::shared_ptr<arming> current_;
};

/// The stimulus being sent: a `getheaders` waiting for the peer's outbound
/// queue to take it. Taken, it is reported as `header_stimulus_accepted` —
/// accepted by the queue, a local fact, not a write to the wire; the session's
/// write loop writes it. Failing — the session is stopping — it is reported as
/// `header_stimulus_failed`.
///
/// Cancelled or replaced while it waits, the send is withdrawn and nothing is
/// reported: a stimulus the walk has moved past is never sent afterwards. The
/// queue full is not a failure: waiting for room is what the send does, and
/// room returning is the event that completes it, bounded by the stimulus's
/// deadline.
///
/// Owned by one coroutine; send() and cancel() are called by it alone.
class stimulus_sender {
public:
    stimulus_sender(::asio::any_io_executor const& executor,
                    sync::sync_coordinator_event_channel& events,
                    spawner spawn);

    stimulus_sender(stimulus_sender const&) = delete;
    stimulus_sender& operator=(stimulus_sender const&) = delete;

    /// Send stimulus `seq` — a getheaders from `from_hash` — to `peer`,
    /// withdrawing whatever was being sent.
    void send(network::peer_session::ptr peer, hash_digest const& from_hash, uint64_t seq);

    /// Withdraw the send in progress, if it is still waiting.
    void cancel();

private:
    struct sending;

    ::asio::strand<::asio::any_io_executor> strand_;
    sync::sync_coordinator_event_channel& events_;
    spawner spawn_;
    std::shared_ptr<sending> current_;
};

/// The delay before retry `attempt` of a parked walk: `base`, doubled for each
/// earlier retry nobody answered, never more than `cap`.
[[nodiscard]]
std::chrono::milliseconds walk_retry_delay(std::chrono::milliseconds base,
                                           std::chrono::milliseconds cap,
                                           uint32_t attempt);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_HEADER_WALK_TASKS_HPP
