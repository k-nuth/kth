// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ASYNC_CHANNEL_HPP
#define KTH_INFRASTRUCTURE_ASYNC_CHANNEL_HPP

#if defined(__EMSCRIPTEN__) && !defined(KTH_BROWSER_NODE)
#error "async_channel.hpp requires coroutine support, which is not available under Emscripten."
#endif

#include <kth/infrastructure/utility/asio_helper.hpp>

#if defined(KTH_ASIO_STANDALONE)
#include <asio/experimental/channel_select.hpp>
#endif

#if ! defined(KTH_ASIO_STANDALONE)
#include <boost/system/error_code.hpp>
#endif

namespace kth {

// =============================================================================
// Async Channel Types
// =============================================================================
//
// IMPORTANT: Thread Safety
// -------------------------
// async_channel:      NOT thread-safe. Use with strand or single-threaded context.
//                     Doc: "Distinct objects: Safe. Shared objects: Unsafe."
// concurrent_channel: Thread-safe for concurrent access from multiple threads.
//
// Usage:
//   // For same-strand communication (e.g., peer_session internal queues)
//   async_channel<std::string> messages(strand, 10);
//
//   // For multi-threaded communication (e.g., work distribution queues)
//   concurrent_channel<std::string> work_queue(pool.get_executor(), 100);
//
//   // Producer coroutine
//   co_await messages.async_send(error_code{}, "hello", use_awaitable);
//
//   // Consumer coroutine
//   auto [ec, msg] = co_await messages.async_receive(as_tuple(use_awaitable));
//
// =============================================================================

#if defined(KTH_ASIO_STANDALONE)
// Standalone Asio uses std::error_code

// Independent channel selection (Knuth fork of standalone Asio, see
// asio/experimental/channel_select.hpp). A select_channel is NOT an
// experimental::channel: it has no executor, no async_send/async_receive and no
// cancel(). It is used through try_send/try_receive, one-shot make_select(...)
// selections and task-owned make_selector(...) loops, which can wait on several
// channels of different types plus an after(time_point) branch, with these
// guarantees the || operator does not give: a losing receive consumes nothing,
// a losing or failed send keeps its payload, and close() is the shutdown signal
// (pending sends complete with broken_pipe, empty receives with eof).
//
// Thread-safe for concurrent producers/consumers. Payload moves must be noexcept
// and must not reenter the channel.
template <typename T>
using select_channel = ::asio::experimental::select_channel<T>;

// NOT thread-safe - use with strand or single-threaded context
template <typename T>
using async_channel = ::asio::experimental::channel<void(std::error_code, T)>;

// Thread-safe - for multi-threaded producer/consumer scenarios
template <typename T>
using concurrent_channel = ::asio::experimental::concurrent_channel<void(std::error_code, T)>;

// Event channels (signaling without data)
using event_channel = ::asio::experimental::channel<void(std::error_code)>;
using concurrent_event_channel = ::asio::experimental::concurrent_channel<void(std::error_code)>;

// Multi-value channels
template <typename... Args>
using multi_channel = ::asio::experimental::channel<void(std::error_code, Args...)>;

template <typename... Args>
using concurrent_multi_channel = ::asio::experimental::concurrent_channel<void(std::error_code, Args...)>;
#else
// Boost.Asio requires boost::system::error_code for channel signatures

// NOT thread-safe - use with strand or single-threaded context
template <typename T>
using async_channel = ::asio::experimental::channel<void(boost::system::error_code, T)>;

// Thread-safe - for multi-threaded producer/consumer scenarios
template <typename T>
using concurrent_channel = ::asio::experimental::concurrent_channel<void(boost::system::error_code, T)>;

// Event channels (signaling without data)
using event_channel = ::asio::experimental::channel<void(boost::system::error_code)>;
using concurrent_event_channel = ::asio::experimental::concurrent_channel<void(boost::system::error_code)>;

// Multi-value channels
template <typename... Args>
using multi_channel = ::asio::experimental::channel<void(boost::system::error_code, Args...)>;

template <typename... Args>
using concurrent_multi_channel = ::asio::experimental::concurrent_channel<void(boost::system::error_code, Args...)>;
#endif

} // namespace kth

#endif // KTH_INFRASTRUCTURE_ASYNC_CHANNEL_HPP
