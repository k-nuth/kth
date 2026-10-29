// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// `noexcept` contracts of the generic C-API helpers used by generated
// wrappers. Positive checks pin what is provably non-throwing; negative checks
// use throwing fixtures (and allocating helpers) so a generic helper can never
// silently swallow a user conversion's exception specification.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <kth/capi/error.h>
#include <kth/capi/helpers.hpp>
#include <kth/capi/conversions.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/machine/interpreter.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

namespace {

enum class small_enum : int { a = 1, b = 2 };

struct thrown {};

// Converts to the target types, but throws.
struct throwing_conversion {
    operator std::size_t() const { throw thrown{}; }
    operator small_enum() const { throw thrown{}; }
    operator kth_error_code_t() const { throw thrown{}; }
};

// Conversion does not throw; copying does.
struct throwing_copy {
    throwing_copy() = default;
    throwing_copy(throwing_copy const&) { throw thrown{}; }
    operator std::size_t() const noexcept { return 7; }
};

// Conversion declared noexcept: the conditional specification must follow it.
struct nothrow_conversion {
    operator std::size_t() const noexcept { return 3; }
    operator small_enum() const noexcept { return small_enum::b; }
    operator kth_error_code_t() const noexcept { return kth_ec_success; }
};

// Trivially copyable, standard layout, but with a throwing default constructor.
struct throwing_default {
    int v;
    throwing_default() noexcept(false) : v(0) {}
};
struct throwing_default_twin {
    int v;
    throwing_default_twin() noexcept(false) : v(0) {}
};

struct pod_a { std::uint32_t v; };
struct pod_b { std::uint32_t v; };

struct overloaded_address {
    overloaded_address* operator&() { throw thrown{}; }
};

struct c_hash_like { std::uint8_t hash[4]; };

struct throwing_byte {
    operator std::uint8_t() const noexcept(false) { throw thrown{}; }
};
struct c_byte1 { std::uint8_t bytes[1]; };

// Trivially copyable, so `struct_cast` accepts it, but taking its address
// through the overloaded operator would throw.
struct odd_pod {
    int n;
    odd_pod* operator&() noexcept(false) { throw thrown{}; }
    odd_pod const* operator&() const noexcept(false) { throw thrown{}; }
};

// Copy does not throw, move does: a named-local return could pick the move.
struct c_throwing_move {
    std::uint8_t bytes[1];
    c_throwing_move() noexcept = default;
    c_throwing_move(c_throwing_move const&) noexcept = default;
    c_throwing_move(c_throwing_move&&) noexcept(false) {}
};

// A non-`std::array` container whose iterators throw although `begin`/`end`
// are `noexcept`.
struct throwing_iter {
    std::uint8_t const* p;
    std::uint8_t operator*() const noexcept { return *p; }
    throwing_iter& operator++() noexcept { ++p; return *this; }
    throwing_iter operator++(int) noexcept { auto r = *this; ++p; return r; }
    bool operator==(throwing_iter const&) const noexcept(false) { throw thrown{}; }
    using iterator_category = std::input_iterator_tag;
    using value_type = std::uint8_t;
    using difference_type = std::ptrdiff_t;
    using pointer = std::uint8_t const*;
    using reference = std::uint8_t;
};
struct throwing_iter_array {
    std::uint8_t data_[1] = {1};
    throwing_iter begin() const noexcept { return {data_}; }
    throwing_iter end() const noexcept { return {data_ + 1}; }
};

} // namespace

template <>
struct std::tuple_size<throwing_iter_array> : std::integral_constant<std::size_t, 1> {};

// ---------------------------------------------------------------------------
// Handle access, scalars and error mapping: unconditionally non-throwing
// ---------------------------------------------------------------------------

static_assert(noexcept(kth::cpp_ref<int>(std::declval<void*>())));
static_assert(noexcept(kth::cpp_ref<int>(std::declval<void const*>())));
static_assert(noexcept(kth::list_ref<int>(std::declval<void*>())));
static_assert(noexcept(kth::list_ref<int>(std::declval<void const*>())));
static_assert(noexcept(kth::bool_to_int(true)));
static_assert(noexcept(kth::int_to_bool(2)));
static_assert(noexcept(kth::to_c_err(std::declval<std::error_code const&>())));
static_assert(noexcept(kth::to_c_err(kth::error::success)));
static_assert(noexcept(kth::take<int>(std::declval<void**>())));

// `optional_cpp_ref` copies the value: it stays potentially throwing.
static_assert( ! noexcept(kth::optional_cpp_ref<int>(std::declval<void const*>())));

// ---------------------------------------------------------------------------
// `sz`, `ecast`, `to_c_err(E)`, `ref_to_c`: conditional on the conversion
// ---------------------------------------------------------------------------

static_assert(noexcept(kth::sz(1)));
static_assert(noexcept(kth::sz(std::uint32_t{1})));
static_assert(noexcept(kth::sz(std::int64_t{1})));
static_assert(noexcept(kth::sz(small_enum::a)));
static_assert(noexcept(kth::sz(nothrow_conversion{})));
static_assert( ! noexcept(kth::sz(throwing_conversion{})));
static_assert( ! noexcept(kth::sz(std::declval<throwing_copy&>())));

static_assert(noexcept(kth::ecast<small_enum>(1)));
static_assert(noexcept(kth::ecast<int>(small_enum::a)));
static_assert(noexcept(kth::ecast<small_enum>(nothrow_conversion{})));
static_assert( ! noexcept(kth::ecast<small_enum>(throwing_conversion{})));

static_assert(noexcept(kth::to_c_err(nothrow_conversion{})));
static_assert( ! noexcept(kth::to_c_err(throwing_conversion{})));

static_assert(noexcept(kth::ref_to_c(std::declval<int&>())));
static_assert(noexcept(kth::ref_to_c(std::declval<int const&>())));
static_assert( ! noexcept(kth::ref_to_c(std::declval<overloaded_address&>())));

// ---------------------------------------------------------------------------
// Value conversions
// ---------------------------------------------------------------------------

static_assert(noexcept(kth::struct_cast<pod_a>(std::declval<pod_b const&>())));
static_assert(noexcept(kth::to_c_struct<pod_a>(std::declval<pod_b const&>())));
static_assert(noexcept(kth::from_c_struct<pod_a>(std::declval<pod_b const&>())));
static_assert( ! noexcept(kth::struct_cast<throwing_default>(std::declval<throwing_default_twin const&>())));
static_assert( ! noexcept(kth::to_c_struct<throwing_default>(std::declval<throwing_default_twin const&>())));
static_assert( ! noexcept(kth::from_c_struct<throwing_default>(std::declval<throwing_default_twin const&>())));

static_assert(noexcept(kth::to_array_cpp<4>(std::declval<std::uint8_t const*>())));
static_assert(noexcept(kth::to_c_value_array<c_hash_like, &c_hash_like::hash>(std::declval<std::array<std::uint8_t, 4> const&>())));

// Exact safe case is non-throwing; everything else keeps the generic,
// unannotated path (throwing element conversion, throwing iterators, a C type
// whose move throws).
static_assert(noexcept(kth::to_c_value_array<c_byte1, &c_byte1::bytes>(std::declval<std::array<std::uint8_t, 1> const&>())));
static_assert( ! noexcept(kth::to_c_value_array<c_byte1, &c_byte1::bytes>(std::declval<std::array<throwing_byte, 1> const&>())));
static_assert( ! noexcept(kth::to_c_value_array<c_byte1, &c_byte1::bytes>(std::declval<throwing_iter_array const&>())));
static_assert( ! noexcept(kth::to_c_value_array<c_throwing_move, &c_throwing_move::bytes>(std::declval<std::array<std::uint8_t, 1> const&>())));
// `struct_cast` takes addresses with std::addressof, so an overloaded
// operator& neither runs nor changes the specification.
static_assert(noexcept(kth::struct_cast<odd_pod>(std::declval<odd_pod const&>())));
static_assert( ! noexcept(kth::struct_cast<c_throwing_move>(std::declval<c_throwing_move const&>())));

// The complete expression of a real generated wrapper
// (`kth_vm_debug_snapshot_last`): handle access, a plain enum field and error
// mapping are all non-throwing, so its operation lambda takes the barrier's
// zero-cost path. The `check*`/`evaluate` wrappers call methods without a
// `noexcept` guarantee and stay on the barrier.
using debug_snapshot_t = kth::domain::machine::debug_snapshot;
static_assert(noexcept(kth::to_c_err(kth::cpp_ref<debug_snapshot_t>(std::declval<void const*>()).last.error)));

// Generated concrete conversions.
static_assert(noexcept(kth::hash_to_cpp(std::declval<std::uint8_t const*>())));
static_assert(noexcept(kth::to_hash_t(std::declval<kth::hash_digest const&>())));
static_assert(noexcept(kth::short_hash_to_cpp(std::declval<std::uint8_t const*>())));
static_assert(noexcept(kth::to_shorthash_t(std::declval<kth::short_hash const&>())));
static_assert(noexcept(kth::point_kind_to_cpp(kth_point_kind_spend)));
static_assert(noexcept(kth::point_kind_to_c(kth::domain::chain::point_kind::spend)));
static_assert(noexcept(kth::network_to_cpp(kth_network_mainnet)));
static_assert(noexcept(kth::network_to_c(kth::domain::config::network::mainnet)));
static_assert(noexcept(kth::opcode_to_cpp(kth_opcode_push_size_0)));
static_assert(noexcept(kth::opcode_to_c(kth::domain::machine::opcode::push_size_0)));
static_assert(noexcept(kth::witness()));

// ---------------------------------------------------------------------------
// Allocating helpers and parsing remain potentially throwing
// ---------------------------------------------------------------------------

static_assert( ! noexcept(kth::make_owned<int>(1)));
static_assert( ! noexcept(kth::make_owned<std::string>("x")));
static_assert( ! noexcept(kth::make_owned_list<int>(std::size_t{3})));
static_assert( ! noexcept(kth::to_owned(1)));
static_assert( ! noexcept(kth::to_owned(std::string{})));
static_assert( ! noexcept(kth::to_owned(std::shared_ptr<int>{})));
static_assert( ! noexcept(kth::to_owned(std::shared_ptr<int const>{})));
static_assert( ! noexcept(kth::to_owned_if_valid(1)));
static_assert( ! noexcept(kth::to_owned_if_success(1, std::error_code{})));
static_assert( ! noexcept(kth::clone<int>(std::declval<void const*>())));
static_assert( ! noexcept(kth::create_c_array(std::declval<kth::data_chunk const&>(), std::declval<std::size_t&>())));
static_assert( ! noexcept(kth::create_c_str(std::declval<std::string const&>())));

static_assert( ! noexcept(kth::domain::chain::transaction::from_data(std::declval<kth::byte_reader&>(), true)));

// ---------------------------------------------------------------------------
// Runtime: a throwing conversion really escapes a helper that is not noexcept
// ---------------------------------------------------------------------------

TEST_CASE("noexcept contracts: throwing user conversions are not swallowed", "[capi][noexcept]") {
    CHECK_THROWS_AS(kth::sz(throwing_conversion{}), thrown);
    CHECK_THROWS_AS(kth::ecast<small_enum>(throwing_conversion{}), thrown);
    CHECK_THROWS_AS(kth::to_c_err(throwing_conversion{}), thrown);
    throwing_copy copyable;
    CHECK_THROWS_AS(kth::sz(copyable), thrown);
}

TEST_CASE("noexcept contracts: throwing value conversions propagate instead of terminating", "[capi][noexcept]") {
    std::array<throwing_byte, 1> bad_elements{};
    CHECK_THROWS_AS((kth::to_c_value_array<c_byte1, &c_byte1::bytes>(bad_elements)), thrown);
    throwing_iter_array bad_iterators;
    CHECK_THROWS_AS((kth::to_c_value_array<c_byte1, &c_byte1::bytes>(bad_iterators)), thrown);
}

TEST_CASE("noexcept contracts: struct_cast ignores a throwing operator&", "[capi][noexcept]") {
    odd_pod in{42};
    odd_pod out{};
    CHECK_NOTHROW(out = kth::struct_cast<odd_pod>(in));
    CHECK(out.n == 42);
    std::array<std::uint8_t, 1> ok{{9}};
    CHECK(kth::to_c_value_array<c_byte1, &c_byte1::bytes>(ok).bytes[0] == 9);
}

TEST_CASE("noexcept contracts: non-throwing helpers keep their values", "[capi][noexcept]") {
    int value = 5;
    CHECK(&kth::cpp_ref<int>(&value) == &value);
    CHECK(kth::sz(nothrow_conversion{}) == 3);
    CHECK(kth::ecast<small_enum>(nothrow_conversion{}) == small_enum::b);
    CHECK(kth::to_c_err(nothrow_conversion{}) == kth_ec_success);
    CHECK(kth::bool_to_int(true) == 1);
    CHECK(kth::int_to_bool(0) == false);
    CHECK(kth::int_to_bool(-3) == true);
    CHECK(kth::to_c_err(std::error_code{}) == kth_ec_success);
    CHECK(kth::sz(std::int32_t{12}) == 12);
}

TEST_CASE("noexcept contracts: owned allocations are released by RAII", "[capi][noexcept]") {
    auto* raw = kth::make_owned<int>(9);
    void* handle = raw;
    auto owned = kth::take<int>(&handle);
    CHECK(handle == nullptr);
    CHECK(owned.get() == raw);
    CHECK(*owned == 9);
}

#if defined(KTH_CAPI_EXCEPTION_BARRIERS)

// ---------------------------------------------------------------------------
// Runtime: the barrier catches what a throwing conversion raises, and a
// provably non-throwing operation takes the zero-barrier path.
// ---------------------------------------------------------------------------

TEST_CASE("noexcept contracts: barrier maps a throwing conversion", "[capi][noexcept][barrier]") {
    auto throwing = [&]() noexcept(noexcept(kth::to_c_err(throwing_conversion{}))) -> kth_error_code_t {
        return kth::to_c_err(throwing_conversion{});
    };
    static_assert( ! noexcept(throwing()));
    CHECK_THROWS_AS(throwing(), thrown);
    CHECK(kth::capi_invoke(throwing) == kth_ec_unhandled_exception);

    auto allocating = [&]() noexcept(false) -> kth_error_code_t {
        throw std::bad_alloc{};
    };
    CHECK(kth::capi_invoke(allocating) == kth_ec_out_of_memory);
}

TEST_CASE("noexcept contracts: barrier fast path for a provably non-throwing operation", "[capi][noexcept][barrier]") {
    int value = 5;
    void* handle = &value;
    auto operation = [&]() noexcept(noexcept(kth::to_c_err(std::error_code{}))) -> kth_error_code_t {
        return kth::to_c_err(std::error_code{});
    };
    static_assert(noexcept(operation()));
    CHECK(kth::capi_invoke(operation) == kth_ec_success);

    auto nothrow_operation = [&]() noexcept(noexcept(kth::to_c_err(nothrow_conversion{}))) -> kth_error_code_t {
        return kth::to_c_err(nothrow_conversion{});
    };
    static_assert(noexcept(nothrow_operation()));
    CHECK(kth::capi_invoke(nothrow_operation) == kth_ec_success);
    CHECK(kth::cpp_ref<int>(handle) == 5);
}

TEST_CASE("noexcept contracts: real wrapper expression takes the fast path", "[capi][noexcept][barrier]") {
    debug_snapshot_t snapshot;
    snapshot.last.error = kth::error::success;
    void const* handle = &snapshot;
    auto operation = [&]() noexcept(noexcept(kth::to_c_err(kth::cpp_ref<debug_snapshot_t>(handle).last.error))) -> kth_error_code_t {
        return kth::to_c_err(kth::cpp_ref<debug_snapshot_t>(handle).last.error);
    };
    static_assert(noexcept(operation()));
    CHECK(kth::capi_invoke(operation) == kth_ec_success);
}

#endif // KTH_CAPI_EXCEPTION_BARRIERS
