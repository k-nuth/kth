// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_STATUS_HPP
#define KTH_NETWORK_STATUS_HPP

#include <cstddef>
#include <memory>
#include <system_error>
#include <utility>
#include <variant>

#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

namespace kth::network {

struct status {
    bool active;
    size_t connections;
};

// A bounded request/reply port, with no access to peer state. RPC admits at
// most 64 connections, each with one outstanding request. The service owns
// receive(); clients only borrow a const port and await read(). Keep the port
// and both executors alive until the service and its clients have joined.
class status_port {
    using reply_channel = ::asio::experimental::concurrent_channel<void(std::error_code, status)>;

public:
    struct request {
        std::shared_ptr<reply_channel> reply;

        [[nodiscard]] bool abandoned() const { return !reply || !reply->is_open(); }
        void respond(status value) const {
            // One reply per request. A cancelled client must never suspend the
            // owner, nor consume another client's reply.
            if (reply) (void)reply->try_send(std::error_code{}, value);
        }
    };

    explicit status_port(::asio::any_io_executor executor, size_t capacity = 64)
        : requests_(executor, capacity), closed_(executor, 1) {}

    status_port(status_port const&) = delete;
    status_port& operator=(status_port const&) = delete;

    [[nodiscard]] bool is_open() const { return requests_.is_open(); }

    // Channel closure broadcasts shutdown even before receive() has started.
    // Queued observations are no longer owed; pending senders and all clients
    // waiting for replies wake without waiting for the owner to drain the queue.
    void close() {
        requests_.close();
        // close preserves buffered requests in Asio. Cancel after closing
        // admission as well, so a sender blocked behind that buffer wakes.
        requests_.cancel();
        closed_.close();
    }

    [[nodiscard]] ::asio::awaitable<status> read() const {
        if (!is_open()) throw std::system_error(::asio::error::operation_aborted);
        auto reply = std::make_shared<reply_channel>(co_await ::asio::this_coro::executor, 1);
        struct close_reply {
            std::shared_ptr<reply_channel> channel;
            ~close_reply() { channel->close(); }
        } guard{reply};
        co_await requests_.async_send(std::error_code{}, request{reply}, ::asio::use_awaitable);
        using namespace ::asio::experimental::awaitable_operators;
        auto result = co_await (
            reply->async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            closed_.async_receive(::asio::as_tuple(::asio::use_awaitable)));
        if (result.index() != 0 || !is_open()) throw std::system_error(::asio::error::operation_aborted);
        auto [ec, value] = std::get<0>(result);
        if (ec) throw std::system_error(ec);
        co_return value;
    }

    [[nodiscard]] ::asio::awaitable<request> receive() {
        if (!is_open()) throw std::system_error(::asio::error::operation_aborted);
        co_return co_await requests_.async_receive(::asio::use_awaitable);
    }

    [[nodiscard]] ::asio::awaitable<void> wait_closed() const {
        (void)co_await closed_.async_receive(::asio::as_tuple(::asio::use_awaitable));
    }

private:
    mutable ::asio::experimental::concurrent_channel<void(std::error_code, request)> requests_;
    // No values are sent: closing an empty channel wakes every receiver.
    mutable ::asio::experimental::concurrent_channel<void(std::error_code)> closed_;
};

} // namespace kth::network

#endif // KTH_NETWORK_STATUS_HPP
