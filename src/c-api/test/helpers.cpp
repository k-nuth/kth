// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unlike the other files in this directory, this test is genuinely C++:
// it exercises the internal `kth/capi/helpers.hpp` template machinery,
// which has no C surface. Kept here because that header lives alongside
// the C-API and the tests guard it against regressions.

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <kth/capi/error.h>
#include <kth/capi/handles.h>
#include <kth/capi/helpers.hpp>

namespace {

// Distinct types so the static_asserts below prove overload selection
// by looking at the returned pointer type, not by relying on equality.
struct plain_val { int n = 0; };

} // namespace

// ---------------------------------------------------------------------------
// kth::to_owned overload selection — compile-time checks
// ---------------------------------------------------------------------------

// For a plain value the forwarding-reference overload wins and returns
// a heap copy of the value itself.
static_assert(std::is_same_v<
    decltype(kth::to_owned(std::declval<plain_val>())),
    plain_val*>);
static_assert(std::is_same_v<
    decltype(kth::to_owned(std::declval<plain_val&>())),
    plain_val*>);
static_assert(std::is_same_v<
    decltype(kth::to_owned(std::declval<plain_val const&>())),
    plain_val*>);

// For a std::shared_ptr the forwarding-reference overload must step
// aside so the dedicated `to_owned(std::shared_ptr<T> const&)` wins and
// leaks the POINTED-TO object (return type `T*`), not a heap-allocated
// shared_ptr (`std::shared_ptr<T>*`). The two are catastrophically
// different from a C consumer's perspective, so we pin the contract
// with a static_assert that breaks the build if the constraint on the
// forwarding-reference overload ever regresses.
static_assert(std::is_same_v<
    decltype(kth::to_owned(std::declval<std::shared_ptr<plain_val>>())),
    plain_val*>);
static_assert(std::is_same_v<
    decltype(kth::to_owned(std::declval<std::shared_ptr<plain_val>&>())),
    plain_val*>);
static_assert(std::is_same_v<
    decltype(kth::to_owned(std::declval<std::shared_ptr<plain_val> const&>())),
    plain_val*>);

// ---------------------------------------------------------------------------
// kth::to_owned overload selection — runtime behaviour
// ---------------------------------------------------------------------------

TEST_CASE("kth::to_owned unwraps shared_ptr and copies the pointed-to value",
          "[capi helpers]") {
    auto ptr = std::make_shared<plain_val>(plain_val{42});
    auto* leaked = kth::to_owned(ptr);

    // Must be a fresh heap allocation: distinct address from the shared_ptr's
    // managed object, yet carrying the same value.
    REQUIRE(leaked != ptr.get());
    REQUIRE(leaked->n == 42);

    delete leaked;
}

TEST_CASE("kth::to_owned copies a plain lvalue onto the heap",
          "[capi helpers]") {
    plain_val src{7};
    auto* leaked = kth::to_owned(src);

    REQUIRE(leaked != &src);
    REQUIRE(leaked->n == 7);

    delete leaked;
}

TEST_CASE("kth::to_owned moves from an rvalue onto the heap",
          "[capi helpers]") {
    auto* leaked = kth::to_owned(plain_val{99});

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 99);

    delete leaked;
}

TEST_CASE("kth::to_owned returns nullptr for an empty shared_ptr",
          "[capi helpers]") {
    std::shared_ptr<plain_val> empty;
    auto* leaked = kth::to_owned(empty);

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

TEST_CASE("kth::to_owned on an rvalue shared_ptr moves the pointed-to value",
          "[capi helpers]") {
    auto ptr = std::make_shared<move_tracker>(5);
    auto* leaked = kth::to_owned(std::move(ptr));

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 5);
    // The rvalue overload moves from `*ptr`, so the original managed
    // object ends up in a moved-from state.
    REQUIRE(ptr->moved_from == true);

    delete leaked;
}

TEST_CASE("kth::to_owned on an lvalue shared_ptr copies the pointed-to value",
          "[capi helpers]") {
    auto ptr = std::make_shared<move_tracker>(9);
    auto* leaked = kth::to_owned(ptr);

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 9);
    // Lvalue overload must NOT touch the managed object.
    REQUIRE(ptr->moved_from == false);

    delete leaked;
}

TEST_CASE("kth::to_owned_if_success moves from a non-shared_ptr rvalue",
          "[capi helpers]") {
    move_tracker src(11);
    std::error_code const ok = kth::error::success;
    auto* leaked = kth::to_owned_if_success(std::move(src), ok);

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 11);
    REQUIRE(src.moved_from == true);

    delete leaked;
}

TEST_CASE("kth::to_owned_if_success returns nullptr on error without touching input",
          "[capi helpers]") {
    move_tracker src(13);
    std::error_code const bad = kth::error::not_found;
    auto* leaked = kth::to_owned_if_success(std::move(src), bad);

    REQUIRE(leaked == nullptr);
    // On error we never construct the heap copy, so the move never runs.
    REQUIRE(src.moved_from == false);
}

TEST_CASE("kth::to_owned_if_success moves from an rvalue shared_ptr on success",
          "[capi helpers]") {
    auto ptr = std::make_shared<move_tracker>(21);
    std::error_code const ok = kth::error::success;
    auto* leaked = kth::to_owned_if_success(std::move(ptr), ok);

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 21);
    REQUIRE(ptr->moved_from == true);

    delete leaked;
}

TEST_CASE("kth::to_owned_if_success copies from an lvalue shared_ptr on success",
          "[capi helpers]") {
    auto ptr = std::make_shared<move_tracker>(23);
    std::error_code const ok = kth::error::success;
    auto* leaked = kth::to_owned_if_success(ptr, ok);

    REQUIRE(leaked != nullptr);
    REQUIRE(leaked->n == 23);
    // Lvalue overload must not touch the managed object.
    REQUIRE(ptr->moved_from == false);

    delete leaked;
}

TEST_CASE("kth::to_owned_if_success on error leaves a shared_ptr untouched",
          "[capi helpers]") {
    auto lv = std::make_shared<move_tracker>(29);
    auto rv = std::make_shared<move_tracker>(31);
    std::error_code const bad = kth::error::not_found;

    REQUIRE(kth::to_owned_if_success(lv, bad) == nullptr);
    REQUIRE(kth::to_owned_if_success(std::move(rv), bad) == nullptr);

    // Neither overload should have constructed a copy (and therefore
    // neither should have moved from `*rv`) when ec != success.
    REQUIRE(lv->moved_from == false);
    REQUIRE(rv->moved_from == false);
}

// ---------------------------------------------------------------------------
// kth::try_create_c_array — helper only (generated wrappers are covered by
// the transaction closure tests and the exception-barrier executable)
// ---------------------------------------------------------------------------

TEST_CASE("kth::try_create_c_array publishes buffer and length on success",
          "[kth::try_create_c_array]") {
    kth::data_chunk const data{9, 8, 7, 6};
    uint8_t* out = nullptr;
    kth_size_t out_size = 0;
    REQUIRE(kth::try_create_c_array(data, &out, &out_size) == kth_ec_success);
    REQUIRE(out != nullptr);
    REQUIRE(out_size == data.size());
    REQUIRE(std::equal(data.begin(), data.end(), out));
    kth_core_destruct_array(out);
}

TEST_CASE("kth::try_create_c_array gives an empty array a non-null buffer",
          "[kth::try_create_c_array]") {
    kth::data_chunk const empty;
    uint8_t* out = nullptr;
    kth_size_t out_size = 0xdeadu;
    REQUIRE(kth::try_create_c_array(empty, &out, &out_size) == kth_ec_success);
    REQUIRE(out != nullptr);
    REQUIRE(out_size == 0);
    kth_core_destruct_array(out);
}

// ---------------------------------------------------------------------------
// kth::to_owned / kth::make_owned when the constructor throws after `new`
// ---------------------------------------------------------------------------
// The class-specific operator new/delete only count and forward to the
// global ones, so the real helpers run unmodified. The constructors
// throw AFTER the allocation succeeded; the counters prove the new-expression
// released the memory. (Destructor calls are not evidence: the destructor of
// an incompletely constructed object never runs.)

namespace {

struct ctor_failure {};

struct throwing_val {
    struct in_place_tag {};

    static inline int allocations = 0;
    static inline int deallocations = 0;
    static inline int copy_attempts = 0;
    static inline int move_attempts = 0;

    static void reset() { allocations = deallocations = copy_attempts = move_attempts = 0; }

    explicit throwing_val(int x) : n(x) {}
    throwing_val(throwing_val const&) { ++copy_attempts; throw ctor_failure{}; }
    throwing_val(throwing_val&&) { ++move_attempts; throw ctor_failure{}; }
    // Variadic-overload entry points: delegate to the failing copy/move.
    throwing_val(in_place_tag, throwing_val const& o) : throwing_val(o) {}
    throwing_val(in_place_tag, throwing_val&& o) : throwing_val(std::move(o)) {}

    static void* operator new(std::size_t size) { ++allocations; return ::operator new(size); }
    static void operator delete(void* p) noexcept { ++deallocations; ::operator delete(p); }

    int n;
};

} // namespace

TEST_CASE("kth::to_owned and kth::make_owned free the allocation when the constructor throws",
          "[capi helpers]") {
    throwing_val::reset();
    throwing_val src(1);
    throwing_val* out = nullptr;

    SECTION("deduced overload, copy") {
        REQUIRE_THROWS_AS(out = kth::to_owned(src), ctor_failure);
        REQUIRE(throwing_val::copy_attempts == 1);
        REQUIRE(throwing_val::move_attempts == 0);
    }
    SECTION("deduced overload, move") {
        REQUIRE_THROWS_AS(out = kth::to_owned(std::move(src)), ctor_failure);
        REQUIRE(throwing_val::copy_attempts == 0);
        REQUIRE(throwing_val::move_attempts == 1);
    }
    SECTION("explicit variadic overload, copy") {
        REQUIRE_THROWS_AS(out = kth::make_owned<throwing_val>(throwing_val::in_place_tag{}, src), ctor_failure);
        REQUIRE(throwing_val::copy_attempts == 1);
        REQUIRE(throwing_val::move_attempts == 0);
    }
    SECTION("explicit variadic overload, move") {
        REQUIRE_THROWS_AS(out = kth::make_owned<throwing_val>(throwing_val::in_place_tag{}, std::move(src)), ctor_failure);
        REQUIRE(throwing_val::copy_attempts == 0);
        REQUIRE(throwing_val::move_attempts == 1);
    }

    REQUIRE(out == nullptr);
    REQUIRE(throwing_val::allocations == 1);
    REQUIRE(throwing_val::deallocations == 1);
}

// ---------------------------------------------------------------------------
// kth::to_owned(shared_ptr) — cv/ref matrix
// ---------------------------------------------------------------------------
// Copy/move constructor counters prove exactly one constructor runs per call
// and that the pointee is moved only for a non-const rvalue shared_ptr.

namespace {

struct counted {
    static inline int copies = 0;
    static inline int moves = 0;
    static void reset() { copies = moves = 0; }

    int n;
    bool moved_from = false;

    explicit counted(int n_) : n(n_) {}
    counted(counted const& o) : n(o.n) { ++copies; }
    counted(counted&& o) noexcept : n(o.n) { ++moves; o.moved_from = true; }
};

struct move_only {
    std::unique_ptr<int> p;
    explicit move_only(int v) : p(std::make_unique<int>(v)) {}
    move_only(move_only&&) = default;
};

// Calls `to_owned(std::forward<Ptr>(ptr))` and checks the pointee was copied
// (`moved == false`) or moved (`moved == true`) with exactly one constructor
// call, and that the wrapper is untouched.
template <typename Ptr>
void check_shared_to_owned(Ptr&& ptr, bool moved) {
    auto const* before = ptr.get();
    auto const uses = ptr.use_count();
    counted::reset();

    counted* out = kth::to_owned(std::forward<Ptr>(ptr));

    REQUIRE(out != nullptr);
    REQUIRE(out != before);
    REQUIRE(out->n == 11);
    REQUIRE(counted::copies == (moved ? 0 : 1));
    REQUIRE(counted::moves == (moved ? 1 : 0));
    REQUIRE(before->moved_from == moved);
    REQUIRE(ptr.get() == before);
    REQUIRE(ptr.use_count() == uses);
    delete out;
}

template <typename Ptr>
void check_empty_shared_to_owned(Ptr&& ptr) {
    counted::reset();
    REQUIRE(kth::to_owned(std::forward<Ptr>(ptr)) == nullptr);
    REQUIRE(counted::copies == 0);
    REQUIRE(counted::moves == 0);
    REQUIRE(ptr.use_count() == 0);
}

} // namespace

#define KTH_TO_OWNED_TYPE(PTR) decltype(kth::to_owned(std::declval<PTR>()))
static_assert(std::is_same_v<KTH_TO_OWNED_TYPE(std::shared_ptr<counted>), counted*>);
static_assert(std::is_same_v<KTH_TO_OWNED_TYPE(std::shared_ptr<counted>&), counted*>);
static_assert(std::is_same_v<KTH_TO_OWNED_TYPE(std::shared_ptr<counted> const&), counted*>);
static_assert(std::is_same_v<KTH_TO_OWNED_TYPE(std::shared_ptr<counted> const), counted*>);
static_assert(std::is_same_v<KTH_TO_OWNED_TYPE(std::shared_ptr<counted const>), counted*>);
static_assert(std::is_same_v<KTH_TO_OWNED_TYPE(std::shared_ptr<counted const>&), counted*>);
static_assert(std::is_same_v<KTH_TO_OWNED_TYPE(std::shared_ptr<counted const> const&), counted*>);
static_assert(std::is_same_v<KTH_TO_OWNED_TYPE(std::shared_ptr<counted const> const), counted*>);
static_assert(std::is_same_v<KTH_TO_OWNED_TYPE(std::shared_ptr<move_only>), move_only*>);
#undef KTH_TO_OWNED_TYPE
static_assert( ! kth::not_shared_ptr<std::shared_ptr<counted> const&>);
static_assert(kth::not_shared_ptr<counted>);

TEST_CASE("kth::to_owned(shared_ptr) copies for every category except a non-const rvalue",
          "[capi helpers]") {
    auto ptr = std::make_shared<counted>(11);
    std::shared_ptr<counted> const cptr = std::make_shared<counted>(11);

    check_shared_to_owned(ptr, false);              // lvalue
    check_shared_to_owned(cptr, false);             // const lvalue
    check_shared_to_owned(std::move(cptr), false);  // const rvalue: still a copy
    check_shared_to_owned(std::move(ptr), true);    // non-const rvalue: move
}

TEST_CASE("kth::to_owned(shared_ptr<const T>) drops constness from the result",
          "[capi helpers]") {
    auto ptr = std::make_shared<counted const>(11);
    std::shared_ptr<counted const> const cptr = ptr;

    // `std::move(*ptr)` is `const T&&`, which selects the copy constructor.
    check_shared_to_owned(ptr, false);
    check_shared_to_owned(cptr, false);
    check_shared_to_owned(std::move(cptr), false);
    check_shared_to_owned(std::move(ptr), false);
}

TEST_CASE("kth::to_owned(shared_ptr) returns nullptr for an empty pointer of any category",
          "[capi helpers]") {
    std::shared_ptr<counted> empty;
    std::shared_ptr<counted> const cempty;
    std::shared_ptr<counted const> empty_const_pointee;

    check_empty_shared_to_owned(empty);
    check_empty_shared_to_owned(cempty);
    check_empty_shared_to_owned(std::move(empty));
    check_empty_shared_to_owned(std::move(cempty));
    check_empty_shared_to_owned(empty_const_pointee);
    check_empty_shared_to_owned(std::move(empty_const_pointee));
}

TEST_CASE("kth::to_owned moves a move-only pointee out of a non-const rvalue shared_ptr",
          "[capi helpers]") {
    auto ptr = std::make_shared<move_only>(7);
    auto* out = kth::to_owned(std::move(ptr));

    REQUIRE(out != nullptr);
    REQUIRE(*out->p == 7);
    // The wrapper is neither reset nor re-counted; only the pointee was moved.
    REQUIRE(ptr != nullptr);
    REQUIRE(ptr.use_count() == 1);
    REQUIRE(ptr->p == nullptr);
    delete out;
}

// ---------------------------------------------------------------------------
// kth::to_owned_if_valid / kth::make_owned_list
// ---------------------------------------------------------------------------

namespace {

struct maybe_valid {
    bool ok;
    int n;
    bool valid() const { return ok; }
};

} // namespace

TEST_CASE("kth::to_owned_if_valid promotes valid values and rejects invalid ones",
          "[capi helpers]") {
    maybe_valid good{true, 3};
    maybe_valid bad{false, 4};

    auto* a = kth::to_owned_if_valid(good);
    REQUIRE(a != nullptr);
    REQUIRE(a != &good);
    REQUIRE(a->n == 3);
    delete a;

    REQUIRE(kth::to_owned_if_valid(bad) == nullptr);

    auto* b = kth::to_owned_if_valid(maybe_valid{true, 5});
    REQUIRE(b != nullptr);
    REQUIRE(b->n == 5);
    delete b;
}

TEST_CASE("kth::to_owned_if_valid does not touch the heap for invalid input",
          "[capi helpers]") {
    throwing_val::reset();
    // Not valid -> must return before constructing: the copy ctor that would
    // throw never runs.
    struct invalid_throwing : throwing_val {
        using throwing_val::throwing_val;
        bool valid() const { return false; }
    };
    invalid_throwing src(1);
    REQUIRE(kth::to_owned_if_valid(src) == nullptr);
    REQUIRE(throwing_val::copy_attempts == 0);
    REQUIRE(throwing_val::allocations == 0);
}

TEST_CASE("kth::make_owned_list forwards every vector constructor shape",
          "[capi helpers]") {
    std::vector<int> const src{4, 5, 6};

    std::unique_ptr<std::vector<int>> empty(kth::make_owned_list<int>());
    std::unique_ptr<std::vector<int>> sized(kth::make_owned_list<int>(3));
    std::unique_ptr<std::vector<int>> filled(kth::make_owned_list<int>(2, 9));
    std::unique_ptr<std::vector<int>> listed(kth::make_owned_list<int>(std::initializer_list<int>{1, 2}));
    std::unique_ptr<std::vector<int>> ranged(kth::make_owned_list<int>(src.begin(), src.end()));

    REQUIRE(empty->empty());
    REQUIRE(sized->size() == 3);
    REQUIRE(*filled == std::vector<int>{9, 9});
    REQUIRE(*listed == std::vector<int>{1, 2});
    REQUIRE(*ranged == src);
}

#if defined(KTH_CAPI_EXCEPTION_BARRIERS)

// ---------------------------------------------------------------------------
// kth::capi_invoke — helper only
// ---------------------------------------------------------------------------

TEST_CASE("kth::capi_invoke passes through the result of a noexcept operation",
          "[kth::capi_invoke]") {
    auto success = [&]() noexcept -> kth_error_code_t { return kth_ec_success; };
    auto failure = [&]() noexcept -> kth_error_code_t { return kth_ec_invalid_size; };
    static_assert(noexcept(kth::capi_invoke(success)));
    REQUIRE(kth::capi_invoke(success) == kth_ec_success);
    REQUIRE(kth::capi_invoke(failure) == kth_ec_invalid_size);
}

TEST_CASE("kth::capi_invoke keeps the exact code of a returned domain error",
          "[kth::capi_invoke]") {
    auto failure = [&]() noexcept(false) -> kth_error_code_t { return kth_ec_invalid_size; };
    REQUIRE(kth::capi_invoke(failure) == kth_ec_invalid_size);
}

TEST_CASE("kth::capi_invoke maps thrown exceptions to the boundary codes",
          "[kth::capi_invoke]") {
    auto oom = [&]() noexcept(false) -> kth_error_code_t { throw std::bad_alloc(); };
    auto other = [&]() noexcept(false) -> kth_error_code_t { throw std::runtime_error("x"); };
    auto custom = [&]() noexcept(false) -> kth_error_code_t { throw 42; };
    REQUIRE(kth::capi_invoke(oom) == kth_ec_out_of_memory);
    REQUIRE(kth::capi_invoke(other) == kth_ec_unhandled_exception);
    REQUIRE(kth::capi_invoke(custom) == kth_ec_unhandled_exception);
}

#endif // KTH_CAPI_EXCEPTION_BARRIERS
