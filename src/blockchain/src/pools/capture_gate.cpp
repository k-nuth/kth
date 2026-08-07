// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/capture_gate.hpp>

#include <spdlog/spdlog.h>

namespace kth::blockchain {

bool capture_gate::try_enter_capture() {
    std::lock_guard<std::mutex> lock(mutex_);
    if ( ! open_) {
        return false;
    }
    ++capturing_;
    return true;
}

void capture_gate::leave_capture() {
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        --capturing_;
        notify = (capturing_ == 0);
    }
    // Outside the lock: the waiter has to take it to observe the count.
    if (notify) {
        drained_.notify_all();
    }
}

bool capture_gate::begin_transition() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (in_transition_) {
        spdlog::critical("[blockchain] A transition began while another was already running");
        return false;
    }

    in_transition_ = true;
    open_ = false;

    // Captures admitted before the close finish on copies they already hold.
    // Waiting for them is what makes "no capture is reading these stores" true
    // rather than merely likely.
    drained_.wait(lock, [this] { return capturing_ == 0; });
    return true;
}

void capture_gate::end_transition() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if ( ! in_transition_) {
            spdlog::critical("[blockchain] A transition ended that had not begun");
            return;
        }
        in_transition_ = false;
        open_ = true;
    }
    // Nothing waits on the gate opening — captures are refused, not queued, so
    // the caller retries rather than blocking on a transition of unknown length.
}

bool capture_gate::transition_in_progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return in_transition_;
}

size_t capture_gate::captures_in_flight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capturing_;
}

} // namespace kth::blockchain
