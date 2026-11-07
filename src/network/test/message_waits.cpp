// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Message waits on the inbound select_channel: peer_session::receive(deadline)
// and the protocol helpers built on it. A wait is one selection with a
// receive case and an `after` case, so a deadline that wins leaves the
// message queued, and no cancellation crosses threads. These cases feed the
// channel with try_send in place of the read loop, and wait on an unstranded
// eight-thread pool.

#include <atomic>
#include <chrono>
#include <expected>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <test_helpers.hpp>

#include <asio/co_spawn.hpp>
#include <asio/steady_timer.hpp>
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

// Waits until a receive is suspended on the peer's inbound channel.
void require_pending(peer_session& peer) {
    auto const deadline = clock_type::now() + 5s;
    while (peer.messages().pending() == 0 && clock_type::now() < deadline) {
        std::this_thread::yield();
    }
    REQUIRE(peer.messages().pending() != 0);
}

} // namespace

TEST_CASE("message waits return a buffered message", "[peer_session][select]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    REQUIRE(peer->messages().try_send(make_ping(42)));

    SECTION("receive with a deadline") {
        auto pending = fixture.start(peer->receive(clock_type::now() + 5s));
        auto result = take_ready(pending);
        REQUIRE(result.has_value());
        REQUIRE(result->payload == make_ping(42).payload);
    }
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
}

TEST_CASE("empty message waits time out", "[peer_session][select]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();

    SECTION("receive with a deadline") {
        auto pending = fixture.start(peer->receive(clock_type::now()));
        auto result = take_ready(pending);
        REQUIRE( ! result);
        REQUIRE(result.error() == error::channel_timeout);
    }
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
    // A timed-out wait leaves nothing suspended on the channel.
    REQUIRE(peer->messages().pending() == 0);
}

TEST_CASE("stopping a peer ends pending message waits", "[peer_session][select][shutdown]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    std::future<message_result> pending;

    SECTION("receive with a deadline") {
        pending = fixture.start(peer->receive(clock_type::now() + 30s));
    }
    SECTION("receive without a deadline") {
        pending = fixture.start(peer->receive());
    }
    SECTION("raw message") {
        pending = fixture.start(wait_for_any_message(*peer, 30s));
    }
    SECTION("typed message") {
        auto typed = fixture.start(wait_for_message<domain::message::ping>(*peer, 30s));
        require_pending(*peer);
        peer->stop();
        auto result = take_ready(typed);
        REQUIRE( ! result);
        REQUIRE(result.error() == error::channel_stopped);
        return;
    }
    require_pending(*peer);
    // From the test thread, outside the pool that runs the wait.
    peer->stop();
    auto result = take_ready(pending);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::channel_stopped);
}

TEST_CASE("message ready at an expired deadline is consumed or remains queued",
          "[peer_session][select]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    REQUIRE(peer->messages().try_send(make_ping(42)));
    bool consumed = false;

    SECTION("receive with a deadline") {
        auto pending = fixture.start(peer->receive(clock_type::now()));
        auto result = take_ready(pending);
        consumed = result.has_value();
        if (result) {
            REQUIRE(result->payload == make_ping(42).payload);
        } else {
            REQUIRE(result.error() == error::channel_timeout);
        }
    }
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
    std::error_code ec;
    auto remaining = peer->messages().try_receive(ec);
    REQUIRE(size_t(consumed) + size_t(remaining.has_value()) == 1);
    if (remaining) {
        REQUIRE(remaining->payload == make_ping(42).payload);
    }
}

TEST_CASE("stopping a peer discards its buffered messages and refuses new ones",
          "[peer_session][select][shutdown]") {
    message_wait_fixture fixture;
    auto peer = fixture.make_peer();
    REQUIRE(peer->messages().try_send(make_ping(1)));
    peer->stop();
    REQUIRE( ! peer->messages().try_send(make_ping(2)));

    // stop() empties the channel: nobody is coming for what it held.
    std::error_code ec;
    REQUIRE( ! peer->messages().try_receive(ec));

    auto pending = fixture.start(peer->receive(clock_type::now() + 5s));
    auto result = take_ready(pending);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::channel_stopped);
}

TEST_CASE("typed message waits still reject other commands and malformed payloads",
          "[peer_session][select]") {
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
    REQUIRE(peer->messages().try_send(make_ping(3)));
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

    // The message after the handler asked to stop stays for the next consumer.
    std::error_code ec;
    auto remaining = peer->messages().try_receive(ec);
    REQUIRE(remaining.has_value());
    REQUIRE(remaining->payload == make_ping(3).payload);
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

TEST_CASE("message deadline, send and stop races conserve every message on eight threads",
          "[peer_session][select][concurrent]") {
    message_wait_fixture fixture;
    bool stop_half = false;
    SECTION("deadline against a send") {}
    SECTION("deadline against a send and a peer stop") { stop_half = true; }

    struct pending_wait {
        peer_session::ptr peer;
        std::future<message_result> received;
        std::future<bool> sent;
    };

    // The wait's deadline and the send become runnable together on the
    // unstranded pool: the case the fast-sync crash of #772 came from, when a
    // wait was `receive || timer`. With stop_half, the test thread also stops
    // every other peer at the deadline, which closes and empties its channel.
    for (size_t batch = 0; batch < 16; ++batch) {
        std::vector<pending_wait> waits;
        waits.reserve(64);
        auto const deadline = clock_type::now() + 20ms;
        for (size_t i = 0; i < 64; ++i) {
            auto peer = fixture.make_peer();
            auto received = fixture.start(peer->receive(deadline));
            auto sent = std::make_shared<std::promise<bool>>();
            auto sent_result = sent->get_future();
            auto timer = std::make_shared<::asio::steady_timer>(fixture.pool.get_executor(),
                deadline + std::chrono::microseconds(int(i % 3) - 1));
            auto const nonce = batch * 64 + i + 1;
            timer->async_wait([peer, timer, sent, nonce](std::error_code ec) {
                sent->set_value( ! ec && peer->messages().try_send(make_ping(nonce)));
            });
            waits.push_back({std::move(peer), std::move(received), std::move(sent_result)});
        }

        std::this_thread::sleep_until(deadline);
        if (stop_half) {
            for (size_t i = 0; i < waits.size(); i += 2) {
                waits[i].peer->stop();
            }
        }

        auto const bound = clock_type::now() + 5s;
        for (size_t i = 0; i < waits.size(); ++i) {
            auto& wait = waits[i];
            bool const stopped = stop_half && i % 2 == 0;
            REQUIRE(wait.sent.wait_until(bound) == std::future_status::ready);
            bool const sent = wait.sent.get();
            // Only a closed channel refuses the send.
            REQUIRE((sent || stopped));
            REQUIRE(wait.received.wait_until(bound) == std::future_status::ready);
            auto result = wait.received.get();
            auto const expected = make_ping(batch * 64 + i + 1).payload;
            size_t accounted = 0;
            if (result) {
                REQUIRE(result->payload == expected);
                ++accounted;
            } else if (stopped) {
                REQUIRE((result.error() == error::channel_timeout || result.error() == error::channel_stopped));
            } else {
                REQUIRE(result.error() == error::channel_timeout);
            }
            std::error_code ec;
            while (auto remaining = wait.peer->messages().try_receive(ec)) {
                REQUIRE(remaining->payload == expected);
                ++accounted;
            }
            if ( ! stopped) {
                REQUIRE(ec == ::asio::error::would_block);
            }
            // Sent once: received once, or still queued. A stop may also have
            // discarded it, but nothing is ever delivered twice. Refused:
            // nowhere.
            if (stopped) {
                REQUIRE(accounted <= (sent ? 1u : 0u));
            } else {
                REQUIRE(accounted == 1u);
            }
        }
    }
}
