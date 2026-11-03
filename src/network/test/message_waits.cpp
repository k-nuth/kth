// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <atomic>
#include <chrono>
#include <expected>
#include <future>
#include <memory>
#include <stop_token>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <test_helpers.hpp>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/thread_pool.hpp>

using namespace kth;
using namespace kth::network;
using namespace std::chrono_literals;

namespace {

using clock_type = std::chrono::steady_clock;
using message_result = std::expected<raw_message, code>;

struct message_wait_fixture {
    ::asio::thread_pool pool{8};
    network::settings settings;
    std::vector<peer_session::ptr> peers;

    message_wait_fixture() {
        settings.protocol_maximum = 70015;
    }

    ~message_wait_fixture() {
        // A failed assertion must not leave a coroutine waiting in join().
        for (auto const& peer : peers) {
            peer->stop();
        }
        pool.stop();
        pool.join();
    }

    peer_session::ptr make_peer() {
        auto peer = std::make_shared<peer_session>(
            ::asio::ip::tcp::socket(pool.get_executor()), settings);
        peers.push_back(peer);
        return peer;
    }

    template <typename T>
    std::future<T> start(::asio::awaitable<T> operation) {
        auto completed = std::make_shared<std::promise<T>>();
        auto result = completed->get_future();
        ::asio::co_spawn(pool, std::move(operation),
            [completed](std::exception_ptr exception, T value) {
                if (exception) {
                    completed->set_exception(exception);
                } else {
                    completed->set_value(std::move(value));
                }
            });
        return result;
    }
};

raw_message make_ping(uint64_t nonce) {
    raw_message result;
    result.heading.set_command(domain::message::ping::command);
    result.payload = kth::to_data_chunk(domain::message::ping{nonce}, 70015);
    return result;
}

template <typename T>
T take_ready(std::future<T>& future) {
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    return future.get();
}

void require_pending(peer_session& peer) {
    auto const deadline = clock_type::now() + 5s;
    while (peer.messages().pending() == 0 && clock_type::now() < deadline) {
        std::this_thread::yield();
    }
    REQUIRE(peer.messages().pending() != 0);
}

// The ping and node dispatch loops keep one selector across successive waits.
::asio::awaitable<message_result> wait_with_selector(
    peer_session& peer, clock_type::time_point deadline, std::stop_token stop)
{
    auto ex = co_await ::asio::this_coro::executor;
    auto selector = ::asio::experimental::make_selector(ex, {}, stop,
        ::asio::experimental::receive(peer.messages()),
        ::asio::experimental::after(deadline));
    for (size_t attempt = 0; attempt < 2; ++attempt) {
        auto completion = co_await selector.wait(::asio::as_tuple(::asio::deferred));
        auto const ec = std::get<0>(completion);
        auto const index = std::get<1>(completion);
        if (ec) {
            co_return std::unexpected(error::channel_stopped);
        }
        if (index == 0) {
            co_return std::move(*selector.value<0>());
        }
        selector.case_at<1>().time = clock_type::now() + 100us;
    }
    co_return std::unexpected(error::channel_timeout);
}

} // namespace

TEST_CASE("message waits return a buffered message", "[protocols_coro][select]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    REQUIRE(peer->messages().try_send(make_ping(42)));

    SECTION("raw message") {
        auto pending = fixture.start(wait_for_any_message(*peer, 5s));
        auto result = take_ready(pending);
        REQUIRE(result.has_value());
        REQUIRE(result->payload == make_ping(42).payload);
    }
    SECTION("typed message") {
        auto pending = fixture.start(wait_for_message<domain::message::ping>(*peer, 5s));
        auto result = take_ready(pending);
        REQUIRE(result.has_value());
        REQUIRE(result->nonce() == 42);
    }
    SECTION("reusable selector") {
        auto pending = fixture.start(wait_with_selector(*peer, clock_type::now() + 5s, {}));
        auto result = take_ready(pending);
        REQUIRE(result.has_value());
        REQUIRE(result->payload == make_ping(42).payload);
    }
}

TEST_CASE("empty message waits time out", "[protocols_coro][select]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();

    SECTION("raw message") {
        auto pending = fixture.start(wait_for_any_message(*peer, 0s));
        auto result = take_ready(pending);
        REQUIRE( ! result);
        REQUIRE(result.error() == error::channel_timeout);
    }
    SECTION("typed message") {
        auto pending = fixture.start(wait_for_message<domain::message::ping>(*peer, 0s));
        auto result = take_ready(pending);
        REQUIRE( ! result);
        REQUIRE(result.error() == error::channel_timeout);
    }
    SECTION("reusable selector") {
        auto pending = fixture.start(wait_with_selector(*peer, clock_type::now(), {}));
        auto result = take_ready(pending);
        REQUIRE( ! result);
        REQUIRE(result.error() == error::channel_timeout);
    }
}

TEST_CASE("stopping a peer ends pending message waits", "[protocols_coro][select][shutdown]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();

    SECTION("raw message") {
        auto pending = fixture.start(wait_for_any_message(*peer, 30s));
        require_pending(*peer);
        peer->stop();
        auto result = take_ready(pending);
        REQUIRE( ! result);
        REQUIRE(result.error() == error::channel_stopped);
    }
    SECTION("typed message") {
        auto pending = fixture.start(wait_for_message<domain::message::ping>(*peer, 30s));
        require_pending(*peer);
        peer->stop();
        auto result = take_ready(pending);
        REQUIRE( ! result);
        REQUIRE(result.error() == error::channel_stopped);
    }
    SECTION("reusable selector") {
        auto pending = fixture.start(wait_with_selector(*peer, clock_type::now() + 30s, {}));
        require_pending(*peer);
        peer->stop();
        auto result = take_ready(pending);
        REQUIRE( ! result);
        REQUIRE(result.error() == error::channel_stopped);
    }
}

TEST_CASE("message ready at an expired deadline is consumed or remains queued",
          "[protocols_coro][select]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    REQUIRE(peer->messages().try_send(make_ping(42)));
    bool consumed = false;

    SECTION("raw message") {
        auto pending = fixture.start(wait_for_any_message(*peer, 0s));
        auto result = take_ready(pending);
        consumed = result.has_value();
        if (result) {
            REQUIRE(result->payload == make_ping(42).payload);
        } else {
            REQUIRE(result.error() == error::channel_timeout);
        }
    }
    SECTION("typed message") {
        auto pending = fixture.start(wait_for_message<domain::message::ping>(*peer, 0s));
        auto result = take_ready(pending);
        consumed = result.has_value();
        if (result) {
            REQUIRE(result->nonce() == 42);
        } else {
            REQUIRE(result.error() == error::channel_timeout);
        }
    }
    SECTION("reusable selector") {
        auto pending = fixture.start(wait_with_selector(*peer, clock_type::now(), {}));
        auto result = take_ready(pending);
        consumed = result.has_value();
        if (result) {
            REQUIRE(result->payload == make_ping(42).payload);
        } else {
            REQUIRE(result.error() == error::channel_timeout);
        }
    }
    std::error_code ec;
    auto remaining = peer->messages().try_receive(ec);
    REQUIRE(size_t(consumed) + size_t(remaining.has_value()) == 1);
    if (remaining) {
        REQUIRE(remaining->payload == make_ping(42).payload);
    }
}

TEST_CASE("typed message waits still reject other commands and malformed payloads",
          "[protocols_coro][select]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    auto raw = make_ping(42);
    SECTION("different command") {
        raw.heading.set_command(domain::message::pong::command);
    }
    SECTION("truncated payload") {
        raw.payload.resize(7);
    }
    REQUIRE(peer->messages().try_send(std::move(raw)));
    auto pending = fixture.start(wait_for_message<domain::message::ping>(*peer, 5s));
    auto result = take_ready(pending);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::bad_stream);
}

TEST_CASE("message wait cancellation preserves a ready message", "[protocols_coro][select][cancel]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    std::stop_source stop;
    stop.request_stop();
    REQUIRE(peer->messages().try_send(make_ping(42)));
    auto pending = fixture.start(wait_for_any_message(*peer, clock_type::now() + 30s, stop.get_token()));
    auto result = take_ready(pending);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::channel_stopped);
    std::error_code ec;
    auto remaining = peer->messages().try_receive(ec);
    REQUIRE(remaining.has_value());
    REQUIRE(remaining->payload == make_ping(42).payload);
    REQUIRE( ! ec);
}

TEST_CASE("a suspended message wait can be cancelled from another thread",
          "[protocols_coro][select][cancel]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    std::stop_source stop;
    auto pending = fixture.start(wait_for_any_message(*peer, clock_type::now() + 30s, stop.get_token()));
    require_pending(*peer);
    REQUIRE(stop.request_stop());
    auto result = take_ready(pending);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::channel_stopped);
    REQUIRE( ! peer->stopped());
    REQUIRE(peer->messages().try_send(make_ping(42)));
    std::error_code ec;
    auto remaining = peer->messages().try_receive(ec);
    REQUIRE(remaining.has_value());
    REQUIRE(remaining->payload == make_ping(42).payload);
}

TEST_CASE("ping loop ends on channel closure or peer stop", "[protocols_coro][select][shutdown]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    auto pending = fixture.start(run_ping_pong(*peer, 30s));
    require_pending(*peer);
    SECTION("closed channel") {
        peer->messages().close();
    }
    SECTION("stopped peer") {
        peer->stop();
    }
    REQUIRE(take_ready(pending) == error::channel_stopped);
}

TEST_CASE("message loop preserves dispatch order and stops when the handler asks",
          "[protocols_coro][select]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    REQUIRE(peer->messages().try_send(make_ping(1)));
    REQUIRE(peer->messages().try_send(make_ping(2)));
    auto received = std::make_shared<std::vector<data_chunk>>();
    auto pending = fixture.start(run_message_loop(*peer,
        [received](raw_message const& raw) -> ::asio::awaitable<bool> {
            received->push_back(raw.payload);
            co_return received->size() < 2;
        }));
    REQUIRE(take_ready(pending) == error::channel_stopped);
    REQUIRE(received->size() == 2);
    REQUIRE((*received)[0] == make_ping(1).payload);
    REQUIRE((*received)[1] == make_ping(2).payload);
}

TEST_CASE("message loop ends on channel closure", "[protocols_coro][select][shutdown]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    auto called = std::make_shared<std::atomic<bool>>(false);
    auto pending = fixture.start(run_message_loop(*peer,
        [called](raw_message const&) -> ::asio::awaitable<bool> {
            called->store(true);
            co_return true;
        }));
    require_pending(*peer);
    peer->messages().close();
    REQUIRE(take_ready(pending) == error::channel_stopped);
    REQUIRE( ! called->load());
}

TEST_CASE("message deadline and cancellation races conserve every message on eight threads",
          "[protocols_coro][select][concurrent][asan][tsan]") {
    message_wait_fixture fixture;
    bool reusable = false;
    SECTION("one shot protocol wait") {}
    SECTION("reused loop selector") { reusable = true; }

    struct pending_wait {
        peer_session::ptr peer;
        std::stop_source stop;
        std::future<message_result> received;
        std::future<bool> sent;
    };

    // Both sides become runnable together, on an unstranded pool. On the old
    // receive || timer path this races parallel_group cancellation with the
    // completion of the other branch, as in the fast-sync crash.
    for (size_t batch = 0; batch < 16; ++batch) {
        std::vector<pending_wait> waits;
        waits.reserve(64);
        auto const deadline = clock_type::now() + 20ms;
        for (size_t i = 0; i < 64; ++i) {
            auto peer = fixture.make_peer();
            std::stop_source stop;
            auto received = fixture.start(reusable
                ? wait_with_selector(*peer, deadline, stop.get_token())
                : wait_for_any_message(*peer, deadline, stop.get_token()));
            auto sent = std::make_shared<std::promise<bool>>();
            auto sent_result = sent->get_future();
            auto timer = std::make_shared<::asio::steady_timer>(fixture.pool.get_executor(),
                deadline + std::chrono::microseconds(int(i % 3) - 1));
            auto const nonce = batch * 64 + i + 1;
            timer->async_wait([peer, timer, sent, nonce](std::error_code ec) {
                sent->set_value( ! ec && peer->messages().try_send(make_ping(nonce)));
            });
            waits.push_back({std::move(peer), std::move(stop), std::move(received), std::move(sent_result)});
        }

        // This is the test thread, outside the pool which owns all the waits.
        std::this_thread::sleep_until(deadline);
        for (size_t i = 0; i < waits.size(); i += 2) {
            waits[i].stop.request_stop();
        }

        auto const bound = clock_type::now() + 5s;
        for (size_t i = 0; i < waits.size(); ++i) {
            auto& wait = waits[i];
            REQUIRE(wait.sent.wait_until(bound) == std::future_status::ready);
            REQUIRE(wait.sent.get());
            REQUIRE(wait.received.wait_until(bound) == std::future_status::ready);
            auto result = wait.received.get();
            auto const expected = make_ping(batch * 64 + i + 1).payload;
            size_t accounted = 0;
            if (result) {
                REQUIRE(result->payload == expected);
                ++accounted;
            } else {
                REQUIRE((result.error() == error::channel_timeout || result.error() == error::channel_stopped));
            }
            std::error_code ec;
            while (auto remaining = wait.peer->messages().try_receive(ec)) {
                REQUIRE(remaining->payload == expected);
                ++accounted;
            }
            REQUIRE(ec == ::asio::error::would_block);
            REQUIRE(accounted == 1);
        }
    }
}
