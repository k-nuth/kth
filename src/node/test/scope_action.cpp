// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <future>
#include <stdexcept>
#include <type_traits>
#include <string>

#include <kth/node/detail/scope_action.hpp>

using namespace kth;

// =============================================================================
// The mechanism both lifecycle guarantees rest on
// =============================================================================
//
// The executor has two properties of the form "whatever happens, this must have
// been published", and both were once written as a statement at the end of the
// happy path:
//
//   * an admitted start owes exactly one outcome. A start that threw between
//     being admitted and reaching its coroutine published none, so a release
//     waiting for that outcome waited forever;
//   * a teardown always publishes that its attempt ended. One that threw left
//     `release_done_` false, so a second caller waited on a condition variable
//     nobody would notify — and left the io thread joinable for ~std::thread to
//     abort on.
//
// The mechanism is a named type so those properties can be checked here, on
// their own, rather than only by arranging for production code to throw. What
// this file does NOT establish is that the guard's scope covers any particular
// statement in the executor; that is placement, and it is read in the diff.

namespace {

struct counter {
    int runs{0};
};

} // namespace

TEST_CASE("an armed scope action runs on the way out", "[scope_action]") {
    counter seen;
    {
        node::detail::scope_action guard([&seen]() { ++seen.runs; }, true);
        CHECK(guard.armed());
        CHECK(seen.runs == 0);      // not before the scope ends
    }
    REQUIRE(seen.runs == 1);
}

TEST_CASE("a disarmed scope action does not run", "[scope_action]") {
    counter seen;
    {
        node::detail::scope_action guard([&seen]() { ++seen.runs; }, true);
        guard.disarm();
        CHECK_FALSE(guard.armed());
    }
    REQUIRE(seen.runs == 0);
}

TEST_CASE("a scope action runs once, whether it is run early or left to the scope", "[scope_action]") {
    counter seen;
    {
        node::detail::scope_action guard([&seen]() { ++seen.runs; }, true);
        guard.run();
        CHECK(seen.runs == 1);
        CHECK_FALSE(guard.armed());
        guard.run();                // asking twice is not doing it twice
        CHECK(seen.runs == 1);
    }
    REQUIRE(seen.runs == 1);        // and neither is the destructor
}

TEST_CASE("a scope action runs while an exception is propagating", "[scope_action]") {
    // The case it exists for. Every one of the exits it has to cover — an
    // exception out of the log setup, out of the node's construction, out of
    // start_io_thread(), out of co_spawn — is this one.
    counter seen;
    auto throw_past_the_guard = [&seen]() {
        node::detail::scope_action guard([&seen]() { ++seen.runs; }, true);
        throw std::runtime_error("thrown past the guard");
    };
    CHECK_THROWS_AS(throw_past_the_guard(), std::runtime_error);
    REQUIRE(seen.runs == 1);
}

TEST_CASE("a scope action that throws does not escape its destructor", "[scope_action]") {
    // It runs from destructors during exception propagation, so an action that
    // throws must not turn a reported failure into a terminate. A process that
    // reaches the assertion below is the whole assertion.
    {
        node::detail::scope_action guard([]() { throw std::runtime_error("from the action"); }, true);
    }
    SUCCEED("the throwing action did not propagate out of the destructor");
}

TEST_CASE("a scope action that throws when run early does not propagate either", "[scope_action]") {
    node::detail::scope_action guard([]() { throw std::runtime_error("from the action"); }, true);
    CHECK_NOTHROW(guard.run());
    CHECK_FALSE(guard.armed());
}

TEST_CASE("a scope action reads its result at the moment it runs", "[scope_action]") {
    // How release() tells a failed teardown from a clean one: the guard publishes
    // whatever the result holds when it fires, so an exception before the result
    // is set to success publishes the failure.
    std::string published;
    std::string result = "failed";
    {
        node::detail::scope_action guard([&published, &result]() { published = result; }, true);
        result = "succeeded";
    }
    CHECK(published == "succeeded");

    published.clear();
    result = "failed";
    // Hoisted: a capture list with a comma in it would split the macro argument.
    auto throw_before_success = [&published, &result]() {
        node::detail::scope_action guard([&published, &result]() { published = result; }, true);
        throw std::runtime_error("before the result was set");
    };
    CHECK_THROWS_AS(throw_before_success(), std::runtime_error);
    REQUIRE(published == "failed");
}

TEST_CASE("a scope action can be put in place before it is owed", "[scope_action]") {
    // How the executor gets protection that exists before the debt does: the
    // guard is constructed disarmed and armed at the moment the debt is
    // incurred, so nothing between the two can throw and leave it uncovered.
    counter seen;
    {
        node::detail::scope_action guard([&seen]() { ++seen.runs; }, false);
        CHECK_FALSE(guard.armed());
        guard.arm();
        CHECK(guard.armed());
    }
    REQUIRE(seen.runs == 1);
}

TEST_CASE("a scope action constructed disarmed and never armed does nothing", "[scope_action]") {
    counter seen;
    {
        node::detail::scope_action guard([&seen]() { ++seen.runs; }, false);
    }
    REQUIRE(seen.runs == 0);
}

TEST_CASE("a scope action costs no allocation and cannot throw while being built", "[scope_action]") {
    // The reason it is a template. It held a std::function once, and one holding
    // three captured pointers allocates on this library — so the guard covering a
    // debt could throw while being constructed and leave that debt uncovered.
    // Checked by the compiler, not by a comment.
    int a = 0;
    int b = 0;
    auto action = [&a, &b, p = &a]() { (void)a; (void)b; (void)p; };
    using guard_type = node::detail::scope_action<decltype(action)>;

    static_assert(std::is_nothrow_constructible_v<guard_type, decltype(action), bool>,
        "constructing the guard must not be able to throw");
    static_assert(sizeof(guard_type) <= sizeof(action) + alignof(guard_type),
        "the action is stored by value, not behind an allocation");

    guard_type guard(action, false);
    CHECK_FALSE(guard.armed());
}

// =============================================================================
// Why the run-completed pair is built by the constructor
// =============================================================================
//
// The executor publishes `start_admitted_` under its lifecycle lock, and from
// that instant a release will wait for an outcome. Anything between that
// publication and the guard that pays the debt must be incapable of throwing —
// an exception there leaves admit_start(), past its caller, with the debt owed
// and nothing able to answer it.
//
// These are the measurements that decided where the pair is built. They are
// assertions rather than a comment so that a standard library which changed them
// makes somebody re-read the reasoning instead of silently invalidating it.

TEST_CASE("building a promise is not something that can be done under a lock", "[scope_action][lifecycle]") {
    static_assert( ! std::is_nothrow_default_constructible_v<std::promise<void>>,
        "a promise allocates its shared state, so building one cannot happen between "
        "publishing the admission and arming the guard");

    std::promise<void> probe;
    static_assert( ! noexcept(probe.get_future()),
        "get_future() reports a second retrieval by throwing, so it cannot happen there either");

    // What admit_start() is left with, and why it is safe there.
    static_assert(std::is_nothrow_move_assignable_v<std::promise<void>>);
    static_assert(std::is_nothrow_move_assignable_v<std::future<void>>);
    SUCCEED("the throwing pair is built by the constructor, where a throw means no executor");
}
