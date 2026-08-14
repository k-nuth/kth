// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_DOWNLOAD_OWNERSHIP_HPP
#define KTH_NODE_SYNC_DOWNLOAD_OWNERSHIP_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <kth/node/define.hpp>

namespace kth::node::sync {

// =============================================================================
// Who owns a peer's download slot, and what a range change owes (#652)
// =============================================================================
//
// A block range created after the first one could end up with no workers at all,
// and the node only recovered when an unrelated peer connect or disconnect
// happened to rebuild them. Observed twice in one mainnet IBD, for 12m43s and
// 10m40s, with the chunk sitting FREE the whole time.
//
// Two things were missing, and they are different:
//
//   * the supervisor never kept the peers it knew about. `spawned_peers` held
//     the peers with a RUNNING task, which is not the same set: when a range
//     finished, every task ended and it drained to empty, so the next range had
//     nobody to start;
//   * a worker's ending carried only the peer's nonce. Replacing a coordinator
//     does not stop its workers — `stop()` sets a flag they notice between
//     chunks, and one mid-download took 59 seconds to come back in the observed
//     run — so a task_ended for the OLD range can arrive after a task for the
//     new one already exists. With only a nonce to go on, that late event would
//     retire a worker it never owned.
//
// This is the bookkeeping for both, kept apart from the coroutine so every
// ordering below can be exercised as plain values rather than raced for.
//
// @par Why an epoch of its own
// `chunk_coordinator::generation()` is the ACTIVE-CHAIN generation: a reorg bumps
// it, and two consecutive ranges without one share it. It answers "which branch
// was this downloaded under", never "which coordinator did this worker belong
// to", so it cannot decide ownership. This epoch is the supervisor's own and
// counts range requests.

/// The worker instance that holds a peer's slot. Both halves are needed: the id
/// tells two workers for the same peer apart, and the epoch tells which range
/// each belonged to.
struct download_slot {
    uint64_t task_id{0};
    uint64_t epoch{0};

    friend bool operator==(download_slot const&, download_slot const&) = default;
};

class KND_API download_ownership {
public:
    /// The peers the supervisor currently knows about, as a SNAPSHOT: a nonce
    /// absent from `nonces` has been withdrawn and must never be started again,
    /// even if a worker of its own is still on the way out.
    void set_known(std::span<uint64_t const> nonces);

    /// A new range. Bumps the epoch and answers which known peers need a worker
    /// started now — those with no live slot. A peer whose previous worker is
    /// still finishing is deliberately NOT here: it is started when that worker
    /// reports, which is the one moment its slot is free.
    [[nodiscard]] std::vector<uint64_t> begin_range();

    /// Record a worker that has just been started.
    ///
    /// Recorded AFTER the start, and that is the simplest thing that is correct.
    /// The supervisor is a single coroutine whose only receive is
    /// `co_await events.async_receive(...)`, and there is no suspension,
    /// callback or reentry between starting a worker and this call — so a report
    /// that reaches the channel first still cannot be processed until the
    /// handler returns. Claiming the slot beforehand would buy nothing here and
    /// would add a release path for a start that failed.
    ///
    /// The epoch is passed rather than read from here, so the value recorded is
    /// exactly the one the worker was given.
    void record(uint64_t nonce, uint64_t task_id, uint64_t epoch);

    /// A worker reported that it ended.
    ///
    /// Returns the nonce to start against the CURRENT coordinator, or nullopt.
    /// `coordinator_wants_workers` is the caller's answer to "is there a
    /// coordinator with work left" — the ownership does not know about chunks.
    ///
    /// Three rules, and they are the whole of it:
    ///
    ///   1. an event whose task id or epoch does not match the live slot changes
    ///      nothing. A late report never retires a worker it did not own;
    ///   2. a worker of an EARLIER epoch frees the slot and, if the peer is still
    ///      known and there is work, hands back exactly one start against the
    ///      current coordinator. This is what replaces the accidental peer event;
    ///   3. a worker of the CURRENT epoch frees the slot and nothing more.
    ///      `download_task_ended` carries no reason, so stop, completion, peer
    ///      failure and a transient error are indistinguishable here — and a
    ///      blind restart on all four is a spin. Giving it a reason is a change
    ///      to the message, not a counter bolted on to hide its absence.
    [[nodiscard]]
    std::optional<uint64_t> ended(uint64_t nonce, uint64_t task_id, uint64_t epoch,
                                  bool coordinator_wants_workers);

    [[nodiscard]] uint64_t epoch() const { return epoch_; }
    [[nodiscard]] size_t worker_count() const { return slots_.size(); }
    [[nodiscard]] bool has_worker(uint64_t nonce) const { return slots_.contains(nonce); }
    [[nodiscard]] bool knows(uint64_t nonce) const { return known_.contains(nonce); }
    [[nodiscard]] size_t known_count() const { return known_.size(); }
    [[nodiscard]] std::optional<download_slot> slot_of(uint64_t nonce) const;

private:
    boost::unordered_flat_set<uint64_t> known_;
    boost::unordered_flat_map<uint64_t, download_slot> slots_;
    uint64_t epoch_{0};
};

} // namespace kth::node::sync

#endif
