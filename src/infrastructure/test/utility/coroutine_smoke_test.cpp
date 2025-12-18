// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_all.hpp>

#include <kth/infrastructure/utility/coroutine_config.hpp>

#include <string>
#include <sstream>

// =============================================================================
// Asio Version Detection Tests
// =============================================================================

TEST_CASE("asio version detection", "[coroutines][version]") {
    auto const info = kth::infrastructure::get_asio_version_info();

    SECTION("version is detected") {
        // We should have a non-zero version if Asio is properly included
        INFO("Asio version: " << info.major << "." << info.minor << "." << info.patch);
        INFO("Asio raw version: " << info.raw_version);
        INFO("Is standalone: " << (info.is_standalone ? "yes" : "no"));
        INFO("Supports coroutines: " << (info.supports_coroutines ? "yes" : "no"));

        // At minimum, we should have Asio 1.x
        CHECK(info.major >= 1);
        // Raw version should be >= 100000 (1.0.0)
        CHECK(info.raw_version >= 100000);
    }

    SECTION("version macros are consistent") {
        CHECK(info.major == KTH_ASIO_VERSION_MAJOR);
        CHECK(info.minor == KTH_ASIO_VERSION_MINOR);
        CHECK(info.patch == KTH_ASIO_VERSION_PATCH);
        CHECK(info.raw_version == KTH_ASIO_VERSION);
    }

#if KTH_USING_BOOST_ASIO
    SECTION("boost version is detected") {
        INFO("Boost version: " << info.boost_major << "." << info.boost_minor << "." << info.boost_patch);
        INFO("BOOST_ASIO_VERSION: " << BOOST_ASIO_VERSION);

        // We expect Boost 1.80+ for coroutine support
        CHECK(info.boost_major >= 1);

        // If using Boost 1.89, Asio should be >= 1.35
        if (info.boost_major == 1 && info.boost_minor >= 89) {
            CHECK(info.major == 1);
            CHECK(info.minor >= 35);
        }
    }
#endif

#if KTH_USING_STANDALONE_ASIO
    SECTION("standalone asio is detected") {
        CHECK(info.is_standalone == true);
    }
#endif
}

TEST_CASE("coroutine support detection", "[coroutines][detection]") {
    SECTION("C++20 detection") {
        INFO("__cplusplus = " << __cplusplus);
        INFO("KTH_HAS_CPP20 = " << KTH_HAS_CPP20);

        // We compile with C++23, so this should be true
        CHECK(KTH_HAS_CPP20 == 1);
    }

    SECTION("compiler coroutine support") {
        INFO("KTH_HAS_COROUTINE_SUPPORT = " << KTH_HAS_COROUTINE_SUPPORT);

        #if defined(__cpp_impl_coroutine)
        INFO("__cpp_impl_coroutine = " << __cpp_impl_coroutine);
        CHECK(KTH_HAS_COROUTINE_SUPPORT == 1);
        #else
        INFO("__cpp_impl_coroutine is not defined");
        #endif
    }

    SECTION("asio coroutine features") {
        INFO("KTH_ASIO_VERSION = " << KTH_ASIO_VERSION);
        INFO("KTH_ASIO_MIN_VERSION_FOR_COROUTINES = " << KTH_ASIO_MIN_VERSION_FOR_COROUTINES);
        INFO("KTH_HAS_ASIO_COROUTINES = " << KTH_HAS_ASIO_COROUTINES);

        // With Boost 1.89 or standalone Asio 1.36, we should have coroutine support
        auto const info = kth::infrastructure::get_asio_version_info();
        if (info.major == 1 && info.minor >= 22) {
            CHECK(KTH_HAS_ASIO_COROUTINES == 1);
        }
    }

    SECTION("final coroutine flag") {
        INFO("KTH_USE_COROUTINES = " << KTH_USE_COROUTINES);
        INFO("coroutines_enabled() = " << kth::infrastructure::coroutines_enabled());

        CHECK(kth::infrastructure::coroutines_enabled() == (KTH_USE_COROUTINES != 0));

        // With C++23, modern compilers, and Asio 1.36/Boost 1.89, coroutines should be enabled
        #if !defined(__EMSCRIPTEN__)
        // Only check if not targeting WebAssembly
        auto const info = kth::infrastructure::get_asio_version_info();
        if (KTH_HAS_CPP20 && KTH_HAS_COROUTINE_SUPPORT && info.supports_coroutines) {
            CHECK(KTH_USE_COROUTINES == 1);
        }
        #endif
    }
}

// =============================================================================
// Coroutine Smoke Tests (only compiled when coroutines are enabled)
// =============================================================================

#if KTH_USE_COROUTINES

#include <coroutine>
#include <kth/infrastructure/utility/asio_helper.hpp>

// Include the experimental headers for coroutines
#if KTH_USING_STANDALONE_ASIO
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#else
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#endif

namespace {

// Simple coroutine that returns a value
asio::awaitable<int> simple_coro() {
    co_return 42;
}

// Coroutine that uses a timer
asio::awaitable<int> timer_coro() {
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(1));
    co_await timer.async_wait(asio::use_awaitable);
    co_return 123;
}

// Coroutine that uses as_tuple for error handling
asio::awaitable<bool> error_handling_coro() {
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(1));

    auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
    co_return !ec;  // true if no error
}

// Coroutine using awaitable_operators (|| for racing)
asio::awaitable<int> racing_coro() {
    using namespace asio::experimental::awaitable_operators;

    auto executor = co_await asio::this_coro::executor;

    asio::steady_timer timer1(executor);
    asio::steady_timer timer2(executor);

    timer1.expires_after(std::chrono::milliseconds(1));
    timer2.expires_after(std::chrono::milliseconds(100));

    // Race: first timer to complete wins
    auto result = co_await (
        timer1.async_wait(asio::use_awaitable) ||
        timer2.async_wait(asio::use_awaitable)
    );

    // result.index() == 0 means timer1 won (expected)
    co_return static_cast<int>(result.index());
}

// Coroutine using awaitable_operators (&& for parallel)
asio::awaitable<int> parallel_coro() {
    using namespace asio::experimental::awaitable_operators;

    auto executor = co_await asio::this_coro::executor;

    asio::steady_timer timer1(executor);
    asio::steady_timer timer2(executor);

    timer1.expires_after(std::chrono::milliseconds(1));
    timer2.expires_after(std::chrono::milliseconds(2));

    // Wait for both timers to complete
    co_await (
        timer1.async_wait(asio::use_awaitable) &&
        timer2.async_wait(asio::use_awaitable)
    );

    co_return 999;  // Both completed
}

} // anonymous namespace

TEST_CASE("coroutine smoke tests", "[coroutines][smoke]") {
    asio::io_context ctx;

    SECTION("simple coroutine returns value") {
        int result = 0;
        asio::co_spawn(ctx, simple_coro(), [&](std::exception_ptr ep, int value) {
            REQUIRE_FALSE(ep);
            result = value;
        });
        ctx.run();
        CHECK(result == 42);
    }

    SECTION("timer coroutine works") {
        int result = 0;
        asio::co_spawn(ctx, timer_coro(), [&](std::exception_ptr ep, int value) {
            REQUIRE_FALSE(ep);
            result = value;
        });
        ctx.run();
        CHECK(result == 123);
    }

    SECTION("error handling with as_tuple works") {
        bool result = false;
        asio::co_spawn(ctx, error_handling_coro(), [&](std::exception_ptr ep, bool value) {
            REQUIRE_FALSE(ep);
            result = value;
        });
        ctx.run();
        CHECK(result == true);
    }

    SECTION("racing with || operator works") {
        int result = -1;
        asio::co_spawn(ctx, racing_coro(), [&](std::exception_ptr ep, int value) {
            REQUIRE_FALSE(ep);
            result = value;
        });
        ctx.run();
        CHECK(result == 0);  // First timer should win
    }

    SECTION("parallel with && operator works") {
        int result = 0;
        asio::co_spawn(ctx, parallel_coro(), [&](std::exception_ptr ep, int value) {
            REQUIRE_FALSE(ep);
            result = value;
        });
        ctx.run();
        CHECK(result == 999);
    }
}

TEST_CASE("coroutine exception handling", "[coroutines][exceptions]") {
    asio::io_context ctx;

    SECTION("exceptions propagate correctly") {
        auto throwing_coro = []() -> asio::awaitable<int> {
            throw std::runtime_error("test exception");
            co_return 0;  // Never reached
        };

        bool exception_caught = false;
        asio::co_spawn(ctx, throwing_coro(), [&](std::exception_ptr ep, int) {
            if (ep) {
                exception_caught = true;
                try {
                    std::rethrow_exception(ep);
                } catch (std::runtime_error const& e) {
                    CHECK(std::string(e.what()) == "test exception");
                }
            }
        });
        ctx.run();
        CHECK(exception_caught);
    }
}

#else // !KTH_USE_COROUTINES

TEST_CASE("coroutines disabled", "[coroutines][disabled]") {
    SECTION("KTH_USE_COROUTINES is 0") {
        CHECK(KTH_USE_COROUTINES == 0);
        CHECK(kth::infrastructure::coroutines_enabled() == false);

        // Log why coroutines are disabled
        INFO("KTH_HAS_CPP20 = " << KTH_HAS_CPP20);
        INFO("KTH_HAS_COROUTINE_SUPPORT = " << KTH_HAS_COROUTINE_SUPPORT);
        INFO("KTH_HAS_ASIO_COROUTINES = " << KTH_HAS_ASIO_COROUTINES);

        #if defined(__EMSCRIPTEN__)
        INFO("Reason: Emscripten target (WASM doesn't support coroutines yet)");
        #elif !KTH_HAS_CPP20
        INFO("Reason: C++20 not enabled");
        #elif !KTH_HAS_COROUTINE_SUPPORT
        INFO("Reason: Compiler doesn't support coroutines");
        #elif !KTH_HAS_ASIO_COROUTINES
        INFO("Reason: Asio version too old (need 1.22+)");
        #endif
    }
}

#endif // KTH_USE_COROUTINES
