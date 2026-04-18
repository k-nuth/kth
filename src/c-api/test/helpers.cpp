// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unlike the other files in this directory, this test is genuinely C++:
// it exercises the internal `kth/capi/helpers.hpp` template machinery,
// which has no C surface. Kept here because that header lives alongside
// the C-API and the tests guard it against regressions.

#include <memory>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include <kth/capi/helpers.hpp>

namespace {

// Distinct types so the static_asserts below prove overload selection
// by looking at the returned pointer type, not by relying on equality.
struct plain_val { int n = 0; };

} // namespace

// ---------------------------------------------------------------------------
// kth::leak overload selection — compile-time checks
// ---------------------------------------------------------------------------

// For a plain value the forwarding-reference overload wins and returns
// a heap copy of the value itself.
static_assert(std::is_same_v<
    decltype(kth::leak(std::declval<plain_val>())),
    plain_val*>);
static_assert(std::is_same_v<
    decltype(kth::leak(std::declval<plain_val&>())),
    plain_val*>);
static_assert(std::is_same_v<
    decltype(kth::leak(std::declval<plain_val const&>())),
    plain_val*>);

// For a std::shared_ptr the forwarding-reference overload must step
// aside so the dedicated `leak(std::shared_ptr<T> const&)` wins and
// leaks the POINTED-TO object (return type `T*`), not a heap-allocated
// shared_ptr (`std::shared_ptr<T>*`). The two are catastrophically
// different from a C consumer's perspective, so we pin the contract
// with a static_assert that breaks the build if the constraint on the
// forwarding-reference overload ever regresses.
static_assert(std::is_same_v<
    decltype(kth::leak(std::declval<std::shared_ptr<plain_val>>())),
    plain_val*>);
static_assert(std::is_same_v<
    decltype(kth::leak(std::declval<std::shared_ptr<plain_val>&>())),
    plain_val*>);
static_assert(std::is_same_v<
    decltype(kth::leak(std::declval<std::shared_ptr<plain_val> const&>())),
    plain_val*>);

// ---------------------------------------------------------------------------
// kth::leak overload selection — runtime behaviour
// ---------------------------------------------------------------------------

TEST_CASE("kth::leak unwraps shared_ptr and copies the pointed-to value",
          "[capi helpers]") {
    auto ptr = std::make_shared<plain_val>(plain_val{42});
    auto* leaked = kth::leak(ptr);

    // Must be a fresh heap allocation: distinct address from the shared_ptr's
    // managed object, yet carrying the same value.
    REQUIRE(leaked != ptr.get());
    REQUIRE(leaked->n == 42);

    delete leaked;
}

TEST_CASE("kth::leak copies a plain lvalue onto the heap",
          "[capi helpers]") {
    plain_val src{7};
    auto* leaked = kth::leak(src);

    REQUIRE(leaked != &src);
    REQUIRE(leaked->n == 7);

    delete leaked;
}

TEST_CASE("kth::leak moves from an rvalue onto the heap",
          "[capi helpers]") {
    auto* leaked = kth::leak(plain_val{99});

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 99);

    delete leaked;
}

TEST_CASE("kth::leak returns nullptr for an empty shared_ptr",
          "[capi helpers]") {
    std::shared_ptr<plain_val> empty;
    auto* leaked = kth::leak(empty);

    REQUIRE(leaked == nullptr);
}

// ---------------------------------------------------------------------------
// Move-capable helpers
// ---------------------------------------------------------------------------

namespace {

// Move-tracking type: the bool tells whether the value's guts were
// moved from, which is enough to tell copy vs. move apart without
// relying on implementation-defined moved-from state.
struct move_tracker {
    int n = 0;
    bool moved_from = false;

    move_tracker() = default;
    explicit move_tracker(int n_) : n(n_) {}
    move_tracker(move_tracker const& other) : n(other.n) {}
    move_tracker(move_tracker&& other) noexcept
        : n(other.n) { other.moved_from = true; }
    move_tracker& operator=(move_tracker const&) = default;
    move_tracker& operator=(move_tracker&&) noexcept = default;
};

} // namespace

TEST_CASE("kth::leak on an rvalue shared_ptr moves the pointed-to value",
          "[capi helpers]") {
    auto ptr = std::make_shared<move_tracker>(5);
    auto* leaked = kth::leak(std::move(ptr));

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 5);
    // The rvalue overload moves from `*ptr`, so the original managed
    // object ends up in a moved-from state.
    REQUIRE(ptr->moved_from == true);

    delete leaked;
}

TEST_CASE("kth::leak on an lvalue shared_ptr copies the pointed-to value",
          "[capi helpers]") {
    auto ptr = std::make_shared<move_tracker>(9);
    auto* leaked = kth::leak(ptr);

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 9);
    // Lvalue overload must NOT touch the managed object.
    REQUIRE(ptr->moved_from == false);

    delete leaked;
}

TEST_CASE("kth::leak_if_success moves from a non-shared_ptr rvalue",
          "[capi helpers]") {
    move_tracker src(11);
    std::error_code const ok = kth::error::success;
    auto* leaked = kth::leak_if_success(std::move(src), ok);

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 11);
    REQUIRE(src.moved_from == true);

    delete leaked;
}

TEST_CASE("kth::leak_if_success returns nullptr on error without touching input",
          "[capi helpers]") {
    move_tracker src(13);
    std::error_code const bad = kth::error::not_found;
    auto* leaked = kth::leak_if_success(std::move(src), bad);

    REQUIRE(leaked == nullptr);
    // On error we never construct the heap copy, so the move never runs.
    REQUIRE(src.moved_from == false);
}

TEST_CASE("kth::leak_if_success moves from an rvalue shared_ptr on success",
          "[capi helpers]") {
    auto ptr = std::make_shared<move_tracker>(21);
    std::error_code const ok = kth::error::success;
    auto* leaked = kth::leak_if_success(std::move(ptr), ok);

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 21);
    REQUIRE(ptr->moved_from == true);

    delete leaked;
}

TEST_CASE("kth::leak_if_success copies from an lvalue shared_ptr on success",
          "[capi helpers]") {
    auto ptr = std::make_shared<move_tracker>(23);
    std::error_code const ok = kth::error::success;
    auto* leaked = kth::leak_if_success(ptr, ok);

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 23);
    // Lvalue overload must not touch the managed object.
    REQUIRE(ptr->moved_from == false);

    delete leaked;
}

TEST_CASE("kth::leak_if_success on error leaves a shared_ptr untouched",
          "[capi helpers]") {
    auto lv = std::make_shared<move_tracker>(29);
    auto rv = std::make_shared<move_tracker>(31);
    std::error_code const bad = kth::error::not_found;

    REQUIRE(kth::leak_if_success(lv, bad) == nullptr);
    REQUIRE(kth::leak_if_success(std::move(rv), bad) == nullptr);

    // Neither overload should have constructed a copy (and therefore
    // neither should have moved from `*rv`) when ec != success.
    REQUIRE(lv->moved_from == false);
    REQUIRE(rv->moved_from == false);
}
