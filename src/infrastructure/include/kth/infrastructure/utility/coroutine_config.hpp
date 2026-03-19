// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_COROUTINE_CONFIG_HPP
#define KTH_INFRASTRUCTURE_COROUTINE_CONFIG_HPP

// =============================================================================
// Coroutine Support Detection and Configuration
// =============================================================================
//
// This header detects whether the compiler and standard library support
// C++20 coroutines and whether Asio has the required coroutine features.
//
// Requirements for KTH_USE_COROUTINES=1:
//   - C++20 or later (__cplusplus >= 202002L)
//   - Compiler coroutine support (__cpp_impl_coroutine)
//   - Asio 1.22+ (standalone) or Boost.Asio 1.22+
//
// Features required from Asio:
//   - asio::awaitable<T>
//   - asio::co_spawn
//   - asio::use_awaitable
//   - asio::experimental::channel
//   - asio::experimental::awaitable_operators
//
// =============================================================================

#include <kth/infrastructure/utility/asio_helper.hpp>

// -----------------------------------------------------------------------------
// Asio Version Detection
// -----------------------------------------------------------------------------
// Both standalone Asio and Boost.Asio define their version in the same format:
//   - ASIO_VERSION (standalone) or BOOST_ASIO_VERSION (Boost)
//   - Format: XXYYZZ where:
//       Major: VERSION / 100000
//       Minor: (VERSION / 100) % 1000
//       Patch: VERSION % 100
//   - Example: 103700 = version 1.37.0
//
// Reference: https://github.com/boostorg/asio
// -----------------------------------------------------------------------------

#if defined(KTH_ASIO_STANDALONE)
    // Standalone Asio
    #include <asio/version.hpp>
    #define KTH_ASIO_VERSION ASIO_VERSION
    #define KTH_USING_STANDALONE_ASIO 1
    #define KTH_USING_BOOST_ASIO 0
#else
    // Boost.Asio
    #include <boost/asio/version.hpp>
    #include <boost/version.hpp>
    #define KTH_ASIO_VERSION BOOST_ASIO_VERSION
    #define KTH_USING_STANDALONE_ASIO 0
    #define KTH_USING_BOOST_ASIO 1

    // Boost version for informational purposes
    #define KTH_BOOST_VERSION BOOST_VERSION
    #define KTH_BOOST_VERSION_MAJOR (BOOST_VERSION / 100000)
    #define KTH_BOOST_VERSION_MINOR ((BOOST_VERSION / 100) % 1000)
    #define KTH_BOOST_VERSION_PATCH (BOOST_VERSION % 100)
#endif

// Parse Asio version components
// Format: XXYYZZ -> Major.Minor.Patch
#define KTH_ASIO_VERSION_MAJOR (KTH_ASIO_VERSION / 100000)
#define KTH_ASIO_VERSION_MINOR ((KTH_ASIO_VERSION / 100) % 1000)
#define KTH_ASIO_VERSION_PATCH (KTH_ASIO_VERSION % 100)

// -----------------------------------------------------------------------------
// Minimum Asio Version for Coroutines
// -----------------------------------------------------------------------------
// We require Asio 1.22+ for stable coroutine support:
//   - asio::awaitable<T> (stable since 1.18)
//   - asio::co_spawn (stable since 1.18)
//   - asio::experimental::channel (since 1.21)
//   - asio::experimental::awaitable_operators (since 1.22)
//   - asio::experimental::parallel_group (since 1.22)
//   - asio::deferred (since 1.22)
//
// Version 1.22.0 = 102200 in XXYYZZ format

#define KTH_ASIO_MIN_VERSION_FOR_COROUTINES 102200  // 1.22.0

// -----------------------------------------------------------------------------
// Coroutine Support Detection
// -----------------------------------------------------------------------------

// Check C++ standard version
#if __cplusplus >= 202002L
    #define KTH_HAS_CPP20 1
#else
    #define KTH_HAS_CPP20 0
#endif

// Check compiler coroutine support
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    #define KTH_HAS_COROUTINE_SUPPORT 1
#else
    #define KTH_HAS_COROUTINE_SUPPORT 0
#endif

// Check if Asio version is sufficient
#if KTH_ASIO_VERSION >= KTH_ASIO_MIN_VERSION_FOR_COROUTINES
    #define KTH_HAS_ASIO_COROUTINES 1
#else
    #define KTH_HAS_ASIO_COROUTINES 0
#endif

// -----------------------------------------------------------------------------
// Feature Flag: KTH_USE_COROUTINES
// -----------------------------------------------------------------------------
// This is the main feature flag that enables coroutine-based networking.
// Can be:
//   1. Explicitly set by user via CMake (-DKTH_USE_COROUTINES=ON/OFF)
//   2. Auto-detected based on compiler/library support
//
// The flag is only enabled if ALL requirements are met:
//   - C++20 standard
//   - Compiler coroutine support
//   - Sufficient Asio version
//   - Not targeting Emscripten (no coroutine support in WASM yet)

#if ! defined(KTH_USE_COROUTINES)
    // Auto-detect
    #if KTH_HAS_CPP20 && KTH_HAS_COROUTINE_SUPPORT && KTH_HAS_ASIO_COROUTINES && !defined(__EMSCRIPTEN__)
        #define KTH_USE_COROUTINES 1
    #else
        #define KTH_USE_COROUTINES 0
    #endif
#endif

// Sanity check: if user explicitly enabled coroutines, verify requirements
#if KTH_USE_COROUTINES && !KTH_HAS_CPP20
    #error "KTH_USE_COROUTINES requires C++20 or later"
#endif

#if KTH_USE_COROUTINES && !KTH_HAS_COROUTINE_SUPPORT
    #error "KTH_USE_COROUTINES requires compiler coroutine support (__cpp_impl_coroutine)"
#endif

#if KTH_USE_COROUTINES && !KTH_HAS_ASIO_COROUTINES
    #error "KTH_USE_COROUTINES requires Asio 1.22+ or Boost.Asio 1.22+"
#endif

// -----------------------------------------------------------------------------
// Helper Macros for Conditional Compilation
// -----------------------------------------------------------------------------

#if KTH_USE_COROUTINES
    #define KTH_IF_COROUTINES(...) __VA_ARGS__
    #define KTH_IF_NO_COROUTINES(...)
#else
    #define KTH_IF_COROUTINES(...)
    #define KTH_IF_NO_COROUTINES(...) __VA_ARGS__
#endif

// -----------------------------------------------------------------------------
// Version Info Struct
// -----------------------------------------------------------------------------

namespace kth::infrastructure {

struct asio_version_info {
    int major;
    int minor;
    int patch;
    int raw_version;  // The raw ASIO_VERSION or BOOST_ASIO_VERSION value
    bool is_standalone;
    bool supports_coroutines;

#if KTH_USING_BOOST_ASIO
    int boost_major;
    int boost_minor;
    int boost_patch;
#endif
};

constexpr asio_version_info get_asio_version_info() {
    return {
        KTH_ASIO_VERSION_MAJOR,
        KTH_ASIO_VERSION_MINOR,
        KTH_ASIO_VERSION_PATCH,
        KTH_ASIO_VERSION,
        KTH_USING_STANDALONE_ASIO != 0,
        KTH_HAS_ASIO_COROUTINES != 0
#if KTH_USING_BOOST_ASIO
        , KTH_BOOST_VERSION_MAJOR
        , KTH_BOOST_VERSION_MINOR
        , KTH_BOOST_VERSION_PATCH
#endif
    };
}

constexpr bool coroutines_enabled() {
    return KTH_USE_COROUTINES != 0;
}

} // namespace kth::infrastructure

#endif // KTH_INFRASTRUCTURE_COROUTINE_CONFIG_HPP
