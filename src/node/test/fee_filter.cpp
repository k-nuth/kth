// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/message/fee_filter.hpp>
#include <kth/node/detail/fee_filter.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/network/settings.hpp>

using namespace kth;
using namespace kth::node::detail;
using namespace std::chrono_literals;

// -----------------------------------------------------------------------------
// Rounding
// -----------------------------------------------------------------------------

TEST_CASE("fee filter rounder: BCHN's buckets, from half the incremental fee by 1.1",
          "[fee_filter][rounder]") {
    fee_filter_rounder const rounder(1000);
    auto const& buckets = rounder.buckets();
    REQUIRE(buckets.size() > 3);
    CHECK(buckets[0] == 0u);
    CHECK(buckets[1] == 500u);
    CHECK(buckets[2] == 550u);
    CHECK(buckets[3] == 605u);
    CHECK(buckets.back() <= fee_filter_rounder::max_fee_per_kb);
    CHECK(uint64_t(double(buckets.back()) * fee_filter_rounder::spacing) > fee_filter_rounder::max_fee_per_kb);
    CHECK(std::is_sorted(buckets.begin(), buckets.end()));
}

TEST_CASE("fee filter rounder: the bucket at or above, or the one below when stepping down",
          "[fee_filter][rounder]") {
    fee_filter_rounder const rounder(1000);
    CHECK(rounder.round(550, false) == 550u);
    CHECK(rounder.round(551, false) == 605u);
    CHECK(rounder.round(551, true) == 550u);
    // Nothing below the first.
    CHECK(rounder.round(0, true) == 0u);
    // Above every bucket: the last, either way.
    CHECK(rounder.round(fee_filter_rounder::max_fee_per_kb * 2, false) == rounder.buckets().back());
    CHECK(rounder.round(fee_filter_rounder::max_fee_per_kb * 2, true) == rounder.buckets().back());
}

TEST_CASE("fee filter: never below the relay floor", "[fee_filter][rounder]") {
    fee_filter_rounder const rounder(1000);
    // An empty pool asks nothing, but the floor still applies (BCHN
    // minRelayTxFee).
    CHECK(fee_filter_to_send(rounder, 0, 1000, true) == 1000u);
    CHECK(fee_filter_to_send(rounder, 0, 1000, false) == 1000u);
    // A raised minimum is sent rounded.
    auto const raised = fee_filter_to_send(rounder, 5000, 1000, false);
    CHECK(raised >= 5000u);
    CHECK(raised == rounder.round(5000, false));
}

// -----------------------------------------------------------------------------
// The schedule
// -----------------------------------------------------------------------------

namespace {
using schedule = fee_filter_schedule;
schedule::time_point const t0 = schedule::time_point{} + 1h;
} // namespace

TEST_CASE("fee filter schedule: the first is due at once, and an unchanged one is not resent",
          "[fee_filter][schedule]") {
    schedule peer;
    CHECK(peer.due() <= t0);

    CHECK(peer.on_due(1000, t0, 10min) == std::optional<uint64_t>(1000));
    CHECK(peer.due() == t0 + 10min);
    CHECK(peer.last_sent() == std::optional<uint64_t>(1000));

    CHECK_FALSE(peer.on_due(1000, t0 + 10min, 7min).has_value());
    CHECK(peer.due() == t0 + 17min);

    CHECK(peer.on_due(2000, t0 + 17min, 3min) == std::optional<uint64_t>(2000));
}

TEST_CASE("fee filter schedule: a move of more than a quarter brings the next send forward",
          "[fee_filter][schedule]") {
    schedule peer;
    REQUIRE(peer.on_due(1200, t0, 30min));

    // Within a quarter either way: nothing changes.
    peer.on_changed(901, t0, 1min);
    peer.on_changed(1599, t0, 1min);
    CHECK(peer.due() == t0 + 30min);

    // More than a third up: within the change delay.
    peer.on_changed(1601, t0, 2min);
    CHECK(peer.due() == t0 + 2min);
}

TEST_CASE("fee filter schedule: a move does not delay a send already due soon",
          "[fee_filter][schedule]") {
    schedule peer;
    REQUIRE(peer.on_due(1200, t0, 4min));
    peer.on_changed(100, t0, 3min + 59s);
    CHECK(peer.due() == t0 + 4min);
    // Down by more than a quarter, with the next far away: brought forward.
    REQUIRE_FALSE(peer.on_due(1200, t0 + 4min, 30min));
    peer.on_changed(899, t0 + 4min, 1min);
    CHECK(peer.due() == t0 + 5min);
}

TEST_CASE("fee filter schedule: a peer that gets none stays without one", "[fee_filter][schedule]") {
    schedule peer;
    peer.never();
    peer.on_changed(1'000'000, t0, 1min);
    CHECK(peer.due() == schedule::time_point::max());
}

TEST_CASE("fee filter schedule: Poisson delays average the interval", "[fee_filter][schedule]") {
    CHECK(poisson_delay(10min, 0.0) == schedule::clock::duration::zero());
    auto const median = std::chrono::duration<double>(poisson_delay(10min, 0.5)).count();
    CHECK(std::abs(median - 600.0 * std::log(2.0)) < 0.01);
    CHECK(poisson_delay(10min, 0.9) > poisson_delay(10min, 0.5));
}

// -----------------------------------------------------------------------------
// The peer's service
// -----------------------------------------------------------------------------

namespace {

struct served_peer {
    ::asio::io_context io;
    ::asio::ip::tcp::socket client{io};
    network::peer_session::ptr session;
    data_chunk bytes;

    served_peer() {
        network::settings settings;
        settings.identifier = 0xdab5bffa;   // regtest
        ::asio::ip::tcp::acceptor acceptor(io, {::asio::ip::address_v4::loopback(), 0});
        ::asio::ip::tcp::socket server(io);
        client.connect(acceptor.local_endpoint());
        acceptor.accept(server);
        session = std::make_shared<network::peer_session>(std::move(server), settings, true);
        ::asio::co_spawn(io, [s = session]() -> ::asio::awaitable<void> {
            (void)co_await s->run();
        }, ::asio::detached);
    }

    // The fee filters the peer was sent, in order, after running for `wait`.
    std::vector<uint64_t> fee_filters(std::chrono::milliseconds wait) {
        auto const deadline = std::chrono::steady_clock::now() + wait;
        while (std::chrono::steady_clock::now() < deadline) {
            io.restart();
            io.run_for(5ms);
            while (client.available() > 0) {
                data_chunk more(client.available());
                ::asio::read(client, ::asio::buffer(more));
                bytes.insert(bytes.end(), more.begin(), more.end());
            }
        }
        std::vector<uint64_t> out;
        size_t at = 0;
        while (at + 24 <= bytes.size()) {
            char command[13] = {};
            std::memcpy(command, bytes.data() + at + 4, 12);
            uint32_t length = 0;
            std::memcpy(&length, bytes.data() + at + 16, 4);
            if (at + 24 + length > bytes.size()) break;
            if (std::string(command) == domain::message::fee_filter::command) {
                data_chunk payload(bytes.begin() + long(at + 24), bytes.begin() + long(at + 24 + length));
                byte_reader reader(payload);
                auto parsed = domain::message::fee_filter::from_data(reader, session->negotiated_version());
                REQUIRE(parsed.has_value());
                out.push_back(parsed->minimum_fee());
            }
            at += 24 + length;
        }
        return out;
    }

    ~served_peer() {
        session->stop();
        io.restart();
        io.run_for(100ms);
    }
};

} // namespace

TEST_CASE("the peer's service sends the fee filter at once, and asks for it once",
          "[fee_filter][service]") {
    served_peer peer;
    node::message_dispatcher dispatcher;
    size_t asked = 0;
    dispatcher.set_fee_filter_handler(
        [&asked](network::peer_session&) -> ::asio::awaitable<std::optional<uint64_t>> {
            ++asked;
            co_return uint64_t(1000);
        });
    // The median draw: the next is about seven minutes away. A random one
    // can fall within the controls' window.
    dispatcher.set_fee_filter_draw([] { return 0.5; });
    auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);

    CHECK(peer.fee_filters(300ms) == std::vector<uint64_t>{1000});
    // The next is a Poisson interval away, not now.
    CHECK(asked == 1u);

    // A small move asks nothing sooner.
    peer.session->announce_fee_filter(1100);
    CHECK(peer.fee_filters(100ms) == std::vector<uint64_t>{1000});
    CHECK(asked == 1u);

    peer.session->stop();
    peer.io.restart();
    peer.io.run_for(200ms);
    CHECK(service.wait_for(0s) == std::future_status::ready);
}

TEST_CASE("the peer's service sends no fee filter to a peer that gets none",
          "[fee_filter][service]") {
    SECTION("without a handler") {
        served_peer peer;
        node::message_dispatcher dispatcher;
        auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
        CHECK(peer.fee_filters(200ms).empty());
    }
    SECTION("refused by the handler, asked once") {
        served_peer peer;
        node::message_dispatcher dispatcher;
        size_t asked = 0;
        dispatcher.set_fee_filter_handler(
            [&asked](network::peer_session&) -> ::asio::awaitable<std::optional<uint64_t>> {
                ++asked;
                co_return std::nullopt;
            });
        auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
        CHECK(peer.fee_filters(200ms).empty());
        peer.session->announce_fee_filter(1'000'000);
        CHECK(peer.fee_filters(100ms).empty());
        CHECK(asked == 1u);
    }
}

// -----------------------------------------------------------------------------
// The watch
// -----------------------------------------------------------------------------

namespace {

network::peer_session::ptr make_session(::asio::io_context& io) {
    ::asio::ip::tcp::socket socket(io);
    return std::make_shared<network::peer_session>(std::move(socket), network::settings{}, false);
}

std::optional<uint64_t> heard(network::peer_session::ptr const& peer) {
    std::error_code ec;
    return peer->fee_filter_changes().try_receive(ec);
}

} // namespace

TEST_CASE("fee filter watch: each ring hands every connected peer the minimum read then",
          "[fee_filter][watch]") {
    ::asio::io_context io;
    ::asio::experimental::select_channel<std::monostate> events(1);
    std::vector<network::peer_session::ptr> peers{make_session(io), make_session(io)};
    uint64_t minimum = 7000;
    std::stop_source stop;
    auto watch = ::asio::co_spawn(io, fee_filter_watch(events, fee_filter_sources{
            .minimum = [&]() -> ::asio::awaitable<uint64_t> { co_return minimum; },
            .peers = [&]() -> ::asio::awaitable<std::vector<network::peer_session::ptr>> { co_return peers; },
        }, stop.get_token()), ::asio::use_future);
    io.poll();
    CHECK_FALSE(heard(peers[0]));

    REQUIRE(events.try_send(std::monostate{}));
    io.poll();
    CHECK(heard(peers[0]) == std::optional<uint64_t>(7000));
    CHECK(heard(peers[1]) == std::optional<uint64_t>(7000));

    // Rings coalesce: one is taken by the waiting watch, one waits in the
    // slot, and any more are that one. Each peer keeps the latest value.
    minimum = 3000;
    REQUIRE(events.try_send(std::monostate{}));
    REQUIRE(events.try_send(std::monostate{}));
    CHECK_FALSE(events.try_send(std::monostate{}));
    io.poll();
    CHECK(heard(peers[0]) == std::optional<uint64_t>(3000));
    CHECK_FALSE(heard(peers[0]));

    stop.request_stop();
    io.poll();
    CHECK(watch.wait_for(0s) == std::future_status::ready);
}

TEST_CASE("fee filter watch: ends when its events close, or stopped before it begins",
          "[fee_filter][watch][shutdown]") {
    SECTION("events closed") {
        ::asio::io_context io;
        ::asio::experimental::select_channel<std::monostate> events(1);
        auto watch = ::asio::co_spawn(io, fee_filter_watch(events, fee_filter_sources{
                .minimum = []() -> ::asio::awaitable<uint64_t> { co_return 0; },
                .peers = []() -> ::asio::awaitable<std::vector<network::peer_session::ptr>> { co_return std::vector<network::peer_session::ptr>{}; },
            }, {}), ::asio::use_future);
        io.poll();
        REQUIRE(watch.wait_for(0s) != std::future_status::ready);
        events.close();
        io.poll();
        CHECK(watch.wait_for(0s) == std::future_status::ready);
    }
    SECTION("stopped before it begins, with a ring waiting") {
        ::asio::io_context io;
        ::asio::experimental::select_channel<std::monostate> events(1);
        REQUIRE(events.try_send(std::monostate{}));
        size_t read = 0;
        std::stop_source stop;
        stop.request_stop();
        auto watch = ::asio::co_spawn(io, fee_filter_watch(events, fee_filter_sources{
                .minimum = [&]() -> ::asio::awaitable<uint64_t> { ++read; co_return 0; },
                .peers = []() -> ::asio::awaitable<std::vector<network::peer_session::ptr>> { co_return std::vector<network::peer_session::ptr>{}; },
            }, stop.get_token()), ::asio::use_future);
        io.poll();
        CHECK(watch.wait_for(0s) == std::future_status::ready);
        CHECK(read == 0u);
    }
}
