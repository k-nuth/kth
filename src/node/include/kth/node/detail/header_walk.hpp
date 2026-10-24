// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_HEADER_WALK_HPP
#define KTH_NODE_DETAIL_HEADER_WALK_HPP

// Not installed. The header walk, as state and nothing else.
//
// A `headers` message is a fact, not a response: nothing on the wire ties it to
// the `getheaders` that may or may not have caused it. So the walk never waits
// for an answer. It sends a stimulus to one peer at a time and reacts to what
// the organizer says about whatever arrives, from whoever sent it:
//
//   - progress, from any peer, is adopted: the walk starts over from the new
//     tip, everyone is eligible again, and the next stimulus goes to the peer
//     that just proved it has more;
//   - no progress counts only when it comes from the peer currently being
//     stimulated. Anyone else's empty or stale answer may be late, or may be an
//     announcement of something we already hold, and changes nothing;
//   - the active stimulus ends exactly once — by that peer's negative, by its
//     deadline, or by a failed send — through one transition keyed on a local
//     sequence number, so whichever of those comes second finds nothing to end.
//
// The sequence number is the walk's own counter. It is never compared with
// anything that came off the wire.
//
// No clock, no channels, no peers: what to do is decided here and the caller
// does it. That is what lets every edge of it be driven by a control.

#include <cstdint>
#include <optional>
#include <vector>

#include <boost/unordered/unordered_flat_set.hpp>

namespace kth::node::detail {

/// A connected peer as the walk sees it: who it is and how high it claims to be.
struct walk_peer {
    uint64_t nonce;
    uint32_t claimed_height;
};

/// One stimulus: a `getheaders` to `peer`, from the current tip, identified by
/// `seq` for its deadline and its delivery report.
struct walk_stimulus {
    uint64_t peer;
    uint64_t seq;

    friend bool operator==(walk_stimulus const&, walk_stimulus const&) = default;
};

/// A walk parked with peers still connected and nobody left to ask: every one
/// of them answered nothing in time, or was spent some other way. Nothing on
/// its own will move it — so the caller schedules `retry_due(generation)`,
/// after a delay that grows with `attempt`. A retry that finds a different
/// park, or no park, does nothing.
struct walk_retry {
    uint64_t generation;
    uint32_t attempt;

    friend bool operator==(walk_retry const&, walk_retry const&) = default;
};

/// What the caller has to do after an input. Both can be set: a confirmation is
/// never accompanied by a stimulus today, but the caller does not rely on that.
struct walk_step {
    /// Every peer that could be asked has been asked this walk without bringing
    /// progress, and at least one of them answered while being asked, to a
    /// stimulus issued after the last debt. What any of them claims to have is
    /// not consulted: a claim is a reason to ask, and once asked, only headers
    /// back it. Reported once per walk, and
    /// provisional: the next progress, debt or change of peers claiming more
    /// than the tip starts a new walk.
    bool confirm_tip{false};

    /// Send this, arm its deadline, and report back how the send went.
    std::optional<walk_stimulus> stimulate;

    /// Schedule this. Set only when the walk parks with peers to ask later.
    std::optional<walk_retry> retry;
};

class header_walk {
public:
    /// Begin a walk from the current tip: at startup, and whenever the caller
    /// has a reason of its own to look again. Does not interrupt a stimulus
    /// already out; the walk continues from its ending.
    [[nodiscard]]
    walk_step restart(uint32_t tip);

    /// The peer set changed. A stimulated peer that has gone ends its stimulus;
    /// a parked walk takes up any new eligible peer; and a peer claiming more
    /// than the tip is reason enough to start one.
    [[nodiscard]]
    walk_step peers_changed(std::vector<walk_peer> peers, uint32_t tip);

    /// The organizer accepted headers that moved the tip, from `source`.
    [[nodiscard]]
    walk_step progress(uint64_t source, uint32_t tip);

    /// The organizer accepted a prefix from `source` that moved the tip and
    /// then rejected the rest. The progress is adopted exactly as progress —
    /// a new walk from the new tip — and `source` is spent in it, since asking
    /// it again would bring the same rejected headers. One step, so at most one
    /// stimulus comes out of it.
    [[nodiscard]]
    walk_step progress_then_failure(uint64_t source, uint32_t tip);

    /// Headers from `source` moved nothing: empty, or all already held.
    [[nodiscard]]
    walk_step no_progress(uint64_t source, uint32_t tip);

    /// Headers from `source` were rejected. Not evidence about the tip either
    /// way; if `source` was being stimulated, its stimulus is over.
    [[nodiscard]]
    walk_step failure(uint64_t source, uint32_t tip);

    /// The deadline of stimulus `seq` expired.
    [[nodiscard]]
    walk_step timed_out(uint64_t seq, uint32_t tip);

    /// Stimulus `seq` could not be sent.
    [[nodiscard]]
    walk_step send_failed(uint64_t seq, uint32_t tip);

    /// Stimulus `seq` left. The only thing that settles a debt, and only for a
    /// stimulus issued after the debt was registered.
    void delivered(uint64_t seq);

    /// The retry scheduled for park `generation` is due. If the walk is still
    /// in that park, it starts over: everyone is eligible again and the next
    /// stimulus goes out. Otherwise — a peer, a header or a debt moved it on —
    /// nothing happens.
    [[nodiscard]]
    walk_step retry_due(uint64_t generation, uint32_t tip);

    /// Something says the chain may have moved and nothing in flight can be
    /// relied on to bring it: an unknown announcement, a header whose parent we
    /// lack, headers discarded under local pressure. Settled only by a stimulus
    /// issued after this call and delivered; and from this call on, only an
    /// answer to such a stimulus is evidence that the tip is current.
    [[nodiscard]]
    walk_step owe(uint32_t tip);

    // Observers, for the caller's logs and for controls.

    [[nodiscard]]
    std::optional<walk_stimulus> active() const { return active_; }

    [[nodiscard]]
    bool owed() const { return owed_; }

    [[nodiscard]]
    bool walking() const { return walking_; }

    [[nodiscard]]
    bool parked() const { return parked_; }

    [[nodiscard]]
    bool spent(uint64_t nonce) const { return spent_.contains(nonce); }

    [[nodiscard]]
    uint32_t best_known_height() const;

    [[nodiscard]]
    size_t eligible_count() const;

private:
    [[nodiscard]]
    bool known(uint64_t nonce) const;

    [[nodiscard]]
    bool eligible(uint64_t nonce) const;

    void begin_walk();

    // The one transition that ends the active stimulus. Callers have already
    // checked that it is the one they mean.
    void end_active();

    // With nothing active: stimulate the next eligible peer, or, with none left,
    // settle the walk — follow up a debt, confirm the tip, or park.
    [[nodiscard]]
    walk_step advance(uint32_t tip);

    std::vector<walk_peer> peers_;
    boost::unordered_flat_set<uint64_t> spent_;
    std::optional<walk_stimulus> active_;
    std::optional<uint64_t> preferred_;

    // The last sequence number handed out. Zero means none yet.
    uint64_t last_seq_{0};

    // A walk is open until it confirms the tip; parked is still open.
    bool walking_{false};

    // Some peer answered this walk without progress while it was being asked,
    // to a stimulus issued after the last debt. The tip is confirmed by
    // evidence from peers, never by their absence, and never by what they said
    // about the chain as it was before the debt.
    bool evidence_{false};

    // A debt, and the last sequence number issued when the latest one was
    // registered: only a later one, delivered, settles it, and only an answer
    // to a later one is evidence. Kept once the debt is settled — settling it
    // is about asking again, not about the answer.
    bool owed_{false};
    uint64_t owed_after_{0};

    // A fresh walk has already been started on behalf of the current debt.
    bool debt_walk_begun_{false};

    // Parked: walking, nobody active, nobody eligible, and not confirmed. Each
    // park has its own generation, so a retry meant for an earlier one is
    // recognised and ignored.
    bool parked_{false};
    uint64_t park_generation_{0};

    // Retries since the last answer from a peer that was asked, or the last
    // progress from anyone. Sets the backoff; an answer resets it.
    uint32_t retry_attempt_{0};

    // Something was heard in reply, or the chain moved: the backoff starts over.
    void answered();
};

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_HEADER_WALK_HPP
