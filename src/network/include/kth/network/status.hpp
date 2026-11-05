// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_STATUS_HPP
#define KTH_NETWORK_STATUS_HPP

#include <cstddef>
#include <memory>
#include <stop_token>
#include <system_error>
#include <utility>

#include <asio/awaitable.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

namespace kth::network {

struct status {
    bool active;
    size_t connections;
};

// RPC admits at most 64 connections with one outstanding query each. A port
// bounds buffered requests; each client owns one single-slot reply channel.
// Select operations, not competing coroutines, handle cancellation and close.
// Keep the port and the client/service executors alive until those tasks join.
class status_port {
    using reply_channel = ::asio::experimental::select_channel<status>;

public:
    struct request {
        // A queued payload must not destroy a channel under the request
        // channel's lock. Only the client owns the reply; cancellation releases
        // it, and this weak endpoint cannot extend or resurrect that lifetime.
        std::weak_ptr<reply_channel> reply;

        [[nodiscard]] bool abandoned() const noexcept { return reply.expired(); }
        void respond(status value) const {
            if (auto channel = reply.lock()) (void)channel->try_send(std::move(value));
        }
    };

    explicit status_port(size_t capacity = 64) : requests_(capacity) {}
    status_port(status_port const&) = delete;
    status_port& operator=(status_port const&) = delete;

    [[nodiscard]] bool is_open() const noexcept { return !stop_.stop_requested(); }

    // The stop token wakes even an in-flight reply wait, or a wait not yet
    // started. select_channel::close wakes senders without a cancel workaround.
    void close() {
        stop_.request_stop();
        requests_.close();
    }

    [[nodiscard]] ::asio::awaitable<status> read() const {
        auto executor = co_await ::asio::this_coro::executor;
        auto reply = std::make_shared<reply_channel>(1);
        struct close_reply {
            std::shared_ptr<reply_channel> channel;
            ~close_reply() { channel->close(); }
        } guard{reply};
        (void)co_await ::asio::experimental::make_select(
            ::asio::experimental::send(requests_, request{reply}))
            .async_wait(executor, {}, stop_.get_token(), ::asio::use_awaitable);
        auto response = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(*reply))
            .async_wait(executor, {}, stop_.get_token(), ::asio::use_awaitable);
        if (!is_open()) throw std::system_error(::asio::error::operation_aborted);
        co_return std::move(*response.template value<0>());
    }

    [[nodiscard]] ::asio::awaitable<request> receive() {
        auto response = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(requests_))
            .async_wait(co_await ::asio::this_coro::executor, {}, stop_.get_token(), ::asio::use_awaitable);
        co_return std::move(*response.template value<0>());
    }

private:
    mutable ::asio::experimental::select_channel<request> requests_;
    std::stop_source stop_;
};

} // namespace kth::network

#endif // KTH_NETWORK_STATUS_HPP
