// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <cstddef>
#include <exception>
#include <expected>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/steady_timer.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/node/detail/peer_events.hpp>

#include "child_process_harness.hpp"

using namespace kth;
using namespace std::chrono_literals;

namespace {

using notification_result = std::expected<node::peer_notification, code>;

network::settings peer_event_settings() {
    network::settings settings(domain::config::network::regtest);
    settings.threads = 1;
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    return settings;
}

struct event_waits {
    network::settings settings{peer_event_settings()};
    node::p2p_node network{settings};
    ::asio::thread_pool pool{8};

    ~event_waits() {
        pool.stop();
        pool.join();
    }

    std::future<notification_result> wait(std::chrono::steady_clock::time_point deadline) {
        auto promise = std::make_shared<std::promise<notification_result>>();
        auto future = promise->get_future();
        ::asio::co_spawn(pool.get_executor(), node::detail::wait_for_peer_event(network, deadline),
            [promise](std::exception_ptr exception, notification_result result) {
                if (exception) {
                    promise->set_exception(exception);
                } else {
                    promise->set_value(std::move(result));
                }
            });
        return future;
    }

    notification_result finish(std::future<notification_result>& future) {
        REQUIRE(future.wait_for(30s) == std::future_status::ready);
        return future.get();
    }

    bool pending() {
        auto const deadline = std::chrono::steady_clock::now() + 30s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (network.peer_events().pending() != 0) {
                return true;
            }
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }
};

::asio::awaitable<bool> send_event_at(node::p2p_node& network,
    std::chrono::steady_clock::time_point deadline, node::peer_event_type event)
{
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor, deadline);
    auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return false;
    }
    co_return network.peer_events().try_send(node::peer_notification{nullptr, event});
}

} // namespace

TEST_CASE("peer event wait returns an event before its deadline", "[p2p_node][select_sync]") {
    kth::test::watchdog guard(120s, "peer event before deadline");
    event_waits waits;
    auto future = waits.wait(std::chrono::steady_clock::now() + 60s);
    REQUIRE(waits.pending());
    REQUIRE(waits.network.peer_events().try_send(
        node::peer_notification{nullptr, node::peer_event_type::connected}));
    auto result = waits.finish(future);
    REQUIRE(result.has_value());
    REQUIRE(result->event == node::peer_event_type::connected);
    REQUIRE(waits.network.peer_events().buffered() == 0);
}

TEST_CASE("peer event wait times out without an event", "[p2p_node][select_sync]") {
    kth::test::watchdog guard(120s, "peer event timeout");
    event_waits waits;
    auto future = waits.wait(std::chrono::steady_clock::now());
    auto result = waits.finish(future);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::channel_timeout);
    REQUIRE(waits.network.peer_events().pending() == 0);
}

TEST_CASE("peer event at a ready deadline is consumed or stays buffered", "[p2p_node][select_sync]") {
    kth::test::watchdog guard(120s, "peer event ready deadline");
    event_waits waits;
    REQUIRE(waits.network.peer_events().try_send(
        node::peer_notification{nullptr, node::peer_event_type::connected}));
    auto future = waits.wait(std::chrono::steady_clock::now());
    auto result = waits.finish(future);
    if ( ! result) {
        REQUIRE(result.error() == error::channel_timeout);
    }
    code ec;
    auto remaining = waits.network.peer_events().try_receive(ec);
    REQUIRE(size_t(result.has_value()) + size_t(remaining.has_value()) == 1);
    if (remaining) {
        REQUIRE_FALSE(ec);
        REQUIRE(remaining->event == node::peer_event_type::connected);
    } else {
        REQUIRE(ec == ::asio::error::would_block);
        REQUIRE(result->event == node::peer_event_type::connected);
    }
}

TEST_CASE("closed peer events end a pending wait", "[p2p_node][select_sync]") {
    kth::test::watchdog guard(120s, "peer event close");
    event_waits waits;
    auto future = waits.wait(std::chrono::steady_clock::now() + 60s);
    REQUIRE(waits.pending());
    waits.network.peer_events().close();
    auto result = waits.finish(future);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ::asio::error::eof);
    REQUIRE_FALSE(waits.network.peer_events().try_send(
        node::peer_notification{nullptr, node::peer_event_type::connected}));
}

TEST_CASE("node stop from another thread cancels its peer event wait", "[p2p_node][select_sync]") {
    kth::test::watchdog guard(120s, "peer event external stop");
    event_waits waits;
    auto future = waits.wait(std::chrono::steady_clock::now() + 60s);
    REQUIRE(waits.pending());
    // The test thread is outside the pool running the pending selection.
    waits.network.stop();
    auto result = waits.finish(future);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ::asio::error::operation_aborted);
    REQUIRE(waits.network.peer_events().pending() == 0);

    REQUIRE(waits.network.peer_events().try_send(
        node::peer_notification{nullptr, node::peer_event_type::disconnected}));
    auto stopped = waits.wait(std::chrono::steady_clock::now() + 60s);
    auto stopped_result = waits.finish(stopped);
    REQUIRE_FALSE(stopped_result.has_value());
    REQUIRE(stopped_result.error() == ::asio::error::operation_aborted);
    REQUIRE(waits.network.peer_events().buffered() == 1);
}

TEST_CASE("peer event deadline races preserve every notification on eight threads", "[p2p_node][select_sync]") {
    kth::test::watchdog guard(120s, "peer event deadline stress");
    event_waits waits;
    for (size_t iteration = 0; iteration != 512; ++iteration) {
        auto const event = iteration % 2 == 0
            ? node::peer_event_type::connected : node::peer_event_type::disconnected;
        auto const deadline = std::chrono::steady_clock::now() + 200us;
        auto future = waits.wait(deadline);
        auto sent = std::make_shared<std::promise<bool>>();
        auto sent_future = sent->get_future();
        ::asio::co_spawn(waits.pool.get_executor(), send_event_at(waits.network, deadline, event),
            [sent](std::exception_ptr exception, bool accepted) {
                if (exception) {
                    sent->set_exception(exception);
                } else {
                    sent->set_value(accepted);
                }
            });
        auto result = waits.finish(future);
        REQUIRE(sent_future.wait_for(30s) == std::future_status::ready);
        REQUIRE(sent_future.get());
        if ( ! result) {
            REQUIRE(result.error() == error::channel_timeout);
        }
        code ec;
        auto remaining = waits.network.peer_events().try_receive(ec);
        REQUIRE(size_t(result.has_value()) + size_t(remaining.has_value()) == 1);
        REQUIRE((result ? result->event : remaining->event) == event);
        REQUIRE(waits.network.peer_events().buffered() == 0);
        REQUIRE(waits.network.peer_events().pending() == 0);
    }
}

TEST_CASE("peer event deadline races survive a concurrent node stop", "[p2p_node][select_sync]") {
    kth::test::watchdog guard(120s, "peer event cancellation stress");
    for (size_t round = 0; round != 8; ++round) {
        event_waits waits;
        std::vector<std::future<notification_result>> received;
        std::vector<std::future<bool>> sent;
        auto const deadline = std::chrono::steady_clock::now() + 1ms;
        for (size_t index = 0; index != 64; ++index) {
            received.push_back(waits.wait(deadline));
            auto promise = std::make_shared<std::promise<bool>>();
            sent.push_back(promise->get_future());
            ::asio::co_spawn(waits.pool.get_executor(), send_event_at(
                waits.network, deadline, node::peer_event_type::connected),
                [promise](std::exception_ptr exception, bool accepted) {
                    if (exception) {
                        promise->set_exception(exception);
                    } else {
                        promise->set_value(accepted);
                    }
                });
        }
        std::this_thread::sleep_until(deadline);
        waits.network.stop();

        size_t consumed = 0;
        for (auto& future : received) {
            auto result = waits.finish(future);
            if (result) {
                REQUIRE(result->event == node::peer_event_type::connected);
                ++consumed;
            } else {
                REQUIRE((result.error() == error::channel_timeout ||
                    result.error() == ::asio::error::operation_aborted));
            }
        }
        for (auto& future : sent) {
            REQUIRE(future.wait_for(30s) == std::future_status::ready);
            REQUIRE(future.get());
        }
        size_t buffered = 0;
        code ec;
        while (auto remaining = waits.network.peer_events().try_receive(ec)) {
            REQUIRE(remaining->event == node::peer_event_type::connected);
            ++buffered;
        }
        REQUIRE(ec == ::asio::error::would_block);
        REQUIRE(consumed + buffered == 64);
        REQUIRE(waits.network.peer_events().pending() == 0);
    }
}
