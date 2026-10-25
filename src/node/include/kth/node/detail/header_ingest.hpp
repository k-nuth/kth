// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_HEADER_INGEST_HPP
#define KTH_NODE_DETAIL_HEADER_INGEST_HPP

// Not installed. The one door every header comes in through.
//
// `headers` and the header that opens a `cmpctblock` are the same fact — here is
// a block header — and whether one answers a `getheaders` cannot be read off the
// wire. So none is treated as an answer: every one is submitted to the ingest by
// the peer's read loop, and handed to validation in turn.
//
// Who owns what:
//
//   - The queues belong to one coroutine, header_ingest_task. Nothing else
//     touches them, so they need no lock: `header_ingest` below is plain state.
//   - Read loops reach it through a bounded command channel, and only with
//     try_send. A read loop never waits on the ingest: a peer that floods, or a
//     validator that is slow, must not stall the pings, the cancellation and
//     the shutdown that the same loop serves.
//   - Validation pulls: a forwarder asks the owner for the next batch and waits
//     for validation to take it. The owner itself never waits on anything but
//     its own channel.
//
// What it holds is bounded in total, not only per peer: sessions come and go,
// and a bound per session alone would let churn accumulate queues — and the
// sessions each batch holds on to — without limit while validation is
// blocked. At most, the ingest holds:
//
//   - `header_ingest_channels::command_capacity` submissions in the channel,
//   - `header_ingest::total_capacity` batches in the owner's queues,
//   - one batch in the handoff, and one with the forwarder,
//
// each batch one `headers` message — at most `max_get_headers` headers, which
// parsing enforces — and one session reference. Validation's own input channel
// is the next bound downstream.
//
// A refusal — the command channel full, a peer's queue full, or the queues full
// in total — is local pressure, not the peer's misbehaviour: nobody is scored. It is not forgotten
// either. It rings a one-slot debt doorbell; a ring that finds the slot taken is
// a debt already pending, which will be collected after this refusal happened,
// so coalescing them loses nothing. The coordinator turns the debt into a
// request from the tip.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <variant>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain/message/header.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/network/peer_session.hpp>

#include <asio/any_io_executor.hpp>

namespace kth::node::detail {

/// One `headers` worth of headers, with the session it came from.
struct ingested_headers {
    network::peer_session::ptr peer;
    domain::message::header::list headers;
};

// -----------------------------------------------------------------------------
// The queues, as state
// -----------------------------------------------------------------------------

/// Per-peer queues of header batches, taken one peer per turn. Owned by a
/// single task; see the top of this file.
class header_ingest {
public:
    /// Batches a peer may have waiting before the next one is refused.
    static constexpr size_t per_peer_capacity = 2;

    /// Batches all peers together may have waiting before the next is refused,
    /// whoever sends it: the bound that churn cannot get around.
    static constexpr size_t total_capacity = 64;

    /// Queue a batch from `peer`. False when its queue is full, or all of them
    /// together are: refused.
    [[nodiscard]]
    bool offer(network::peer_session::ptr const& peer, domain::message::header::list headers);

    /// The next batch, one peer per turn in the order the peers first queued.
    [[nodiscard]]
    std::optional<ingested_headers> take();

    [[nodiscard]]
    size_t queued(uint64_t nonce) const;

    [[nodiscard]]
    bool empty() const { return ring_.empty(); }

    /// Batches waiting, from all peers.
    [[nodiscard]]
    size_t total() const { return total_; }

private:
    struct peer_queue {
        network::peer_session::ptr peer;
        std::deque<domain::message::header::list> batches;
    };

    boost::unordered_flat_map<uint64_t, peer_queue> queues_;
    // Peers with something queued, in turn order. A peer appears at most once.
    std::deque<uint64_t> ring_;
    size_t total_{0};
};

// -----------------------------------------------------------------------------
// The channels around it
// -----------------------------------------------------------------------------

/// A read loop's submission.
struct ingest_submit {
    network::peer_session::ptr peer;
    domain::message::header::list headers;
};

/// The forwarder asking for the next batch. At most one is outstanding.
struct ingest_pull {};

using ingest_command = std::variant<ingest_submit, ingest_pull>;

/// Something was refused, and a request from the tip is owed.
struct headers_owed {};

/// What the read loops and the ingest task share: the command channel into the
/// owner, and the debt doorbell out of it. Created and closed by the node.
struct header_ingest_channels {
    /// Submissions from read loops between two turns of the owner. The owner
    /// takes each at once, so this fills only if the owner is starved.
    static constexpr size_t command_capacity = 256;

    explicit header_ingest_channels(::asio::any_io_executor const& executor);

    header_ingest_channels(header_ingest_channels const&) = delete;
    header_ingest_channels& operator=(header_ingest_channels const&) = delete;

    /// From a read loop: submit a batch, never waiting. False when refused, in
    /// which case the debt has been rung.
    [[nodiscard]]
    bool submit(network::peer_session::ptr const& peer, domain::message::header::list headers);

    /// Ring the debt doorbell. A ring that finds a debt already pending is that
    /// debt: it will be collected after now, which is all a debt needs.
    void owe();

    /// End both: the owner and the debt bridge see the channels closed.
    void close();

    concurrent_channel<ingest_command> commands;
    concurrent_channel<headers_owed> debt;
};

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_HEADER_INGEST_HPP
