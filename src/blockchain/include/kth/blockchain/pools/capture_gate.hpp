// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_CAPTURE_GATE_HPP
#define KTH_BLOCKCHAIN_POOLS_CAPTURE_GATE_HPP

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>

#include <kth/blockchain/define.hpp>

namespace kth::blockchain {

/// Keeps captures and transitions apart, which a flag cannot do.
///
/// A template is built from a chain view and a copy of the mempool. Asking
/// whether a transition is running and then capturing are two moments, and a
/// transition that begins between them mutates the stores the capture is about
/// to read. What joins them is entry: a capture is admitted or refused, and a
/// transition cannot start mutating until every admitted capture has finished.
///
/// A capture that was admitted before the close completes on what it took —
/// its copies are private by then, so nothing a transition does can reach them.
/// What the close stops is the next one.
///
/// Reopening is earned, not unwound. `end_transition` is called after the
/// coherent state has been published, and only then; a scope guard that reopened
/// on the way out would reopen after a failure too, which is the one case where
/// the gate must stay shut while the node winds down.
struct KB_API capture_gate {

    /// Admits a capture, or refuses because a transition holds the gate.
    /// Balanced by leave_capture; use captured_lease rather than pairing by hand.
    [[nodiscard]]
    bool try_enter_capture();

    void leave_capture();

    /// Closes entry and waits for every admitted capture to finish. Returns
    /// false if a transition is already running — two at once is not a state
    /// this has an answer for, and sharing a flag would let the first to finish
    /// reopen for both.
    [[nodiscard]]
    bool begin_transition();

    /// Reopens. Only after the transition's state has been published.
    void end_transition();

    [[nodiscard]]
    bool transition_in_progress() const;

    /// Diagnostic only.
    [[nodiscard]]
    size_t captures_in_flight() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable drained_;
    bool open_{true};
    bool in_transition_{false};
    size_t capturing_{0};
};

/// An admitted capture. Falsey when the gate refused.
///
/// Deliberately not held across the build: everything after the copy runs on
/// private data, and holding it there would make a transition wait for work
/// that cannot be affected by it.
struct KB_API captured_lease {
    explicit captured_lease(capture_gate& gate)
        : gate_(gate.try_enter_capture() ? &gate : nullptr)
    {}

    ~captured_lease() { release(); }

    captured_lease(captured_lease const&) = delete;
    captured_lease& operator=(captured_lease const&) = delete;

    [[nodiscard]]
    explicit operator bool() const { return gate_ != nullptr; }

    /// Leaves early, so the build that follows runs outside the gate.
    void release() {
        if (gate_ != nullptr) {
            gate_->leave_capture();
            gate_ = nullptr;
        }
    }

private:
    capture_gate* gate_;
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_POOLS_CAPTURE_GATE_HPP
