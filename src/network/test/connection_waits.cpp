// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>

#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/network/settings.hpp>

using namespace kth;
using namespace kth::network;
using namespace std::chrono_literals;

namespace {

::asio::awaitable<std::expected<handshake_result, code>> direct_handshake(
    peer_session::ptr peer, handshake_config config)
{
    co_return co_await perform_handshake_direct(*peer, config);
}

::asio::awaitable<code> send_direct_ping(peer_session::ptr peer) {
    domain::message::ping const ping{42};
    co_return co_await peer->send_direct(ping);
}

struct connection_wait_fixture {
    ::asio::thread_pool pool{8};
    network::settings settings{domain::config::network::regtest};
    ::asio::ip::tcp::acceptor acceptor{pool.get_executor(), {::asio::ip::tcp::v4(), 0}};
    std::string const hostname = "127.0.0.1";
    std::vector<peer_session::ptr> peers;

    connection_wait_fixture() {
        // An empty user agent is a prefix of every blacklisted one, so a peer
        // without one is refused by the handshake.
        settings.user_agent = "/KnuthTest:0.1.0/";
    }

    ~connection_wait_fixture() {
        for (auto const& peer : peers) peer->stop();
        pool.stop();
        pool.join();
    }

    std::pair<peer_session::ptr, peer_session::ptr> connect() {
        auto const port = acceptor.local_endpoint().port();
        REQUIRE(port != 0);
        auto accepted = acceptor.async_accept(::asio::as_tuple(::asio::use_future));
        // The coroutine starts on the pool; async_connect must establish its own strand.
        auto connected = ::asio::co_spawn(pool, async_connect(pool.get_executor(),
            hostname, port, settings, 5s), ::asio::use_future);
        REQUIRE(connected.wait_for(10s) == std::future_status::ready);
        auto result = connected.get();
        REQUIRE(result.has_value());
        peers.push_back(*result);
        REQUIRE(accepted.wait_for(10s) == std::future_status::ready);
        auto completion = accepted.get();
        auto& [ec, socket] = completion;
        REQUIRE_FALSE(ec);
        auto remote = std::make_shared<peer_session>(std::move(socket), settings, true);
        peers.push_back(remote);
        return {*result, std::move(remote)};
    }
};

} // namespace

TEST_CASE("connect and direct handshake serialize their races on a thread pool",
          "[peer_session][protocols_coro][concurrent][asan][tsan]") {
    connection_wait_fixture fixture;
    for (size_t i = 0; i < 64; ++i) {
        auto [peer, remote] = fixture.connect();
        auto local_config = make_handshake_config(fixture.settings, 0, 2 * i + 1);
        auto remote_config = make_handshake_config(fixture.settings, 0, 2 * i + 2);
        auto local = ::asio::co_spawn(fixture.pool,
            direct_handshake(peer, std::move(local_config)), ::asio::use_future);
        auto other = ::asio::co_spawn(fixture.pool,
            direct_handshake(remote, std::move(remote_config)), ::asio::use_future);
        REQUIRE(local.wait_for(10s) == std::future_status::ready);
        REQUIRE(other.wait_for(10s) == std::future_status::ready);
        REQUIRE(local.get().has_value());
        REQUIRE(other.get().has_value());
        peer->stop();
        remote->stop();
    }
}

TEST_CASE("direct handshake deadline ends a socket read on a thread pool",
          "[protocols_coro][concurrent][timeout]") {
    connection_wait_fixture fixture;
    auto [peer, remote] = fixture.connect();
    auto config = make_handshake_config(fixture.settings, 0, 1);
    config.timeout = 2s;
    auto completion = ::asio::co_spawn(fixture.pool,
        direct_handshake(peer, std::move(config)), ::asio::use_future);
    REQUIRE(completion.wait_for(10s) == std::future_status::ready);
    auto result = completion.get();
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::channel_timeout);
}

TEST_CASE("stopping a peer from another thread ends its direct handshake",
          "[protocols_coro][concurrent][shutdown]") {
    connection_wait_fixture fixture;
    auto [peer, remote] = fixture.connect();
    auto config = make_handshake_config(fixture.settings, 0, 1);
    auto completion = ::asio::co_spawn(fixture.pool,
        direct_handshake(peer, std::move(config)), ::asio::use_future);
    auto const deadline = std::chrono::steady_clock::now() + 5s;
    while (peer->bytes_sent() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(peer->bytes_sent() != 0);
    peer->stop();
    REQUIRE(completion.wait_for(5s) == std::future_status::ready);
    REQUIRE_FALSE(completion.get().has_value());
}

TEST_CASE("closing a full inbound channel releases the session reader",
          "[peer_session][select][concurrent][shutdown]") {
    connection_wait_fixture fixture;
    auto [peer, remote] = fixture.connect();
    for (size_t i = 0; i < 100; ++i) {
        REQUIRE(peer->messages().try_send(raw_message{}));
    }
    auto running = ::asio::co_spawn(fixture.pool, peer->run(), ::asio::use_future);
    auto sent = ::asio::co_spawn(remote->executor(), send_direct_ping(remote), ::asio::use_future);
    REQUIRE(sent.wait_for(5s) == std::future_status::ready);
    REQUIRE(sent.get() == error::success);

    auto const deadline = std::chrono::steady_clock::now() + 5s;
    while (peer->messages().pending() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(peer->messages().pending() == 1);
    peer->messages().close();
    REQUIRE(running.wait_for(5s) == std::future_status::ready);
    REQUIRE(running.get() == error::channel_stopped);
    REQUIRE(peer->stopped());
    // Closing kept what was buffered; the reader's refused message was not added.
    REQUIRE(peer->messages().buffered() == 100);
}
