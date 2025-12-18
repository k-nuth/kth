// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_COMPILER_CONSTRAINTS_HPP
#define KTH_INFRASTRUCTURE_COMPILER_CONSTRAINTS_HPP

// =============================================================================
// Compiler Constraints and Requirements
// =============================================================================
//
// This header enforces compile-time constraints on the compiler and standard
// library to ensure the project builds correctly.
//
// Requirements:
//   - C++23 or later
//   - Coroutine support (__cpp_impl_coroutine >= 201902L)
//   - std::expected support (__cpp_lib_expected >= 202202L)
//   - Concepts support (__cpp_concepts >= 202002L)
//
// Compiler minimums:
//   - GCC 13+
//   - Clang 17+
//   - MSVC 19.36+ (VS 2022 17.6+)
//
// =============================================================================

// -----------------------------------------------------------------------------
// C++ Standard Version
// -----------------------------------------------------------------------------

#if __cplusplus < 202302L
    #error "KTH requires C++23 or later. Please use -std=c++23 or equivalent."
#endif

// -----------------------------------------------------------------------------
// Coroutine Support
// -----------------------------------------------------------------------------

#if !defined(__cpp_impl_coroutine) || __cpp_impl_coroutine < 201902L
    #error "KTH requires coroutine support (__cpp_impl_coroutine >= 201902L). " \
           "Please use a compiler with C++20 coroutine support."
#endif

// -----------------------------------------------------------------------------
// std::expected Support (C++23)
// -----------------------------------------------------------------------------

// Note: __cpp_lib_expected requires <expected> or <version> to be included first.
// We check it after including the header below.

// -----------------------------------------------------------------------------
// Concepts Support
// -----------------------------------------------------------------------------

#if !defined(__cpp_concepts) || __cpp_concepts < 202002L
    #error "KTH requires concepts support (__cpp_concepts >= 202002L). " \
           "Please use a C++20 compliant compiler."
#endif

// -----------------------------------------------------------------------------
// Compiler Version Checks
// -----------------------------------------------------------------------------

#if defined(__GNUC__) && !defined(__clang__)
    // GCC
    #if __GNUC__ < 13
        #error "KTH requires GCC 13 or later for full C++23 support."
    #endif
#elif defined(__clang__)
    // Clang
    #if __clang_major__ < 17
        #error "KTH requires Clang 17 or later for full C++23 support."
    #endif
#elif defined(_MSC_VER)
    // MSVC
    #if _MSC_VER < 1936
        #error "KTH requires MSVC 19.36 (VS 2022 17.6) or later for full C++23 support."
    #endif
#endif

// -----------------------------------------------------------------------------
// WebAssembly / Emscripten Constraints
// -----------------------------------------------------------------------------

#if defined(__EMSCRIPTEN__)
    // Coroutines are not yet fully supported in Emscripten/WebAssembly
    #warning "WebAssembly builds may have limited coroutine support."
#endif

// -----------------------------------------------------------------------------
// Standard Library Headers (must be included before validation)
// -----------------------------------------------------------------------------
// Order matters: <compare> must come before <coroutine> on some implementations

#include <compare>
#include <concepts>
#include <coroutine>
#include <expected>
#include <type_traits>

// -----------------------------------------------------------------------------
// Library Feature Checks (after including headers)
// -----------------------------------------------------------------------------

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
    #error "KTH requires std::expected support (__cpp_lib_expected >= 202202L). " \
           "Please use a C++23 compliant standard library."
#endif

// -----------------------------------------------------------------------------
// Compile-Time Validation
// -----------------------------------------------------------------------------

namespace kth::infrastructure::constraints {
namespace detail {

// Test coroutine support
struct test_coroutine_promise {
    test_coroutine_promise() = default;
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {}
    struct test_coro {
        using promise_type = test_coroutine_promise;
    };
    test_coro get_return_object() noexcept { return {}; }
};

// Test std::expected support
inline void test_expected() {
    [[maybe_unused]] std::expected<int, int> e = 42;
    static_assert(std::is_same_v<decltype(e.value()), int&>);
}

// Test concepts support
template <typename T>
concept testable_concept = requires(T t) {
    { t } -> std::convertible_to<T>;
};

static_assert(testable_concept<int>, "Concepts must work");

} // namespace detail

// -----------------------------------------------------------------------------
// Feature Summary (constexpr for runtime queries if needed)
// -----------------------------------------------------------------------------

struct compiler_info {
    static constexpr int cpp_standard = __cplusplus;
    static constexpr long coroutine_version = __cpp_impl_coroutine;
    static constexpr long expected_version = __cpp_lib_expected;
    static constexpr long concepts_version = __cpp_concepts;

#if defined(__GNUC__) && !defined(__clang__)
    static constexpr char const* compiler_name = "GCC";
    static constexpr int compiler_major = __GNUC__;
    static constexpr int compiler_minor = __GNUC_MINOR__;
#elif defined(__clang__)
    static constexpr char const* compiler_name = "Clang";
    static constexpr int compiler_major = __clang_major__;
    static constexpr int compiler_minor = __clang_minor__;
#elif defined(_MSC_VER)
    static constexpr char const* compiler_name = "MSVC";
    static constexpr int compiler_major = _MSC_VER / 100;
    static constexpr int compiler_minor = _MSC_VER % 100;
#else
    static constexpr char const* compiler_name = "Unknown";
    static constexpr int compiler_major = 0;
    static constexpr int compiler_minor = 0;
#endif
};

} // namespace kth::infrastructure::constraints

#endif // KTH_INFRASTRUCTURE_COMPILER_CONSTRAINTS_HPP
