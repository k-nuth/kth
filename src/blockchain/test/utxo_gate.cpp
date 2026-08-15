// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <kth/blockchain/utxo_gate.hpp>

using namespace kth;
using namespace kth::blockchain;

// =============================================================================
// The exclusion window UTXO-Z 0.10 requires for apply_deletes() (#649)
// =============================================================================
//
// apply_deletes() erases from the active containers and writes through the file
// cache's mappings, so it needs a window with no find(), resolve(), insert(),
// compaction or close() in flight. The library's own lock covers
// resolve-vs-resolve and does not extend here.
//
// These tests are about the gate itself: that a writer cannot start under a
// reader, that a reader cannot slip in once the window is asked for, that the
// door reopens, and — the one that matters most — that the window spans the
// whole logical mutation rather than each call, because a reader admitted
// between the inserts and the deletions sees a set holding outputs the blocks
// spent, and the transition record does not protect a reader that never
// consults it.

// Everything test-only in this file has internal linkage. `fake_store` and
// `watchdog` are exactly the names another test translation unit in the same
// binary is likely to define, and two differing definitions of either would be
// an ODR violation the linker is free to resolve silently and wrongly.
namespace {

constexpr auto settle = std::chrono::milliseconds(120);

// A stand-in for the store, so the capability rules can be tested without one.
struct fake_store {
    int touched = 0;
};

} // namespace

TEST_CASE("a capability only opens the store of the gate that issued it",
          "[utxo][gate]") {
    // Two gates, two stores. A window over one proves exclusion over the other's
    // readers of nothing at all, so offering it must be refused rather than
    // quietly accepted — the whole guarantee is that holding a token means the
    // readers of THIS store are out.
    utxo_gate mine;
    utxo_gate other;
    guarded_store<fake_store> store(mine);

    auto foreign = other.write().value();
    CHECK_THROWS_AS(store.with_write(foreign, [](auto& s) { ++s.touched; }),
                    utxo_capability_error);

    // And the right one works, so the rejection is not "everything is refused".
    auto ours = mine.write().value();
    CHECK_NOTHROW(store.with_write(ours, [](auto& s) { ++s.touched; }));
}

TEST_CASE("a released or moved-from capability authorises nothing",
          "[utxo][gate]") {
    utxo_gate gate;
    guarded_store<fake_store> store(gate);

    SECTION("moved-from") {
        auto window = gate.write().value();
        auto moved = std::move(window);
        // `window` is spent: it no longer names the gate, so it proves nothing.
        CHECK_THROWS_AS(store.with_write(window, [](auto& s) { ++s.touched; }),
                        utxo_capability_error);
        CHECK_NOTHROW(store.with_write(moved, [](auto& s) { ++s.touched; }));
    }

    SECTION("released") {
        utxo_write_window spent = gate.write().value();
        {
            auto taking = std::move(spent);   // released at the end of this scope
        }
        CHECK_THROWS_AS(store.with_write(spent, [](auto& s) { ++s.touched; }),
                        utxo_capability_error);
    }

    SECTION("a lease that has gone out of scope cannot be reused") {
        utxo_read_lease spent = gate.read().value();
        {
            auto taking = std::move(spent);
        }
        CHECK_THROWS_AS(store.with_read(spent, [](auto const& s) { return s.touched; }),
                        utxo_capability_error);
    }
}

namespace {

// A requires-expression over a CONCRETE type is a hard error rather than a
// substitution failure, so each impossibility is asked through a template.
template <typename G>
concept writes_without_capability = requires(G& g) { g.with_write([](auto&) {}); };
template <typename G>
concept has_getter = requires(G& g) { g.get(); };
template <typename G>
concept has_arrow = requires(G& g) { g.operator->(); };
template <typename G, typename L>
concept writes_under_read_lease = requires(G& g, L const& l) {
    g.with_write(l, [](auto&) {});
};

template <typename G, typename Cap, typename F>
concept accepts_callback = requires(G& g, Cap const& c, F f) { g.with_write(c, f); };
template <typename G, typename Cap, typename F>
concept accepts_read_callback = requires(G const& g, Cap const& c, F f) { g.with_read(c, f); };

} // namespace

TEST_CASE("the callback may not hand the store back out", "[utxo][gate]") {
    // Returning a reference or a pointer is the ACCIDENTAL way the store leaves
    // the scope that authorised reaching it — someone writes `return s.field;`
    // against a `decltype(auto)` and the reference outlives the capability. That
    // shape is refused. A callback determined to stash the reference in a
    // capture still can, and the header says so rather than pretending
    // otherwise.
    using store_t = guarded_store<fake_store>;

    // POSITIVE: void and an owned value are the shapes callers need.
    static_assert(accepts_callback<store_t, utxo_write_window, void(*)(fake_store&)>,
        "a void callback is refused");
    static_assert(accepts_callback<store_t, utxo_write_window, int(*)(fake_store&)>,
        "a callback returning a value is refused");
    static_assert(accepts_read_callback<store_t, utxo_read_lease, int(*)(fake_store const&)>,
        "a reading callback returning a value is refused");

    // NEGATIVE: every way of handing the store itself back.
    static_assert( ! accepts_callback<store_t, utxo_write_window, fake_store&(*)(fake_store&)>,
        "a callback may return a mutable reference to the store");
    static_assert( ! accepts_callback<store_t, utxo_write_window,
                                      fake_store const&(*)(fake_store&)>,
        "a callback may return a const reference to the store");
    static_assert( ! accepts_callback<store_t, utxo_write_window, fake_store*(*)(fake_store&)>,
        "a callback may return a pointer to the store");
    static_assert( ! accepts_callback<store_t, utxo_write_window,
                                      fake_store const*(*)(fake_store&)>,
        "a callback may return a const pointer to the store");

    // And the same on the reading side, which is the one called most.
    static_assert( ! accepts_read_callback<store_t, utxo_read_lease,
                                           fake_store const&(*)(fake_store const&)>,
        "a reading callback may return a reference to the store");
    static_assert( ! accepts_read_callback<store_t, utxo_read_lease,
                                           fake_store const*(*)(fake_store const&)>,
        "a reading callback may return a pointer to the store");

    // A value really does come back, so the constraint is not just refusing.
    utxo_gate gate;
    store_t store(gate);
    auto window = gate.write().value();
    store.with_write(window, [](fake_store& s) { s.touched = 7; });
    auto const seen = store.with_read(window, [](fake_store const& s) { return s.touched; });
    CHECK(seen == 7);
}

TEST_CASE("the store cannot be reached without a capability", "[utxo][gate]") {
    // The structural half, checked by the type system rather than by review.
    using store_t = guarded_store<fake_store>;

    // A mutation without a token: no overload takes only a callback.
    static_assert( ! writes_without_capability<store_t>,
        "guarded_store accepts a write without a capability");

    // A direct access: no getter, no operator->.
    static_assert( ! has_getter<store_t>,
        "guarded_store grew a getter; the store is reachable without a capability");
    static_assert( ! has_arrow<store_t>,
        "guarded_store grew operator->; the store is reachable without a capability");

    // A read lease does not authorise a mutation.
    static_assert( ! writes_under_read_lease<store_t, utxo_read_lease>,
        "a read lease authorises a mutation");

    // POSITIVE: the authorised shapes DO compile, so the negations above are
    // not passing because every call is ill-formed.
    static_assert(requires(store_t& g, utxo_write_window const& w) {
        g.with_write(w, [](auto&) {});
    }, "the authorised write does not compile");
    static_assert(requires(store_t const& g, utxo_read_lease const& l) {
        g.with_read(l, [](auto const&) {});
    }, "the authorised read does not compile");
    static_assert(requires(store_t const& g, utxo_write_window const& w) {
        g.with_read(w, [](auto const&) {});
    }, "the authorised read from inside the window does not compile");
}

// Every concurrent test here runs under this. An exclusion defect must produce a
// NAMED failure, not a suite that hangs until CI's global timeout — which tells
// an operator nothing except that something, somewhere, stopped.
namespace {

struct watchdog {
    std::atomic<bool> done{false};
    std::thread barker;

    watchdog(std::chrono::milliseconds budget, std::string what)
        : barker([this, budget, what = std::move(what)] {
              auto const deadline = std::chrono::steady_clock::now() + budget;
              while ( ! done.load()) {
                  if (std::chrono::steady_clock::now() > deadline) {
                      // Abort rather than let the suite wedge: the stack of every
                      // thread is in the core, which is the diagnosis.
                      std::fprintf(stderr,
                          "\n[watchdog] %s did not finish within its budget: "
                          "treating it as an exclusion defect\n", what.c_str());
                      std::abort();
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(10));
              }
          }) {}

    ~watchdog() {
        done.store(true);
        barker.join();
    }
};

constexpr auto watchdog_budget = std::chrono::seconds(20);

} // namespace

TEST_CASE("asking for the window while holding a lease does not acquire it",
          "[utxo][gate]") {
    // NOT a claim that an upgrade is detected — it is not, and the header says
    // so. The pattern is REACHABLE: begin_utxo_write().value() can be called while a
    // lease is alive. It is simply not supported, and the blocking variant would
    // self-deadlock. What keeps it from happening is that no caller forms it,
    // which is audited rather than enforced.
    //
    // What this pins is the defensive bound: the timed acquisition returns a
    // VALUE the test can check instead of parking a thread, and the gate stays
    // usable afterwards. The blocking write() in the same position WOULD wedge,
    // which is why no production path may take a lease and then a window.
    watchdog guard(watchdog_budget, "the upgrade attempt");
    utxo_gate gate;

    auto lease = gate.read().value();
    REQUIRE(gate.readers() == 1);

    auto const started = std::chrono::steady_clock::now();
    auto refused = gate.try_write_for(std::chrono::milliseconds(150));
    auto const waited = std::chrono::steady_clock::now() - started;

    // Expired rather than acquired, and it came back — the distinction this
    // test can actually make. It is a bounded timeout, not a detected upgrade.
    CHECK_FALSE(refused.has_value());
    // It really did wait its budget, with a tolerance for timer resolution:
    // wait_for measures against the condition variable's own clock, which may
    // return a tick or two before steady_clock agrees, and a test that fails on
    // that is measuring the platform rather than the gate. The claim being made
    // is "it waited", not "it waited to the millisecond".
    CHECK(waited >= std::chrono::milliseconds(140));
    CHECK(waited < std::chrono::seconds(5));           // and it really did return

    // POSITIVE, and the reason this test is not just an assertion about failing:
    // the gate stays usable afterwards. A refusal that left writers_waiting_
    // raised would keep every later reader out.
    { auto releasing = std::move(lease); }

    auto after = gate.try_write_for(std::chrono::seconds(2));
    REQUIRE(after.has_value());
    CHECK(after->held());
    { auto closing = std::move(*after); }

    std::atomic<bool> entered{false};
    std::thread reader([&] {
        auto l = gate.read().value();
        entered.store(true);
    });
    reader.join();
    CHECK(entered.load());
}

TEST_CASE("a second lease on one thread is not safe once a writer is pending",
          "[utxo][gate]") {
    // The invariant the header states, pinned rather than trusted. A thread
    // holding a lease and asking for a SECOND one is fine while nothing else
    // wants the gate — and self-deadlocks the moment a writer becomes pending:
    // the second request waits for writers_waiting_ == 0, and the writer waits
    // for readers_ == 0, which the FIRST lease is holding above zero.
    //
    // The gate cannot detect it — leases are values the caller holds, not
    // context the gate can inspect — so this bounds it instead of hanging, and
    // the timed form is what makes the verdict a value.
    watchdog guard(watchdog_budget, "the nested lease with a writer pending");
    utxo_gate gate;

    auto first = gate.read().value();   // not const: released explicitly at the end
    REQUIRE(gate.readers() == 1);

    // Nesting with nobody waiting: allowed, and it must stay allowed, or this
    // test would be measuring a gate that simply refuses everything.
    {
        auto const nested = gate.read().value();
        CHECK(gate.readers() == 2);
    }
    REQUIRE(gate.readers() == 1);

    // Now a writer becomes pending. It cannot get in — `first` is still held —
    // but writer preference closes the door behind it.
    std::atomic<bool> writer_in{false};
    std::thread writer([&] {
        auto const window = gate.write().value();
        writer_in.store(true);
    });
    while (gate.writers_waiting() == 0) {
        std::this_thread::yield();
    }
    CHECK_FALSE(writer_in.load());

    // THE PROPERTY: the second lease is refused now. The blocking read() here
    // would wait for a writer that is waiting for this very thread.
    auto const nested = gate.try_read_for(std::chrono::milliseconds(150));
    CHECK_FALSE(nested.has_value());
    CHECK_FALSE(writer_in.load());   // and the writer is still shut out by `first`

    // Releasing the first lease lets the writer through, so the refusal above
    // was the nesting and not a gate that had stopped working.
    { auto const releasing = std::move(first); }
    writer.join();
    CHECK(writer_in.load());
    CHECK_FALSE(gate.writing());
}

TEST_CASE("the timed variants refuse reentry instead of waiting it out",
          "[utxo][gate]") {
    // The blocking pair refuses reentry; the timed pair used to wait the whole
    // budget and hand back a nullopt, which reads as "another thread had it"
    // when nobody did — a programming error reported as contention, and the
    // longer the budget the longer the lie takes to arrive.
    watchdog guard(watchdog_budget, "reentry through the timed variants");
    utxo_gate gate;

    auto const window = gate.write().value();

    // A budget long enough that waiting it out would be unmistakable in the
    // elapsed time below.
    auto const before = std::chrono::steady_clock::now();
    CHECK_THROWS_AS(gate.try_write_for(std::chrono::seconds(3)), utxo_reentry_error);
    CHECK_THROWS_AS(gate.try_read_for(std::chrono::seconds(3)), utxo_reentry_error);
    auto const elapsed = std::chrono::steady_clock::now() - before;

    // Refused, not waited out: neither call spent its budget.
    CHECK(elapsed < std::chrono::milliseconds(500));

    // And the refusals left the gate intact — the window still holds, and the
    // pending-writer count was restored on the way out rather than stranded,
    // which a later reader would be the one to pay for.
    CHECK(window.held());
    CHECK(gate.writing());
}

// =============================================================================
// Giving up on the window: two DIFFERENT paths, and one does not evidence the
// other
// =============================================================================
//
// Writer preference means readers wait for the pending count to reach zero, so a
// writer that stops waiting owes the count back AND a notification: a reader
// already parked in cv.wait() re-evaluates its predicate only when notified.
//
// A timed-out wait_for is an ORDINARY RETURN. It proves the count is restored
// and that the sequence works, and it proves nothing whatsoever about what
// happens when the wait is left by an exception, where the notification is the
// part that can be forgotten. The two are tested apart, below.

TEST_CASE("a writer that times out restores the count and wakes the readers",
          "[utxo][gate]") {
    // THE NORMAL RETURN. wait_for exhausting its budget is not an error path.
    watchdog guard(watchdog_budget, "the pending-writer count after a timeout");
    utxo_gate gate;

    // Parked BEFORE the writer gives up, so what admits it is the notification
    // the giving-up path issues — not a fresh evaluation that happened to run
    // after the count was already down.
    std::atomic<bool> reader_at_gate{false};
    std::atomic<bool> admitted{false};
    std::thread reader;

    {
        auto const lease = gate.read().value();

        reader = std::thread([&] {
            reader_at_gate.store(true);
            auto const l = gate.read().value();     // waits: a writer is pending
            admitted.store(true);
        });
        while ( ! reader_at_gate.load()) {
            std::this_thread::yield();
        }

        auto const refused = gate.try_write_for(std::chrono::milliseconds(60));
        CHECK_FALSE(refused.has_value());
    }

    // The proof is a reader getting in, not a counter being inspected.
    reader.join();
    CHECK(admitted.load());
    CHECK(gate.readers() == 0);
}

TEST_CASE("a writer that unwinds restores the count and wakes the readers",
          "[utxo][gate]") {
    // THE EXCEPTIONAL PATH, exercised where it lives.
    //
    // The gate's own wait cannot reach it: `wait(lock, pred)` is `while (!pred())
    // wait(lock);`, so the only exception it can propagate is the predicate's,
    // and the gate's predicate reads two members and cannot throw. There is no
    // honest seam in utxo_gate — hence the extraction, and hence this test drives
    // blockchain::detail::pending_writer directly rather than dressing a timeout
    // up as an exception.
    //
    // The sequence is the one libstdc++ produces: the mutex is re-acquired
    // before the exception propagates, so the guard is destroyed holding it.
    watchdog guard(watchdog_budget, "the pending-writer count after an unwind");

    std::mutex mutex;
    std::condition_variable cv;
    size_t writers_waiting = 0;

    std::atomic<bool> release_writer{false};
    std::atomic<bool> unwound{false};
    std::atomic<bool> reader_at_gate{false};
    std::atomic<bool> admitted{false};

    std::thread writer([&] {
        try {
            std::unique_lock<std::mutex> lock(mutex);
            blockchain::detail::pending_writer pending(writers_waiting, cv, lock);

            // Releases the mutex the way cv.wait would, parks on something this
            // gate's cv cannot wake — so the reader below is admitted by the
            // guard's notification and by nothing else — and re-acquires before
            // throwing, which is what the library does before propagating.
            lock.unlock();
            while ( ! release_writer.load()) {
                std::this_thread::yield();
            }
            lock.lock();
            throw std::runtime_error("the wait ended badly");
        } catch (std::runtime_error const&) {
            unwound.store(true);
        }
    });

    std::thread reader([&] {
        std::unique_lock<std::mutex> lock(mutex);
        reader_at_gate.store(true);
        cv.wait(lock, [&] { return writers_waiting == 0; });
        admitted.store(true);
    });

    // Parked, provably: the flag is set while the reader holds the mutex, so
    // acquiring it here is only possible once cv.wait has released it. Without
    // this the reader might evaluate its predicate after the decrement and pass
    // without the notification ever mattering.
    while ( ! reader_at_gate.load()) {
        std::this_thread::yield();
    }
    { std::lock_guard<std::mutex> parked(mutex); }

    release_writer.store(true);

    writer.join();
    reader.join();

    CHECK(unwound.load());
    CHECK(admitted.load());          // woken by the guard, on the way out
    CHECK(writers_waiting == 0u);    // and the count came back
}

TEST_CASE("reentering the window from the same thread fails immediately",
          "[utxo][gate]") {
    // Scope discipline alone proved insufficient: three accidental reentries in
    // one change, one of them in production. This turns the resulting hang into
    // a named error at the site that caused it.
    //
    // DETECTION, never authorisation: the recorded id only ever refuses.
    watchdog guard(watchdog_budget, "the reentry detection");
    utxo_gate gate;

    auto window = gate.write().value();

    // 1 & 2 — both refused, and immediately: no budget, no waiting.
    auto const before = std::chrono::steady_clock::now();
    CHECK_THROWS_AS(gate.write().value(), utxo_reentry_error);
    CHECK_THROWS_AS(gate.read().value(), utxo_reentry_error);
    auto const elapsed = std::chrono::steady_clock::now() - before;
    CHECK(elapsed < std::chrono::milliseconds(200));

    // 3 — the original window survived both refusals and still authorises.
    CHECK(window.held());

    // 6 — moving within the same thread still works.
    auto moved = std::move(window);
    CHECK(moved.held());
    CHECK_THROWS_AS(gate.write().value(), utxo_reentry_error);   // still owned

    // 4 — another thread WAITS rather than being refused: legitimate contention
    // must not have been turned into an error.
    std::atomic<bool> other_entered{false};
    std::thread other([&] {
        auto w = gate.write().value();
        other_entered.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK_FALSE(other_entered.load());

    // 3 (second half) — releasing reopens the gate for everyone.
    { auto closing = std::move(moved); }
    other.join();
    CHECK(other_entered.load());
    CHECK_FALSE(gate.writing());

    std::atomic<bool> reader_in{false};
    std::thread reader([&] {
        auto l = gate.read().value();
        reader_in.store(true);
    });
    reader.join();
    CHECK(reader_in.load());
}

TEST_CASE("an exception inside the window clears the owner", "[utxo][gate]") {
    // 5 — a stale owner would refuse the NEXT operation on this thread, turning
    // one failure into a node that cannot write again.
    watchdog guard(watchdog_budget, "the owner after an exception");
    utxo_gate gate;

    try {
        auto window = gate.write().value();
        throw std::runtime_error("the operation failed");
    } catch (std::runtime_error const&) {
    }

    // The same thread can take it again, which it could not if the owner had
    // survived the unwinding.
    auto again = gate.write().value();
    CHECK(again.held());
    { auto closing = std::move(again); }

    // And a read on this thread is no longer refused either.
    auto lease = gate.read().value();
    CHECK(lease.held());
}

TEST_CASE("a writer does not start while a reader is admitted", "[utxo][gate]") {
    utxo_gate gate;
    watchdog guard(watchdog_budget, "the writer waiting on a reader");
    std::atomic<bool> writing{false};

    std::thread writer;
    {
        auto lease = gate.read().value();
        REQUIRE(gate.readers() == 1);

        writer = std::thread([&] {
            auto window = gate.write().value();
            writing.store(true);
            std::this_thread::sleep_for(settle);
        });

        // The writer is waiting on a reader that has not left, so it must not
        // have entered. Sleeping is what gives it the chance to be wrong.
        std::this_thread::sleep_for(settle);
        CHECK_FALSE(writing.load());
        CHECK_FALSE(gate.writing());

        // Leaving this scope releases the reader, which is what lets it through.
    }

    writer.join();
    CHECK(writing.load());
}

TEST_CASE("a reader admitted after the window is asked for waits for it",
          "[utxo][gate]") {
    watchdog guard(watchdog_budget, "the reader waiting on the window");
    // Writer preference, and it is the property rather than a tuning choice: a
    // stream of readers must not starve the deletion a batch cannot publish
    // without.
    utxo_gate gate;
    std::atomic<bool> read_entered{false};
    std::atomic<bool> window_open{false};

    auto window = gate.write().value();
    window_open.store(true);

    std::thread reader([&] {
        auto lease = gate.read().value();
        read_entered.store(true);
    });

    std::this_thread::sleep_for(settle);
    CHECK_FALSE(read_entered.load());

    // And the door reopens: the reader must progress once the window closes,
    // which is what rules out an implementation that simply blocks forever.
    { auto closing = std::move(window); }
    reader.join();
    CHECK(read_entered.load());
}

TEST_CASE("many readers coexist and none excludes another", "[utxo][gate]") {
    watchdog guard(watchdog_budget, "the concurrent readers");
    // The positive control for the shared side. Without it, a gate that made
    // every reader exclusive would pass every exclusion test in this file.
    utxo_gate gate;
    constexpr int count = 8;
    std::atomic<int> inside{0};
    std::atomic<int> peak{0};
    std::vector<std::thread> readers;

    for (int i = 0; i < count; ++i) {
        readers.emplace_back([&] {
            auto lease = gate.read().value();
            auto const now = inside.fetch_add(1) + 1;
            int seen = peak.load();
            while (now > seen && ! peak.compare_exchange_weak(seen, now)) {
            }
            std::this_thread::sleep_for(settle);
            inside.fetch_sub(1);
        });
    }

    for (auto& r : readers) {
        r.join();
    }

    CHECK(peak.load() > 1);
    CHECK(gate.readers() == 0);
}

TEST_CASE("a reader forced between the inserts and the deletions waits for both",
          "[utxo][gate]") {
    watchdog guard(watchdog_budget, "the reader forced mid-operation");
    // THE ONE THIS EXISTS FOR. A window taken per mutating call keeps readers
    // out of the mappings and still lets one in at the exact point where the
    // set holds outputs the blocks spent — inserts applied, deletions not yet.
    // The transition record does not help: a reader never consults it.
    //
    // The moment is FORCED, not raced for, and both directions are pinned:
    // the operation waits until the reader has announced it is about to ask,
    // and the reader records that the window was still open when it did. A
    // version that only spawned the reader and slept could pass without the
    // reader ever blocking — it would reach the gate after the window closed,
    // observe the finished state, and look identical to the real thing.
    utxo_gate gate;

    std::atomic<bool> inserts_done{false};
    std::atomic<bool> deletes_done{false};
    std::atomic<bool> reader_at_gate{false};
    std::atomic<bool> saw_window_open{false};
    std::atomic<bool> read_saw_deletes{false};
    std::atomic<bool> read_returned{false};

    std::thread mutation([&] {
        // ONE window for the whole logical operation.
        auto window = gate.write().value();

        inserts_done.store(true);              // ... inserts applied

        // The incoherent interval, held open until the reader is actually at the
        // door. Without this the sleep alone decides, and a slow thread start
        // would let the whole mutation finish first.
        while ( ! reader_at_gate.load()) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(settle);

        deletes_done.store(true);              // ... deletions applied
    });

    // Enter exactly in that interval.
    while ( ! inserts_done.load()) {
        std::this_thread::yield();
    }

    std::thread reader([&] {
        // Recorded BEFORE blocking: this is the proof that the reader arrived
        // mid-operation rather than after it. The mutation cannot finish until
        // the flag below is seen, so the window is necessarily still open here.
        saw_window_open.store(gate.writing());
        reader_at_gate.store(true);

        auto lease = gate.read().value();
        read_saw_deletes.store(deletes_done.load());
        read_returned.store(true);
    });

    mutation.join();
    reader.join();

    // Arrived while the window was open...
    CHECK(saw_window_open.load());
    // ... waited, and by the time it was admitted the whole mutation had landed.
    // Under a per-call window it would have been admitted mid-operation and
    // observed the set with the deletions still outstanding.
    CHECK(read_returned.load());
    CHECK(read_saw_deletes.load());
}

TEST_CASE("an exception inside the window does not leave the door shut",
          "[utxo][gate]") {
    watchdog guard(watchdog_budget, "the door after an exception");
    // Fail-closed is about refusing to publish, never about wedging the node.
    utxo_gate gate;

    try {
        auto window = gate.write().value();
        throw std::runtime_error("the operation failed");
    } catch (std::runtime_error const&) {
    }

    CHECK_FALSE(gate.writing());

    // Provable rather than asserted from a flag: a reader gets in.
    std::atomic<bool> entered{false};
    std::thread reader([&] {
        auto lease = gate.read().value();
        entered.store(true);
    });
    reader.join();
    CHECK(entered.load());

    // And the window can be taken again.
    {
        auto again = gate.write().value();
        CHECK(again.held());
    }
    CHECK_FALSE(gate.writing());
}

TEST_CASE("a released window and lease report themselves released",
          "[utxo][gate]") {
    utxo_gate gate;

    {
        auto window = gate.write().value();
        CHECK(window.held());
        auto moved = std::move(window);
        CHECK(moved.held());
        CHECK_FALSE(window.held());   // moved from: must not release twice
    }
    CHECK_FALSE(gate.writing());

    {
        auto lease = gate.read().value();
        CHECK(lease.held());
        auto moved = std::move(lease);
        CHECK(moved.held());
        CHECK_FALSE(lease.held());
    }
    CHECK(gate.readers() == 0);
}

// =============================================================================
// Thread affinity
// =============================================================================
//
// A capability carries a pointer to its gate, and that alone would authorise
// anywhere: a window taken in one thread and used in another would open the
// store for a thread the gate never admitted, while the gate's recorded owner
// still named the first one — so the thread actually holding the window could
// ask for another and be told to wait for itself.
//
// The contract IS thread-affine: the scope audit behind it is per function per
// thread, and the rule that nothing may live across a co_await exists because a
// coroutine resumes on whichever executor thread is free. These make that
// checkable. None of them depends on a global timeout to reach a verdict: each
// failure is a value or an exception, and the watchdogs are there so a
// regression is a named abort instead of a wedge.

TEST_CASE("a capability used by another thread authorises nothing",
          "[utxo][gate][affinity]") {
    watchdog guard(watchdog_budget, "a cross-thread use of a capability");
    utxo_gate gate;
    guarded_store<fake_store> store(gate);

    SECTION("a write window") {
        auto const window = gate.write().value();
        // It still names the right gate — that is the point. What disqualifies it
        // is the thread asking, and the two are reported apart.
        CHECK(window.authorises(gate));

        std::atomic<bool> refused{false};
        std::atomic<bool> other_error{false};
        std::thread borrower([&] {
            try {
                store.with_write(window, [](auto& s) { ++s.touched; });
            } catch (utxo_affinity_error const&) {
                refused.store(true);
            } catch (...) {
                other_error.store(true);
            }
        });
        borrower.join();

        CHECK(refused.load());
        CHECK_FALSE(other_error.load());
        // Refused, and NOT by damaging the capability: the issuing thread
        // continues to hold a window that works.
        CHECK_NOTHROW(store.with_write(window, [](auto& s) { ++s.touched; }));
    }

    SECTION("a read lease") {
        auto const lease = gate.read().value();

        std::atomic<bool> refused{false};
        std::thread borrower([&] {
            try {
                store.with_read(lease, [](auto const& s) { return s.touched; });
            } catch (utxo_affinity_error const&) {
                refused.store(true);
            }
        });
        borrower.join();

        CHECK(refused.load());
        CHECK_NOTHROW(store.with_read(lease, [](auto const& s) { return s.touched; }));
    }
}

TEST_CASE("moving a capability out of its thread is refused at the move",
          "[utxo][gate][affinity]") {
    // The other shape of the transfer, and the earlier one: refusing the USE
    // catches a capability that already crossed, refusing the MOVE stops it from
    // crossing. An empty capability carries nothing, so it still moves freely.
    watchdog guard(watchdog_budget, "a cross-thread move of a capability");
    utxo_gate gate;

    auto window = gate.write().value();

    std::atomic<bool> refused{false};
    std::atomic<bool> stole_it{false};
    std::thread thief([&] {
        try {
            auto taken = std::move(window);
            stole_it.store(taken.held());
        } catch (utxo_affinity_error const&) {
            refused.store(true);
        }
    });
    thief.join();

    CHECK(refused.load());
    CHECK_FALSE(stole_it.load());
    // The refused move left the source intact rather than half-transferred: the
    // window is still held here, and still by this thread.
    CHECK(window.held());
    CHECK(window.on_issuing_thread());
    CHECK(gate.writing());
}

TEST_CASE("a capability released by another thread opens the gate",
          "[utxo][gate][affinity]") {
    // Destruction is deliberately not affinity-checked: a destructor that threw
    // during unwinding would terminate. What it must do instead is leave the gate
    // OPEN and uncorrupted, which is what this asserts — because the failure mode
    // of getting this wrong is a store nobody can ever reach again.
    watchdog guard(watchdog_budget, "a cross-thread release");
    utxo_gate gate;
    guarded_store<fake_store> store(gate);

    // Parked by its owner, which is legal: the move happens on the issuing
    // thread. What follows is another thread ending its life.
    std::optional<utxo_write_window> parked = gate.write().value();
    REQUIRE(gate.writing());

    std::thread releaser([&] { parked.reset(); });
    releaser.join();

    CHECK_FALSE(gate.writing());

    // Not merely "the flag is clear": a new writer and a new reader both get in,
    // which is the property the flag stands for. Neither can be satisfied by a
    // gate left in a half-released state.
    {
        auto const fresh = gate.write().value();
        CHECK(fresh.held());
        CHECK_NOTHROW(store.with_write(fresh, [](auto& s) { ++s.touched; }));
    }
    {
        auto const lease = gate.read().value();
        CHECK(lease.held());
        CHECK_NOTHROW(store.with_read(lease, [](auto const& s) { return s.touched; }));
    }
    CHECK(gate.readers() == 0);

    // And from a DIFFERENT thread, so the gate is open in general rather than
    // only for the one that happens to have run before.
    std::atomic<bool> got_in{false};
    std::thread later([&] {
        auto const window = gate.write().value();
        got_in.store(window.held());
    });
    later.join();
    CHECK(got_in.load());
}

TEST_CASE("a capability moved and used within one thread keeps working",
          "[utxo][gate][affinity]") {
    // The negation of the three above: affinity must not cost the ordinary move.
    // Returning a capability, parking it in an optional and handing it on all
    // happen on one thread and must remain unremarkable.
    utxo_gate gate;
    guarded_store<fake_store> store(gate);

    std::optional<utxo_write_window> parked = gate.write().value();
    CHECK_NOTHROW(store.with_write(*parked, [](auto& s) { ++s.touched; }));

    auto moved = std::move(*parked);
    parked.reset();
    CHECK(moved.held());
    CHECK(moved.on_issuing_thread());
    CHECK_NOTHROW(store.with_write(moved, [](auto& s) { ++s.touched; }));

    // Released here, so what follows is not measuring the window instead of the
    // lease: with it still held, try_read_for would time out and the thread would
    // prove nothing while appearing to pass.
    {
        auto const spent = std::move(moved);
    }
    REQUIRE_FALSE(gate.writing());

    // A capability taken and moved INSIDE another thread is that thread's own,
    // and works there — affinity is about crossing, not about which thread it is.
    std::atomic<bool> acquired{false};
    std::atomic<bool> usable{false};
    std::thread worker([&] {
        auto lease = gate.read().value();
        acquired.store(true);
        auto owned = std::move(lease);
        usable.store(owned.on_issuing_thread() &&
            store.with_read(owned, [](auto const& s) { return s.touched; }) == 2);
    });
    worker.join();

    CHECK(acquired.load());
    CHECK(usable.load());
    CHECK(gate.readers() == 0);
}
