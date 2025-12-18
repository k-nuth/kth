// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <kth/infrastructure/utility/awaitable_helpers.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>

#if defined(KTH_ASIO_STANDALONE)
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#else
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#endif

using namespace kth;
using namespace std::chrono_literals;

// Helper to run a coroutine and get the result
template <typename T>
T run_coro(::asio::io_context& ctx, awaitable<T> coro) {
    T result{};
    ::asio::co_spawn(ctx, std::move(coro), [&](std::exception_ptr ep, T value) {
        if (ep) std::rethrow_exception(ep);
        result = std::move(value);
    });
    ctx.run();
    return result;
}

// Overload for void
inline void run_coro(::asio::io_context& ctx, awaitable<void> coro) {
    ::asio::co_spawn(ctx, std::move(coro), [](std::exception_ptr ep) {
        if (ep) std::rethrow_exception(ep);
    });
    ctx.run();
}

// =============================================================================
// delay() tests
// =============================================================================

TEST_CASE("delay basic functionality", "[awaitable_helpers][delay]") {
    ::asio::io_context ctx;

    SECTION("delay completes after specified duration") {
        auto start = std::chrono::steady_clock::now();

        run_coro(ctx, delay(50ms));

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        // Should take at least 50ms (allow some tolerance)
        CHECK(ms >= 45);
        CHECK(ms < 200);  // But not too long
    }

    SECTION("delay with different duration types") {
        auto start = std::chrono::steady_clock::now();

        run_coro(ctx, delay(std::chrono::microseconds(50000)));  // 50ms

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        CHECK(ms >= 45);
    }
}

// =============================================================================
// with_timeout() tests
// =============================================================================

namespace {

awaitable<int> slow_operation(std::chrono::milliseconds duration) {
    co_await delay(duration);
    co_return 42;
}

awaitable<int> fast_operation() {
    co_await delay(10ms);
    co_return 123;
}

} // anonymous namespace

TEST_CASE("with_timeout success case", "[awaitable_helpers][timeout]") {
    ::asio::io_context ctx;

    SECTION("operation completes before timeout") {
        auto test_coro = []() -> awaitable<std::expected<int, std::error_code>> {
            co_return co_await with_timeout(fast_operation(), 500ms);
        };

        auto result = run_coro(ctx, test_coro());

        REQUIRE(result.has_value());
        CHECK(result.value() == 123);
    }
}

TEST_CASE("with_timeout timeout case", "[awaitable_helpers][timeout]") {
    ::asio::io_context ctx;

    SECTION("operation times out") {
        auto test_coro = []() -> awaitable<std::expected<int, std::error_code>> {
            co_return co_await with_timeout(slow_operation(500ms), 50ms);
        };

        auto result = run_coro(ctx, test_coro());

        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == make_error_code(std::errc::timed_out));
    }
}

// =============================================================================
// with_timeout with expected return type
// =============================================================================

namespace {

enum class custom_error {
    none = 0,
    timeout,
    other
};

std::error_code make_error_code(custom_error e) {
    static struct : std::error_category {
        char const* name() const noexcept override { return "custom"; }
        std::string message(int ev) const override {
            switch (static_cast<custom_error>(ev)) {
                case custom_error::none: return "none";
                case custom_error::timeout: return "timeout";
                case custom_error::other: return "other";
                default: return "unknown";
            }
        }
    } category;
    return {static_cast<int>(e), category};
}

awaitable<std::expected<std::string, custom_error>> expected_operation(bool succeed) {
    co_await delay(10ms);
    if (succeed) {
        co_return "success";
    }
    co_return std::unexpected(custom_error::other);
}

} // anonymous namespace

TEST_CASE("with_timeout with expected return", "[awaitable_helpers][timeout][expected]") {
    ::asio::io_context ctx;

    SECTION("successful operation with custom timeout error") {
        auto test_coro = []() -> awaitable<std::expected<std::string, custom_error>> {
            co_return co_await with_timeout(
                expected_operation(true),
                500ms,
                custom_error::timeout
            );
        };

        auto result = run_coro(ctx, test_coro());

        REQUIRE(result.has_value());
        CHECK(result.value() == "success");
    }

    SECTION("operation error propagates") {
        auto test_coro = []() -> awaitable<std::expected<std::string, custom_error>> {
            co_return co_await with_timeout(
                expected_operation(false),
                500ms,
                custom_error::timeout
            );
        };

        auto result = run_coro(ctx, test_coro());

        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == custom_error::other);
    }
}

// =============================================================================
// post_to() tests
// =============================================================================

TEST_CASE("post_to switches executor", "[awaitable_helpers][post_to]") {
    threadpool pool1(2);
    threadpool pool2(2);

    ::asio::io_context ctx;

    SECTION("can post to different executor") {
        std::atomic<bool> executed_on_pool{false};

        auto test_coro = [&]() -> awaitable<void> {
            co_await post_to(pool1.get_executor());
            executed_on_pool = true;
        };

        ::asio::co_spawn(ctx, test_coro(), ::asio::detached);
        ctx.run();

        // Give pool time to execute
        std::this_thread::sleep_for(50ms);

        pool1.stop();
        pool1.join();
        pool2.stop();
        pool2.join();

        CHECK(executed_on_pool.load());
    }
}
