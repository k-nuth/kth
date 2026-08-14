// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_UTXO_GATE_HPP
#define KTH_BLOCKCHAIN_UTXO_GATE_HPP

#include <chrono>
#include <expected>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <string>
#include <utility>

#include <kth/blockchain/define.hpp>
#include <kth/database/databases/result_code.hpp>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth::blockchain {

class utxo_gate;

/// Raised when a thread that already holds the exclusive window asks for another
/// window, or for a read lease.
///
/// FAIL-FAST DETECTION, never authorisation: the recorded thread id is used only
/// to refuse, and never to grant, bypass or transfer a capability. Reentering
/// would deadlock — the gate is deliberately not recursive — and this turns that
/// hang into an immediate, named error at the site that caused it.
///
/// It exists because scope discipline alone proved insufficient: three accidental
/// reentries occurred in one change, one of them in production (`start()` in
/// reference mode, where a window covering the whole function met the wiring
/// below it).
struct KB_API utxo_reentry_error : std::logic_error {
    using std::logic_error::logic_error;
};

/// Raised when a capability is moved or used by a thread other than the one that
/// took it from the gate.
///
/// The contract IS thread-affine, and this makes that checkable instead of
/// merely written down. The exclusion this gate sells is over a window in one
/// thread's execution: the scope audit that justifies it is per function per
/// thread, and the rule that no capability may live across a `co_await` exists
/// precisely because a coroutine resumes on whichever executor thread is free —
/// which is a thread transfer wearing a different shape.
///
/// A capability that authorised anywhere would also silence the reentry
/// detection: the gate's recorded owner would still name the thread that took
/// the window, so the thread actually holding it could ask for another one and
/// be told to wait for itself.
struct KB_API utxo_affinity_error : std::logic_error {
    using std::logic_error::logic_error;
};

/// Raised when a thread holding the UTXO window reaches for a lock that the rest
/// of the node takes BEFORE the UTXO gate.
///
/// The node has two locks and one order. The transaction organizer takes
/// `validation_mutex_` and then, inside `validator_.accept()`, a UTXO read
/// lease. A connect batch that still held the window while taking that same
/// mutex would be acquiring them in the opposite order, and two threads doing
/// both at once is the AB-BA deadlock — which appears as a node that stops, with
/// no error and nothing in the log.
///
/// Detection, exactly like utxo_reentry_error: the recorded thread is read only
/// to refuse. The right fix is always to end the window first — the invariant is
/// that nothing under it may take another of the node's locks.
struct KB_API utxo_lock_order_error : std::logic_error {
    using std::logic_error::logic_error;
};

/// Raised when a capability does not authorise the access it was offered for.
///
/// A programming error, surfaced rather than left as undefined behaviour: a
/// token from a different gate, or one already released, proves exclusion over
/// something that is not this store — or over nothing at all.
struct KB_API utxo_capability_error : std::logic_error {
    using std::logic_error::logic_error;
};

namespace detail {

/// Shared by both capabilities: a transfer is legal only while the thread doing
/// it is the one the gate issued to. An empty capability transfers nothing, so
/// it moves freely.
inline void check_capability_move(bool held, std::thread::id owner, char const* what) {
    if (held && owner != std::this_thread::get_id()) {
        throw utxo_affinity_error(
            std::string("a UTXO ") + what + " was moved by a thread other than the "
            "one that took it; capabilities are thread-affine");
    }
}

/// The pending-writer bookkeeping of utxo_gate, extracted so its exceptional
/// path can be exercised directly rather than argued about.
///
/// Writer preference means a reader waits for the pending count to reach zero.
/// So a writer that stops waiting — by timing out, or by unwinding — owes two
/// things and not one: the count restored AND a notification. The decrement
/// alone changes nothing for a reader already parked in `cv.wait()`, because a
/// condition variable re-evaluates its predicate only when it is notified. A
/// count restored in silence leaves that reader waiting for a writer that is
/// already gone.
///
/// @par The order, which is the whole of it
/// Decrement under the mutex, release the mutex, then notify.
///   - unguarded decrement would race the readers that read the count;
///   - notifying while still holding the mutex is legal, but wakes readers
///     straight into blocking on it again;
///   - notifying before the decrement would be the premature one: the reader
///     wakes, finds its predicate still false and parks again, having consumed
///     the only notification it was going to get.
class pending_writer {
public:
    pending_writer(size_t& count, std::condition_variable& cv,
                   std::unique_lock<std::mutex>& lock)
        : count_(&count), cv_(&cv), lock_(&lock) {
        ++*count_;
    }

    pending_writer(pending_writer const&) = delete;
    pending_writer& operator=(pending_writer const&) = delete;

    /// The wait succeeded and the caller is becoming the writer: restore the
    /// count and stand down. No notification, because the caller is about to
    /// set writer_active_ and every reader's predicate stays false either way;
    /// and the lock stays held, because the caller still has state to publish
    /// under it.
    void acquired() {
        if (count_ == nullptr) {
            return;
        }
        --*count_;
        count_ = nullptr;
    }

    /// Gave up, or unwinding out of the wait.
    ///
    /// The mutex is held here: `condition_variable::wait` re-acquires it before
    /// propagating an exception, so an unwinding writer arrives holding it just
    /// as a timed-out one does. `owns_lock()` is checked anyway — a destructor
    /// may not throw, and `unlock()` on a lock that is not held does.
    ~pending_writer() {
        if (count_ == nullptr) {
            return;
        }
        --*count_;
        if (lock_->owns_lock()) {
            lock_->unlock();
        }
        cv_->notify_all();
    }

private:
    size_t* count_;
    std::condition_variable* cv_;
    std::unique_lock<std::mutex>* lock_;
};

} // namespace detail


/// Proof that the exclusive window over the UTXO store is held.
///
/// UTXO-Z 0.10 requires apply_deletes() to run with no find(), resolve(),
/// insert(), compaction or close() in flight: it erases from the active
/// containers AND writes through the file cache's mappings, and the library's
/// own lock covers resolve-vs-resolve only. This is that window (#649).
///
/// It is a capability, not a convention. The store cannot be reached without one
/// of these or a read lease — see guarded_store — so a caller that forgets is a
/// compile error rather than a race, and a caller added later inherits the rule
/// without knowing it exists.
///
/// @par It covers the OPERATION, not the call
/// Held across the whole logical mutation: for a connect batch from before the
/// first insert until the deletions have left the set coherent; for a
/// reorganization the entire rewind, restorations and final sweep included. Per
/// call would still keep readers out of the mappings, but it would let one in
/// between the inserts and the deletions — where the set holds outputs the
/// blocks spent, and the transition record does not protect a reader that never
/// consults it.
class KB_API utxo_write_window {
public:
    utxo_write_window(utxo_write_window const&) = delete;
    utxo_write_window& operator=(utxo_write_window const&) = delete;

    /// Movable, and DELIBERATELY not noexcept: the move is checked, because a
    /// capability that changed threads silently would authorise a store access
    /// from a thread the gate never admitted.
    utxo_write_window(utxo_write_window&& other)
        : gate_(nullptr), owner_() {
        detail::check_capability_move(other.gate_ != nullptr, other.owner_, "write window");
        gate_ = std::exchange(other.gate_, nullptr);
        owner_ = std::exchange(other.owner_, std::thread::id{});
        take_state_from(other);
    }

    utxo_write_window& operator=(utxo_write_window&& other) {
        if (this != &other) {
            // Checked BEFORE releasing: a refused move must leave both sides as
            // they were, not destroy the capability it declined to overwrite.
            detail::check_capability_move(other.gate_ != nullptr, other.owner_, "write window");
            release();
            gate_ = std::exchange(other.gate_, nullptr);
            owner_ = std::exchange(other.owner_, std::thread::id{});
            take_state_from(other);
        }
        return *this;
    }

    /// Releases on every path, including an exception or a cancellation midway.
    /// The door must never be left shut by a failure — the failure's own
    /// fail-closed handling refuses to publish, which is a different mechanism.
    ///
    /// Destruction is deliberately NOT affinity-checked. A destructor that threw
    /// during unwinding would call std::terminate, so releasing from another
    /// thread is defined rather than refused: it takes the gate's mutex like any
    /// other release, opens the door and wakes whoever waits. Refusing the USE is
    /// what protects the store; refusing the RELEASE would only strand it.
    ~utxo_write_window() { release(); }

    [[nodiscard]] bool held() const { return gate_ != nullptr; }

    // -------------------------------------------------------------------------
    // Three facts, and they are three because two of them get the answer wrong
    // -------------------------------------------------------------------------
    //
    // What the window has to know is not one thing:
    //
    //   * `mark_mutating()` — an operation that CAN leave partial progress is
    //     about to start. Said BEFORE the call, because afterwards there may be
    //     no one left to say it: an exception, a cancellation and an early
    //     return all leave through the destructor and nowhere else. This governs
    //     poison, and it is deliberately pessimistic — it claims only that
    //     something MIGHT have been applied;
    //
    //   * `mark_mutated()` — something WAS applied, and there is evidence.
    //     `deletion_progress.erased` is that evidence for a deletion batch, and
    //     the library documents it as exact even on the failure path. This is
    //     what `switch_result.mutated` reports, and it must not be the flag
    //     above: the reorg caller republishes the chain view when it is true, so
    //     a conservative true moves the generation and drops the template cache
    //     for a switch that touched nothing, while a false negative leaves the
    //     published view describing a branch the node has already left;
    //
    //   * `complete()` — the operation reached ITS OWN safe boundary. Which
    //     boundary that is belongs to the operation, not to the gate: a connect
    //     batch is safe once `utxo_sync` returns, because its LMDB record and
    //     its UTXO-Z mutations have to agree on disk; a compaction is safe the
    //     moment `compact_all()` returns success, because it changes which files
    //     hold the set and not what the set contains, so there is no second
    //     store for it to agree with and no barrier it owes. Baking either rule
    //     in here would poison the other operation for following its own
    //     protocol correctly.
    //
    // A window that was never marked — statistics, a bloom walk, the open —
    // leaves without poison whatever else happened to it.

    /// An operation that may apply part of its work is about to begin.
    void mark_mutating() { mutation_may_have_started_ = true; }

    /// Evidence that something was applied. Separate from the above on purpose;
    /// see the note there.
    void mark_mutated() {
        mutation_may_have_started_ = true;
        ever_mutated_ = true;
    }

    /// The operation reached the boundary past which abandoning it is safe.
    ///
    /// A contract error on a window that was never marked, and on a second call:
    /// both mean the caller's idea of its own protocol and this window's
    /// disagree, and the failure worth having is the loud one rather than a
    /// store that quietly stops being poisoned.
    void complete() {
        if ( ! mutation_may_have_started_) {
            throw utxo_capability_error(
                "complete() on a window that never declared a mutation: either the "
                "operation forgot mark_mutating(), or it has nothing to complete");
        }
        if (completed_) {
            throw utxo_capability_error(
                "complete() called twice on one window; the safe boundary is "
                "crossed once");
        }
        completed_ = true;
    }

    /// Whether anything was actually applied — the answer `switch_result.mutated`
    /// carries. Evidence, never the conservative flag.
    [[nodiscard]] bool has_mutated() const { return ever_mutated_; }

    /// Whether releasing now would poison the gate. For controls and
    /// diagnostics: a caller decides by marking, not by asking.
    [[nodiscard]] bool would_poison() const {
        return mutation_may_have_started_ && ! completed_;
    }

    /// Whether this capability was issued by `gate` and is still held. A token
    /// from another gate authorises nothing: it proves exclusion over a store
    /// that is not the one being reached for.
    [[nodiscard]] bool authorises(utxo_gate const& gate) const {
        return gate_ == &gate;
    }

    /// Whether the asking thread is the one the gate issued this to. Checked at
    /// every access, so a capability that reached another thread stops
    /// authorising there even though it still names the right gate.
    [[nodiscard]] bool on_issuing_thread() const {
        return owner_ == std::this_thread::get_id();
    }

private:
    friend class utxo_gate;
    explicit utxo_write_window(utxo_gate& gate)
        : gate_(&gate), owner_(std::this_thread::get_id()) {}
    void release();

    /// The three facts move with the gate they describe, and the source is left
    /// clean. A moved-from window holds no gate, so it releases nothing and can
    /// poison nothing — leaving its flags set would be harmless today and a trap
    /// the first time one is reused.
    void take_state_from(utxo_write_window& other) {
        mutation_may_have_started_ = std::exchange(other.mutation_may_have_started_, false);
        ever_mutated_ = std::exchange(other.ever_mutated_, false);
        completed_ = std::exchange(other.completed_, false);
    }

    utxo_gate* gate_;
    std::thread::id owner_;

    /// Not carried across a move: a moved-from window releases nothing, and the
    /// receiving one takes the facts with the gate. See the move operations,
    /// which transfer all three.
    bool mutation_may_have_started_{false};
    bool ever_mutated_{false};
    bool completed_{false};
};

/// Permission to CLOSE the store, and nothing else.
///
/// A latched gate refuses every window and every lease, which is the point — and
/// which would also make it impossible to shut the store down cleanly, since
/// close() needs the same exclusion any mutation does. This is the one way past
/// that, and it is deliberately a separate type rather than a flag on the write
/// window: a bool would be one argument away from authorising a write.
///
/// What it can do is bounded by what accepts it. `guarded_store::with_close` is
/// the only overload that takes one, and it hands the store to a callback that
/// may not return a reference or a pointer — the same constraint the other two
/// carry — so the store cannot escape through it either.
///
/// It does NOT clear the latch. Closing is how a node winds down; it is not a
/// repair, and a gate that came back clean after a close would let the next
/// start() run over a store nobody established anything about.
class KB_API utxo_close_authority {
public:
    utxo_close_authority(utxo_close_authority const&) = delete;
    utxo_close_authority& operator=(utxo_close_authority const&) = delete;

    utxo_close_authority(utxo_close_authority&& other)
        : gate_(nullptr), owner_() {
        detail::check_capability_move(other.gate_ != nullptr, other.owner_, "close authority");
        gate_ = std::exchange(other.gate_, nullptr);
        owner_ = std::exchange(other.owner_, std::thread::id{});
    }

    utxo_close_authority& operator=(utxo_close_authority&& other) {
        if (this != &other) {
            detail::check_capability_move(other.gate_ != nullptr, other.owner_, "close authority");
            release();
            gate_ = std::exchange(other.gate_, nullptr);
            owner_ = std::exchange(other.owner_, std::thread::id{});
        }
        return *this;
    }

    ~utxo_close_authority() { release(); }

    [[nodiscard]] bool held() const { return gate_ != nullptr; }

    [[nodiscard]] bool authorises(utxo_gate const& gate) const {
        return gate_ == &gate;
    }

    [[nodiscard]] bool on_issuing_thread() const {
        return owner_ == std::this_thread::get_id();
    }

private:
    friend class utxo_gate;
    explicit utxo_close_authority(utxo_gate& gate)
        : gate_(&gate), owner_(std::this_thread::get_id()) {}
    void release();

    utxo_gate* gate_;
    std::thread::id owner_;
};

/// Proof that a shared read lease is held: many of these may coexist, none of
/// them with a write window.
class KB_API utxo_read_lease {
public:
    utxo_read_lease(utxo_read_lease const&) = delete;
    utxo_read_lease& operator=(utxo_read_lease const&) = delete;

    /// Thread-affine for the same reason as the window, and it is not a weaker
    /// case: a lease that authorised elsewhere would let a thread the gate never
    /// counted read the store, which is exactly what the window is meant to
    /// exclude.
    utxo_read_lease(utxo_read_lease&& other)
        : gate_(nullptr), owner_() {
        detail::check_capability_move(other.gate_ != nullptr, other.owner_, "read lease");
        gate_ = std::exchange(other.gate_, nullptr);
        owner_ = std::exchange(other.owner_, std::thread::id{});
    }

    utxo_read_lease& operator=(utxo_read_lease&& other) {
        if (this != &other) {
            detail::check_capability_move(other.gate_ != nullptr, other.owner_, "read lease");
            release();
            gate_ = std::exchange(other.gate_, nullptr);
            owner_ = std::exchange(other.owner_, std::thread::id{});
        }
        return *this;
    }

    /// Not affinity-checked, for the reason given on the window's destructor.
    ~utxo_read_lease() { release(); }

    [[nodiscard]] bool held() const { return gate_ != nullptr; }

    [[nodiscard]] bool authorises(utxo_gate const& gate) const {
        return gate_ == &gate;
    }

    [[nodiscard]] bool on_issuing_thread() const {
        return owner_ == std::this_thread::get_id();
    }

private:
    friend class utxo_gate;
    explicit utxo_read_lease(utxo_gate& gate)
        : gate_(&gate), owner_(std::this_thread::get_id()) {}
    void release();

    utxo_gate* gate_;
    std::thread::id owner_;
};

/// Readers-or-one-writer over the UTXO store, with writer preference.
///
/// @par Why not the node's priority mutex
/// It has no shared mode — it serialises everything one at a time — and it is
/// taken across coroutine suspension points, which is undefined behaviour a
/// reader/writer split would inherit. Nothing here is ever held across a
/// suspension: every store operation this guards is synchronous, and the leases
/// carry a count rather than a lock.
///
/// @par Not a check, a wait
/// A writer does not ask whether readers are present and proceed. It marks
/// itself pending — which closes the door to readers that have not entered yet,
/// immediately — and then waits under the mutex until the reader count reaches
/// zero. There is no instant at which the answer could go stale between the
/// question and the mutation, which is what a "no readers right now" test would
/// have.
/// @par What this does NOT detect: an upgrade
/// Nothing on utxo_read_lease yields a window — there is no upgrade operation to
/// call — so the only way to form one is for a caller to hold a lease and then
/// ask the gate for a window in the same scope. That WOULD self-deadlock, and
/// this gate cannot see it coming: capabilities are values the caller holds, not
/// context the gate can inspect, and reconstructing ownership from thread::id
/// was rejected deliberately when capabilities were chosen over it.
///
/// So the honest statement is: the upgrade is REACHABLE — begin_utxo_write() can
/// be called while a read lease is alive — but it is not supported, and the
/// blocking variant would self-deadlock if anyone did it. What keeps it from
/// happening is that no caller forms the pattern: audited, and every lease in
/// block_chain dies with the single function that took it. try_write_for() below
/// exists as a defensive bound for tests, NOT as upgrade detection.
///
/// @par Thread affinity, and the one case it still does not cover
/// Capabilities are thread-affine and checked: moving one out of its issuing
/// thread throws, and using one from another thread throws, so a window cannot
/// quietly authorise a store access on a thread the gate never admitted (see
/// utxo_affinity_error). Releasing from another thread stays defined, because a
/// destructor cannot refuse.
///
/// What remains: an owner may still park a live capability in shared state, and
/// a second thread that merely HOLDS it — without ever using it — can then ask
/// for a window and wait for one nothing will release. The gate cannot tell that
/// apart from the ordinary case of a thread legitimately waiting for a window
/// another thread holds; the two are identical from here, and separating them
/// needs the ownership tracking that capabilities were chosen over. Both useful
/// shapes of the transfer are refused, this one is not, and no production path
/// forms it: every capability is a `const` local that dies in the function that
/// took it.
class KB_API utxo_gate {
public:
    utxo_gate() = default;

    /// A capability that outlived its gate would release into freed memory, and
    /// the crash would surface far from the ordering mistake that caused it —
    /// in a destructor, during shutdown, with nothing left to point at. Caught
    /// here instead, where the mistake actually is.
    ///
    /// KTH_CONTRACT and not KTH_ASSERT: this must hold in Release, which is the
    /// only build a node runs.
    ~utxo_gate() {
        std::lock_guard<std::mutex> guard(mutex_);
        KTH_CONTRACT(readers_ == 0 && ! writer_active_ && writers_waiting_ == 0);
    }

    utxo_gate(utxo_gate const&) = delete;
    utxo_gate& operator=(utxo_gate const&) = delete;

    /// Admit a reader. Blocks while a writer holds or has asked for the window.
    ///
    /// @par ONE lease per thread, and it is an invariant rather than a style
    /// A thread holding a lease and asking for a SECOND one self-deadlocks if a
    /// writer becomes pending in between: the second request waits for
    /// writers_waiting_ == 0, and the writer waits for readers_ == 0 — which the
    /// first lease is holding above zero. Writer preference is what makes it a
    /// deadlock rather than a delay.
    ///
    /// Not detectable here, for the same reason the read-then-write upgrade is
    /// not: leases are values the caller holds, not context the gate can inspect,
    /// and counting per thread was rejected with the rest of ownership tracking.
    /// What holds it up is the audit — every lease in block_chain dies with the
    /// single function that took it, and nothing nests — plus the bounded
    /// regression in the suite.
    [[nodiscard]]
    std::expected<utxo_read_lease, database::result_code> read() {
        std::unique_lock<std::mutex> guard(mutex_);
        if (poisoned_) {
            return std::unexpected(database::result_code::recovery_required);
        }
        // Refused, not waited on: this thread is holding the window this lease
        // would wait for. Detection only — a thread that does NOT hold the
        // window is never granted anything on the strength of its id.
        if (writer_active_ && owner_ == std::this_thread::get_id()) {
            throw utxo_reentry_error(
                "a read lease was requested by the thread already holding the "
                "exclusive window; reading under the window is not supported");
        }
        // Writer preference, and it is the point rather than a tuning choice: a
        // stream of readers must not be able to starve the deletion that a
        // batch cannot publish without.
        cv_.wait(guard, [this] { return ! writer_active_ && writers_waiting_ == 0; });
        // Asked AGAIN after the wait. The first check refuses a caller that
        // arrives late; this one refuses the caller that was already parked when
        // the latch was published, which is the reader the ordering in
        // end_write() exists to catch.
        if (poisoned_) {
            return std::unexpected(database::result_code::recovery_required);
        }
        ++readers_;
        return utxo_read_lease(*this);
    }

    /// Take the exclusive window. Blocks until every admitted reader has left.
    [[nodiscard]]
    std::expected<utxo_write_window, database::result_code> write() {
        std::unique_lock<std::mutex> guard(mutex_);
        if (poisoned_) {
            return std::unexpected(database::result_code::recovery_required);
        }
        if (writer_active_ && owner_ == std::this_thread::get_id()) {
            throw utxo_reentry_error(
                "the exclusive window was requested by the thread already holding "
                "it; the gate is not recursive");
        }
        detail::pending_writer pending(writers_waiting_, cv_, guard);
        // The only exception this call can propagate is the predicate's, and
        // this predicate reads two members: on this implementation the guard's
        // exceptional path is unreachable from here. It is written to be correct
        // regardless, and tested where it lives rather than through this call.
        cv_.wait(guard, [this] { return ! writer_active_ && readers_ == 0; });
        if (poisoned_) {
            return std::unexpected(database::result_code::recovery_required);
        }
        pending.acquired();
        writer_active_ = true;
        owner_ = std::this_thread::get_id();
        return utxo_write_window(*this);
    }

    /// Take the exclusive window FOR CLOSING, latched or not.
    ///
    /// Waits for ordinary exclusion exactly as write() does — a close that ran
    /// alongside a reader would unmap what that reader is holding — and it is
    /// the poison check, and only that, which it skips. The latch stays set:
    /// this returns permission to shut the store down, never permission to keep
    /// using it.
    [[nodiscard]]
    utxo_close_authority authorise_close() {
        std::unique_lock<std::mutex> guard(mutex_);
        if (writer_active_ && owner_ == std::this_thread::get_id()) {
            throw utxo_reentry_error(
                "a close authority was requested by the thread already holding the "
                "exclusive window; the gate is not recursive");
        }
        detail::pending_writer pending(writers_waiting_, cv_, guard);
        cv_.wait(guard, [this] { return ! writer_active_ && readers_ == 0; });
        pending.acquired();
        writer_active_ = true;
        owner_ = std::this_thread::get_id();
        return utxo_close_authority(*this);
    }

    /// The STORE reported that it has latched; close the gate behind it.
    ///
    /// UTXO-Z latches itself for the same reason this gate does — an operation
    /// it could not finish — and it says so with a code of its own. A boundary
    /// that only translated that code would leave every later caller queueing
    /// for a store that has already stopped answering, one refused call at a
    /// time. This is the read path's way to latch: a lease cannot poison on
    /// release, because a read leaves nothing half-applied, so the fact has to
    /// be published where it is observed.
    ///
    /// Idempotent, and it only ever sets.
    void latch_observed() {
        std::lock_guard<std::mutex> guard(mutex_);
        poisoned_ = true;
    }

    /// Whether the gate has latched. Diagnostics and controls; a caller learns
    /// it by being refused, not by asking first.
    [[nodiscard]] bool poisoned() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return poisoned_;
    }

    /// Take the window, or give up. Returns nullopt rather than waiting past the
    /// deadline.
    ///
    /// Not for production paths — a batch that gave up on its window would have
    /// to decide what to do with a half-applied delta, and #602's answer is that
    /// it must not have one. It exists so a test can assert that an exclusion
    /// defect FAILS rather than hanging: a suite that deadlocks tells an
    /// operator nothing except that CI timed out.
    [[nodiscard]]
    std::optional<utxo_write_window> try_write_for(std::chrono::milliseconds budget) {
        std::unique_lock<std::mutex> guard(mutex_);
        // A latched gate declines rather than waits. `nullopt` is the only
        // refusal this signature has, and it already means "you did not get it".
        if (poisoned_) {
            return std::nullopt;
        }
        // Same refusal as write(), and for the same reason: waiting out the
        // budget would report a programming error as contention, and hand back a
        // nullopt that reads as "someone else had it" when nobody did.
        if (writer_active_ && owner_ == std::this_thread::get_id()) {
            throw utxo_reentry_error(
                "the exclusive window was requested by the thread already holding "
                "it; the gate is not recursive");
        }
        {
            detail::pending_writer pending(writers_waiting_, cv_, guard);
            if ( ! cv_.wait_for(guard, budget,
                    [this] { return ! writer_active_ && readers_ == 0; })) {
                // Restored, released and announced by ~pending_writer, in that
                // order. Giving up in silence would leave every later reader
                // waiting on a count nothing will lower again.
                return std::nullopt;
            }
            // Asked AGAIN, for the same reason read() and write() ask twice: the
            // latch can be published while this caller is parked in wait_for,
            // and a caller that only checked on the way in would be admitted to
            // a store that latched while it waited.
            if (poisoned_) {
                return std::nullopt;
            }
            pending.acquired();
        }
        writer_active_ = true;
        owner_ = std::this_thread::get_id();
        return utxo_write_window(*this);
    }

    /// A reader that gives up rather than waiting, for the same reason as
    /// try_write_for: a test must be able to observe "did not acquire" as a
    /// value instead of hanging.
    [[nodiscard]]
    std::optional<utxo_read_lease> try_read_for(std::chrono::milliseconds budget) {
        std::unique_lock<std::mutex> guard(mutex_);
        if (poisoned_) {
            return std::nullopt;
        }
        if (writer_active_ && owner_ == std::this_thread::get_id()) {
            throw utxo_reentry_error(
                "a read lease was requested by the thread already holding the "
                "exclusive window; reading under the window is not supported");
        }
        auto const got = cv_.wait_for(guard, budget,
            [this] { return ! writer_active_ && writers_waiting_ == 0; });
        if ( ! got) {
            return std::nullopt;
        }
        if (poisoned_) {
            return std::nullopt;
        }
        ++readers_;
        return utxo_read_lease(*this);
    }

    /// Diagnostics for tests. Never a basis for a decision: a count read outside
    /// the mutex is a fact about the past.
    [[nodiscard]] size_t readers() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return readers_;
    }

    /// How many writers are waiting for the window. Diagnostics only, and the
    /// same caveat as readers(): a count read outside the mutex is a fact about
    /// the past. A test uses it to know a writer has actually become pending
    /// instead of sleeping and hoping.
    [[nodiscard]] size_t writers_waiting() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return writers_waiting_;
    }

    [[nodiscard]] bool writing() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return writer_active_;
    }

    /// Whether THIS thread is the one holding the window.
    ///
    /// For refusing, never for granting: it is what lets a lock-order control
    /// say "you are under the window, do not take that mutex here" at the site
    /// rather than leaving it to be found as a hang. Unlike `writing()`, the
    /// answer is about the caller, so it cannot be misread as permission —
    /// with_read/with_write still ask the capability and never the thread.
    [[nodiscard]] bool held_by_current_thread() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return writer_active_ && owner_ == std::this_thread::get_id();
    }

private:
    friend class utxo_write_window;
    friend class utxo_read_lease;
    friend class utxo_close_authority;

    void end_read() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            --readers_;
        }
        cv_.notify_all();
    }

    /// @param poison Whether the operation left through an unsafe exit.
    ///
    /// The ORDER inside the critical section is the whole of the guarantee. The
    /// latch is published FIRST, then the writer is stood down, and only after
    /// the mutex is released is anyone woken. So there is no instant at which
    /// the door is open and the latch is not yet set: a reader that wakes has
    /// already lost, and a reader that has not entered yet is refused before it
    /// takes the lock. Poisoning in a separate call after end_write() would
    /// leave exactly that gap.
    void end_write(bool poison) {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (poison) {
                poisoned_ = true;
            }
            writer_active_ = false;
            // Cleared on every path, an exception included: the destructor runs
            // during unwinding, so a stale owner cannot outlive a failed
            // operation and refuse the next one.
            owner_ = std::thread::id{};
        }
        cv_.notify_all();
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    size_t readers_{0};
    size_t writers_waiting_{0};
    bool writer_active_{false};

    /// Set once, never cleared. There is no unpoison(), and close() does not
    /// clear it: what the latch says is that an operation which may have applied
    /// part of its work did not finish, and no later call can make that untrue.
    /// The only thing that resolves it is a restart, which consults the
    /// transition record — a decision this object is deliberately not able to
    /// make on its own.
    bool poisoned_{false};

    /// Whoever holds the window, for DETECTION only. Never read to authorise an
    /// access: with_read/with_write ask the capability, never the thread.
    std::thread::id owner_{};
};

inline void utxo_write_window::release() {
    if (gate_ != nullptr) {
        // The verdict travels WITH the release, so the gate can publish it in
        // the same critical section that opens the door. Deciding it here and
        // poisoning in a second call would leave a window between them in which
        // a reader is admitted to a store nobody has declared unusable yet.
        gate_->end_write(would_poison());
        gate_ = nullptr;
        mutation_may_have_started_ = false;
        ever_mutated_ = false;
        completed_ = false;
    }
}

inline void utxo_close_authority::release() {
    if (gate_ != nullptr) {
        // Never poisons: closing is a wind-down, not an operation that could
        // have left the set half-written. Never CLEARS it either — end_write
        // only ever sets.
        gate_->end_write(false);
        gate_ = nullptr;
    }
}

inline void utxo_read_lease::release() {
    if (gate_ != nullptr) {
        gate_->end_read();
        gate_ = nullptr;
    }
}



/// A callback whose result cannot carry the store out of the scope that
/// authorised reaching it. void and owned values pass; references and pointers
/// do not — expressed as a constraint rather than an assertion in the body, so
/// the ill-formed shape is DETECTABLE rather than merely fatal.
template <typename F, typename Arg>
concept escaping_free_result =
    std::invocable<F, Arg> &&
    ! std::is_reference_v<std::invoke_result_t<F, Arg>> &&
    ! std::is_pointer_v<std::invoke_result_t<F, Arg>>;

/// A value reachable only under proof that the gate authorises the access.
///
/// This is what makes the exclusion structural rather than a habit. There is no
/// operator->, no getter and no way to name the value: access is handed to a
/// callback for the duration of the call, so a method of block_chain that
/// reaches for the store without a capability does not compile — including one
/// written years from now by someone who never read this file.
///
/// @par Why a callback and not a reference
/// A `T& under(capability)` would let the reference outlive the capability that
/// justified it, which is exactly the guarantee being sold. Scoping the access
/// to a call removes the ordinary way to do that by accident. A callback that
/// deliberately stashes the reference can still escape it — no C++ API prevents
/// that without a lifetime annotation — so this is a barrier against the
/// mistake, not against the intent, and the difference is worth stating.
template <typename T>
class guarded_store {
public:
    explicit guarded_store(utxo_gate& gate) : gate_(&gate) {}

    guarded_store(guarded_store const&) = delete;
    guarded_store& operator=(guarded_store const&) = delete;

    /// Mutating access, for the operation holding the exclusive window.
    ///
    /// The callback may return void or a value of its own. It may NOT return a
    /// reference or a pointer: that is the accidental way the store escapes the
    /// scope that justified reaching it, and it is the one worth closing. A
    /// callback determined to stash the reference still can — see the note above
    /// — but nothing here hands it out by return.
    template <typename F>
        requires escaping_free_result<F, T&>
    decltype(auto) with_write(utxo_write_window const& window, F&& callback) {
        authorise(window, "write");
        return std::forward<F>(callback)(value_);
    }

    /// Reading access under a shared lease.
    template <typename F>
        requires escaping_free_result<F, T const&>
    decltype(auto) with_read(utxo_read_lease const& lease, F&& callback) const {
        authorise(lease, "read");
        return std::forward<F>(callback)(value_);
    }

    /// Closing access, authorised by the administrative capability.
    ///
    /// The ONLY overload that takes one. Separate from with_write so that the
    /// capability which survives a latched gate cannot be handed to a mutation:
    /// the type system refuses it rather than a review having to notice.
    template <typename F>
        requires escaping_free_result<F, T&>
    decltype(auto) with_close(utxo_close_authority const& authority, F&& callback) {
        authorise(authority, "close");
        return std::forward<F>(callback)(value_);
    }

    /// Reading access from INSIDE the exclusive window, authorised by the
    /// capability the operation already holds rather than by which thread is
    /// asking. Undo capture reads the set in the middle of a connect batch, and
    /// a lease there would wait on the window its own operation is holding.
    template <typename F>
        requires escaping_free_result<F, T const&>
    decltype(auto) with_read(utxo_write_window const& window, F&& callback) const {
        authorise(window, "read under the write window");
        return std::forward<F>(callback)(value_);
    }

private:
    /// Two independent questions, asked in this order and reported apart because
    /// they are different mistakes: the capability must name THIS gate, and the
    /// thread asking must be the one the gate issued it to. Naming the right gate
    /// first matters — a released or moved-from capability has no meaningful
    /// issuing thread, and reporting affinity for it would misname the defect.
    template <typename Capability>
    void authorise(Capability const& capability, char const* what) const {
        if ( ! capability.authorises(*gate_)) {
            throw utxo_capability_error(
                std::string("the capability offered for ") + what +
                " was released, default-constructed, or issued by another gate");
        }
        if ( ! capability.on_issuing_thread()) {
            throw utxo_affinity_error(
                std::string("the capability offered for ") + what +
                " was taken by another thread; it authorises nothing here");
        }
    }

    utxo_gate* gate_;
    T value_;
};

} // namespace kth::blockchain

#endif
