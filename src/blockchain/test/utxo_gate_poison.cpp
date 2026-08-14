// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// When the gate latches, and what it takes to latch it.
//
// The property being defended is narrow and easy to lose: an operation that may
// have applied part of its work and did not finish must leave the store closed
// to everyone, and it must do so WITHOUT any of its exits saying so. The eight
// `co_return`s of a connect batch, an exception out of a deletion sweep and a
// cancellation all leave the same way — through the window's destructor — and
// that is the only place the decision can be made once.
//
// So every case here leaves a window WITHOUT calling anything on the way out,
// and then asks the gate. Nothing is logged, no callback runs, no fatal handler
// is installed: if any of those were required for the latch to happen, these
// would pass while production hung on a store nobody had closed.

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <kth/blockchain/utxo_gate.hpp>
#include <kth/database/databases/result_code.hpp>

using namespace kth;
using namespace kth::blockchain;
using kth::database::result_code;

namespace {

struct fake_store {
    int writes{0};
    int reads{0};
};

} // namespace

// =============================================================================
// What latches and what does not
// =============================================================================

TEST_CASE("poison - an observational window latches nothing", "[utxo_gate][poison]") {
    // Statistics, a sizing report, a bloom walk, the open. They take the
    // exclusive window because UTXO-Z will not have a traversal overlap a
    // mutation, not because they apply anything — so however they leave, the
    // store is exactly as they found it.
    utxo_gate gate;
    {
        auto window = gate.write();
        REQUIRE(window);
        CHECK_FALSE(window->would_poison());
    }
    CHECK_FALSE(gate.poisoned());
    CHECK(gate.write().has_value());
}

TEST_CASE("poison - a mutation that never completes latches the gate", "[utxo_gate][poison]") {
    // The early-return shape: something was declared, an exit was taken, and the
    // exit said nothing. This is the eight connect exits and the reorg's
    // abandonment paths, reduced to what they have in common.
    utxo_gate gate;
    {
        auto window = gate.write();
        REQUIRE(window);
        window->mark_mutating();
        CHECK(window->would_poison());
        // and out, with nothing else called
    }
    CHECK(gate.poisoned());
}

TEST_CASE("poison - an exception out of a marked window latches it", "[utxo_gate][poison]") {
    // Nothing on the unwinding path calls the gate. If the latch depended on a
    // catch block, this is the case that would find out.
    utxo_gate gate;
    CHECK_THROWS_AS([&] {
        auto window = gate.write();
        REQUIRE(window);
        window->mark_mutating();
        throw std::runtime_error("a deletion sweep gave up");
    }(), std::runtime_error);

    CHECK(gate.poisoned());
}

TEST_CASE("poison - success without complete() still latches", "[utxo_gate][poison]") {
    // The forgotten-call case, and it is deliberately NOT forgiven. A mutation
    // that ran to the end and did not say so is indistinguishable, from here,
    // from one that stopped halfway — and the safe reading of "I do not know" is
    // the closed one. This is what turns a future missing complete() into a
    // failed test rather than a store quietly left open.
    utxo_gate gate;
    {
        auto window = gate.write();
        REQUIRE(window);
        window->mark_mutated();   // evidence of a change, applied and finished
        // complete() forgotten
    }
    CHECK(gate.poisoned());
}

TEST_CASE("poison - a completed mutation latches nothing", "[utxo_gate][poison]") {
    utxo_gate gate;
    {
        auto window = gate.write();
        REQUIRE(window);
        window->mark_mutating();
        window->mark_mutated();
        window->complete();
        CHECK_FALSE(window->would_poison());
    }
    CHECK_FALSE(gate.poisoned());
    CHECK(gate.write().has_value());
}

// =============================================================================
// The three facts are three
// =============================================================================

TEST_CASE("poison - the conservative flag is not the evidence", "[utxo_gate][poison]") {
    // If these were one field, `switch_result.mutated` would be true for every
    // switch that was attempted and rejected — and the reorg caller republishes
    // the chain view on it, moving the generation and dropping the template
    // cache for a switch that touched nothing.
    utxo_gate gate;
    auto window = gate.write();
    REQUIRE(window);

    window->mark_mutating();
    CHECK(window->would_poison());
    CHECK_FALSE(window->has_mutated());   // attempted, no evidence of change

    window->mark_mutated();
    CHECK(window->has_mutated());
}

TEST_CASE("poison - evidence alone arms the latch too", "[utxo_gate][poison]") {
    // The other direction: an operation that discovers it changed something
    // without having declared it first is still unsafe to abandon.
    utxo_gate gate;
    {
        auto window = gate.write();
        REQUIRE(window);
        window->mark_mutated();
        CHECK(window->would_poison());
    }
    CHECK(gate.poisoned());
}

TEST_CASE("poison - complete() is a contract, not a hint", "[utxo_gate][poison]") {
    utxo_gate gate;
    auto window = gate.write();
    REQUIRE(window);

    // On a window that declared nothing: the caller and this window disagree
    // about which protocol is being run, and the loud failure is the one worth
    // having.
    CHECK_THROWS_AS(window->complete(), utxo_capability_error);

    window->mark_mutating();
    window->complete();
    CHECK_THROWS_AS(window->complete(), utxo_capability_error);
}

TEST_CASE("poison - the facts move with the window", "[utxo_gate][poison]") {
    // A moved-from window holds no gate and must poison nothing; the receiving
    // one owes everything the source owed.
    utxo_gate gate;
    {
        auto source = gate.write();
        REQUIRE(source);
        source->mark_mutating();

        auto moved = std::move(*source);
        CHECK(moved.would_poison());
        CHECK_FALSE(source->held());
    }
    CHECK(gate.poisoned());
}

// =============================================================================
// What a latched gate does
// =============================================================================

TEST_CASE("poison - a latched gate refuses reads and writes", "[utxo_gate][poison]") {
    utxo_gate gate;
    {
        auto window = gate.write();
        REQUIRE(window);
        window->mark_mutating();
    }

    auto const lease = gate.read();
    REQUIRE_FALSE(lease);
    CHECK(lease.error() == result_code::recovery_required);

    auto const window = gate.write();
    REQUIRE_FALSE(window);
    CHECK(window.error() == result_code::recovery_required);

    // The timed variants decline rather than wait out their budget. A generous
    // budget on purpose: with zero the refusal is indistinguishable from an
    // expired deadline, so the assertion would hold on an unlatched gate too.
    // Nothing holds the gate here, so a non-latched one would acquire at once.
    CHECK_FALSE(gate.try_write_for(std::chrono::seconds(5)).has_value());
    CHECK_FALSE(gate.try_read_for(std::chrono::seconds(5)).has_value());
}

TEST_CASE("poison - no parked reader is ever admitted by a latching release",
          "[utxo_gate][poison]") {
    // The ordering inside end_write(), and the only way to see it is to lose the
    // race on purpose. Readers park in cv_.wait() while the writer holds the
    // window; the writer then marks and releases. If the latch were published
    // AFTER the writer stood down and the mutex was dropped, a woken reader
    // would find the door open, the latch not yet set, and be admitted to a
    // store nobody has declared unusable.
    //
    // One reader once cannot show this: it almost always arrives late enough to
    // see the flag anyway. Many readers over many rounds do — the window is
    // small, not absent, and a single admission across the whole run is a
    // failure, because in the correct ordering it is impossible rather than
    // unlikely.
    constexpr int rounds = 200;
    constexpr int readers_per_round = 4;

    std::atomic<int> admitted{0};

    for (int round = 0; round < rounds; ++round) {
        utxo_gate gate;
        auto window = gate.write();
        REQUIRE(window);

        std::atomic<int> ready{0};
        std::vector<std::thread> readers;
        readers.reserve(readers_per_round);

        for (int i = 0; i < readers_per_round; ++i) {
            readers.emplace_back([&] {
                ++ready;
                // Blocks: the window is held. Every one of these is parked
                // before the release below, so any lease returned here was
                // granted by that release.
                if (auto lease = gate.read(); lease) {
                    ++admitted;
                }
            });
        }

        while (ready.load() < readers_per_round) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        window->mark_mutating();
        { auto const dying = std::move(*window); }   // latch, open, notify

        for (auto& reader : readers) {
            reader.join();
        }
        REQUIRE(gate.poisoned());
    }

    CHECK(admitted.load() == 0);
}

TEST_CASE("poison - the latch is irreversible", "[utxo_gate][poison]") {
    utxo_gate gate;
    {
        auto window = gate.write();
        REQUIRE(window);
        window->mark_mutating();
    }
    REQUIRE(gate.poisoned());

    // Closing is a wind-down, not a repair.
    { auto const closing = gate.authorise_close(); }
    CHECK(gate.poisoned());
    CHECK_FALSE(gate.write().has_value());

    // And a clean operation afterwards cannot wash it out — there is no path
    // that clears the flag, so a window that completes changes nothing.
    CHECK_FALSE(gate.read().has_value());
}

// =============================================================================
// The administrative capability
// =============================================================================

TEST_CASE("poison - close works on a latched gate and only close", "[utxo_gate][poison]") {
    utxo_gate gate;
    guarded_store<fake_store> store(gate);
    {
        auto window = gate.write();
        REQUIRE(window);
        window->mark_mutating();
    }
    REQUIRE(gate.poisoned());

    auto closing = gate.authorise_close();
    CHECK(closing.held());
    CHECK(closing.authorises(gate));
    CHECK(closing.on_issuing_thread());

    store.with_close(closing, [](auto& s) { ++s.writes; });
}

TEST_CASE("poison - a close authority from another gate authorises nothing", "[utxo_gate][poison]") {
    utxo_gate gate;
    utxo_gate other;
    guarded_store<fake_store> store(gate);

    auto foreign = other.authorise_close();
    CHECK_THROWS_AS(store.with_close(foreign, [](auto&) {}), utxo_capability_error);
}

TEST_CASE("poison - close excludes like any writer", "[utxo_gate][poison]") {
    // It skips the poison check and nothing else: a close that ran alongside a
    // reader would unmap what that reader is holding.
    utxo_gate gate;
    auto lease = gate.read();
    REQUIRE(lease);

    CHECK_FALSE(gate.try_write_for(std::chrono::milliseconds(5)).has_value());

    std::atomic<bool> closed{false};
    std::thread closer([&] {
        auto const closing = gate.authorise_close();
        closed = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(closed);   // still waiting for the reader

    { auto const dying = std::move(*lease); }
    closer.join();
    CHECK(closed);
}

// =============================================================================
// A store that latched under us
// =============================================================================

TEST_CASE("poison - a caller parked in try_write_for is not admitted by a latch",
          "[utxo_gate][poison]") {
    // The same defect the double-check in write() exists for, on the timed
    // variant. The budget is long enough that the caller is genuinely parked
    // rather than expiring, so an admission here would be an admission and not a
    // timeout.
    utxo_gate gate;
    auto window = gate.write();
    REQUIRE(window);
    window->mark_mutating();

    std::atomic<bool> parked{false};
    std::atomic<bool> admitted{false};

    std::thread waiter([&] {
        parked = true;
        if (gate.try_write_for(std::chrono::seconds(5))) {
            admitted = true;
        }
    });

    while ( ! parked) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    { auto const dying = std::move(*window); }
    waiter.join();

    CHECK_FALSE(admitted.load());
    CHECK(gate.poisoned());
}

TEST_CASE("poison - a caller parked in try_read_for is not admitted by a latch",
          "[utxo_gate][poison]") {
    utxo_gate gate;
    auto window = gate.write();
    REQUIRE(window);
    window->mark_mutating();

    std::atomic<bool> parked{false};
    std::atomic<bool> admitted{false};

    std::thread waiter([&] {
        parked = true;
        if (gate.try_read_for(std::chrono::seconds(5))) {
            admitted = true;
        }
    });

    while ( ! parked) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    { auto const dying = std::move(*window); }
    waiter.join();

    CHECK_FALSE(admitted.load());
}

TEST_CASE("poison - an observed latch closes the gate", "[utxo_gate][poison]") {
    // The read path's way in. A read leaves nothing half-applied, so its lease
    // cannot latch on release — but UTXO-Z latches itself, and a boundary that
    // only translated the code would leave every later caller queueing for a
    // store that has already stopped answering.
    utxo_gate gate;
    {
        auto const lease = gate.read();
        REQUIRE(lease);
        gate.latch_observed();
    }

    CHECK(gate.poisoned());
    CHECK_FALSE(gate.read().has_value());
    CHECK_FALSE(gate.write().has_value());
}

TEST_CASE("poison - observing a latch twice changes nothing", "[utxo_gate][poison]") {
    utxo_gate gate;
    gate.latch_observed();
    gate.latch_observed();
    CHECK(gate.poisoned());

    // And it still only ever sets: closing after it leaves it latched.
    { auto const closing = gate.authorise_close(); }
    CHECK(gate.poisoned());
}

TEST_CASE("poison - a latched gate reaches the store zero times", "[utxo_gate][poison]") {
    // The APIs that still return void — set_utxo_bloom and clear_utxo_bloom,
    // left alone for #661 — cannot report the refusal. What they CAN do is not
    // touch the store, and that is the property worth pinning: a signature that
    // has nowhere to put an error is not a licence to proceed as if there were
    // none.
    utxo_gate gate;
    guarded_store<fake_store> store(gate);

    {
        auto window = gate.write();
        REQUIRE(window);
        store.with_write(*window, [](auto& s) { ++s.writes; });
        window->mark_mutating();
    }
    REQUIRE(gate.poisoned());

    int const writes_before = [&] {
        auto const closing = gate.authorise_close();
        int seen = 0;
        store.with_close(closing, [&](auto& s) { seen = s.writes; });
        return seen;
    }();
    REQUIRE(writes_before == 1);

    // Everything a void API would do: take a window, and only reach the store if
    // it got one.
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto window = gate.write();
        if ( ! window) {
            continue;
        }
        store.with_write(*window, [](auto& s) { ++s.writes; });
    }

    auto const closing = gate.authorise_close();
    store.with_close(closing, [&](auto& s) {
        CHECK(s.writes == writes_before);   // not one more
    });
}
