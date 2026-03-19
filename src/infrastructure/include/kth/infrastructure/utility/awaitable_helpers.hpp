// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_AWAITABLE_HELPERS_HPP
#define KTH_INFRASTRUCTURE_AWAITABLE_HELPERS_HPP

#if defined(__EMSCRIPTEN__)
#error "awaitable_helpers.hpp requires coroutine support, which is not available under Emscripten."
#endif

#include <chrono>
#include <expected>
#include <system_error>

#include <kth/infrastructure/utility/asio_helper.hpp>

namespace kth {

// =============================================================================
// Awaitable Helpers for Coroutine-based Async Operations
// =============================================================================

using ::asio::awaitable;
using ::asio::use_awaitable;

// -----------------------------------------------------------------------------
// with_timeout: Race an operation against a timer
// -----------------------------------------------------------------------------

template <typename T>
awaitable<std::expected<T, std::error_code>> with_timeout(
    awaitable<T> op,
    std::chrono::milliseconds timeout
) {
    using namespace ::asio::experimental::awaitable_operators;

    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);
    timer.expires_after(timeout);

    auto result = co_await (
        std::move(op) ||
        timer.async_wait(use_awaitable)
    );

    if (result.index() == 1) {
        co_return std::unexpected(make_error_code(std::errc::timed_out));
    }

    co_return std::get<0>(std::move(result));
}

// Overload for operations that return expected
template <typename T, typename E>
awaitable<std::expected<T, E>> with_timeout(
    awaitable<std::expected<T, E>> op,
    std::chrono::milliseconds timeout,
    E timeout_error
) {
    using namespace ::asio::experimental::awaitable_operators;

    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);
    timer.expires_after(timeout);

    auto result = co_await (
        std::move(op) ||
        timer.async_wait(use_awaitable)
    );

    if (result.index() == 1) {
        co_return std::unexpected(timeout_error);
    }

    co_return std::get<0>(std::move(result));
}

// -----------------------------------------------------------------------------
// delay: Simple async sleep
// -----------------------------------------------------------------------------

template <typename Rep, typename Period>
awaitable<void> delay(std::chrono::duration<Rep, Period> duration) {
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);
    timer.expires_after(duration);
    co_await timer.async_wait(use_awaitable);
}

// -----------------------------------------------------------------------------
// post_to: Execute on a specific executor
// -----------------------------------------------------------------------------

template <typename Executor>
awaitable<void> post_to(Executor&& exec) {
    co_await ::asio::post(std::forward<Executor>(exec), use_awaitable);
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_AWAITABLE_HELPERS_HPP
