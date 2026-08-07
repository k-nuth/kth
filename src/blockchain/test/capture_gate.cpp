// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/blockchain/pools/capture_gate.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace kth;
using kth::blockchain::capture_gate;
using kth::blockchain::captured_lease;

// =============================================================================
// Keeping captures and transitions apart (#621)
// =============================================================================
//
// A flag read on entry cannot do this: asking whether a transition is running
// and then capturing are two moments, and a transition that begins between them
// mutates the stores the capture is about to read. What joins them is that entry
// is admitted or refused, and that a transition cannot start mutating until
// every admitted capture has finished.

TEST_CASE("a capture is admitted while nothing is transitioning", "[capture_gate]") {
    capture_gate gate;

    captured_lease const lease(gate);
    CHECK(bool(lease));
    CHECK(gate.captures_in_flight() == 1u);
    CHECK_FALSE(gate.transition_in_progress());
}

TEST_CASE("a capture is refused while a transition holds the gate", "[capture_gate]") {
    capture_gate gate;
    REQUIRE(gate.begin_transition());

    captured_lease const lease(gate);
    CHECK_FALSE(bool(lease));
    CHECK(gate.transition_in_progress());

    gate.end_transition();
    captured_lease const after(gate);
    CHECK(bool(after));
}

TEST_CASE("a transition waits for a capture that was already admitted", "[capture_gate]") {
    // The crossing the whole design is about, provoked rather than observed: a
    // capture is admitted and parked, a transition tries to begin, and the
    // transition must not be able to proceed until that capture is done.
    capture_gate gate;

    captured_lease lease(gate);
    REQUIRE(bool(lease));

    std::atomic<bool> began{false};
    std::atomic<bool> started{false};

    // Catch2's macros are not thread-safe: a failure inside a spawned thread
    // throws out of it and terminates, and the handler mutates run state the
    // main thread is also writing. The thread records, the main thread asserts.
    std::atomic<bool> begin_ok{false};

    std::thread transition([&] {
        started = true;
        begin_ok = gate.begin_transition();
        began = true;
    });

    // Give it every chance to finish early. If begin_transition returned while a
    // capture was still in flight, this is where it would show.
    while ( ! started) std::this_thread::yield();
    for (int i = 0; i < 200 && ! began; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_FALSE(began.load());
    CHECK(gate.captures_in_flight() == 1u);

    // The capture finishes; only now may the transition proceed.
    lease.release();
    transition.join();
    CHECK(begin_ok.load());
    CHECK(began.load());
    CHECK(gate.captures_in_flight() == 0u);

    gate.end_transition();
}

TEST_CASE("a capture admitted before the close is not interrupted by it", "[capture_gate]") {
    // The other half: closing entry stops the next capture, not the one that is
    // already holding copies. Nothing revokes an admitted lease.
    capture_gate gate;

    captured_lease lease(gate);
    REQUIRE(bool(lease));

    std::atomic<bool> begin_ok{false};
    std::thread transition([&] { begin_ok = gate.begin_transition(); });

    // Still admitted while the transition waits.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(gate.captures_in_flight() == 1u);

    lease.release();
    transition.join();
    CHECK(begin_ok.load());
    gate.end_transition();
}

TEST_CASE("a second transition is refused rather than nested", "[capture_gate]") {
    // Sharing a flag would let the first to finish reopen for both.
    capture_gate gate;
    REQUIRE(gate.begin_transition());
    CHECK_FALSE(gate.begin_transition());

    gate.end_transition();
    CHECK(gate.begin_transition());
    gate.end_transition();
}

TEST_CASE("the gate stays closed until a transition ends", "[capture_gate]") {
    // Reopening is earned. There is no path that reopens on the way out of a
    // scope, because a failure inside the window must leave it shut.
    capture_gate gate;
    REQUIRE(gate.begin_transition());

    for (int i = 0; i < 5; ++i) {
        captured_lease const refused(gate);
        CHECK_FALSE(bool(refused));
    }

    gate.end_transition();
    captured_lease const admitted(gate);
    CHECK(bool(admitted));
}

TEST_CASE("many captures drain before a transition begins", "[capture_gate]") {
    capture_gate gate;

    constexpr int readers = 8;
    std::atomic<int> admitted{0};
    std::atomic<bool> release{false};
    std::vector<std::thread> threads;

    for (int i = 0; i < readers; ++i) {
        threads.emplace_back([&] {
            captured_lease lease(gate);
            if (lease) {
                ++admitted;
                while ( ! release) std::this_thread::yield();
            }
        });
    }

    while (admitted < readers) std::this_thread::yield();
    CHECK(gate.captures_in_flight() == size_t(readers));

    std::atomic<bool> began{false};
    std::atomic<bool> begin_ok{false};
    std::thread transition([&] {
        begin_ok = gate.begin_transition();
        began = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(began.load());

    release = true;
    for (auto& t : threads) t.join();
    transition.join();

    CHECK(begin_ok.load());
    CHECK(began.load());
    CHECK(gate.captures_in_flight() == 0u);
    gate.end_transition();
}
