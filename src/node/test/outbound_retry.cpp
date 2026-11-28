// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <string>

#include <kth/node/detail/outbound_retry.hpp>

using namespace std::chrono_literals;
using kth::network::normalized_address;
using kth::node::detail::outbound_retry;

namespace {

auto const peer = normalized_address(::asio::ip::make_address("203.0.113.7"));
auto const other = normalized_address(::asio::ip::make_address("203.0.113.8"));
outbound_retry::clock::time_point const t0{};

normalized_address i2p(std::string const& key) {
    return normalized_address(kth::infrastructure::config::authority::parse_from(key + ".b32.i2p", 0).value());
}

} // namespace

TEST_CASE("outbound retry: the wait doubles with each short session in a row, up to a limit",
          "[node][outbound_retry]") {
    CHECK(outbound_retry::cooldown_after(1) == 30s);
    CHECK(outbound_retry::cooldown_after(2) == 60s);
    CHECK(outbound_retry::cooldown_after(3) == 120s);
    CHECK(outbound_retry::cooldown_after(6) == 960s);
    CHECK(outbound_retry::cooldown_after(7) == 1800s);
    CHECK(outbound_retry::cooldown_after(1000) == 1800s);
}

TEST_CASE("outbound retry: an address whose sessions keep ending at once is left alone for longer",
          "[node][outbound_retry]") {
    outbound_retry retry;
    CHECK_FALSE(retry.cooling(peer, t0));

    retry.session_ended(peer, 200ms, t0);
    CHECK(retry.cooling(peer, t0 + 29s));
    CHECK_FALSE(retry.cooling(peer, t0 + 30s));

    // Dialled again once the wait is over, and dropped again.
    retry.forget_expired(t0 + 31s);
    retry.session_ended(peer, 200ms, t0 + 31s);
    CHECK(retry.short_sessions(peer) == 2);
    CHECK(retry.cooling(peer, t0 + 31s + 59s));
    CHECK_FALSE(retry.cooling(peer, t0 + 31s + 60s));

    retry.forget_expired(t0 + 92s);
    retry.session_ended(peer, 200ms, t0 + 92s);
    CHECK(retry.cooling(peer, t0 + 92s + 119s));
    CHECK_FALSE(retry.cooling(peer, t0 + 92s + 120s));

    // Another address is not affected.
    CHECK_FALSE(retry.cooling(other, t0 + 93s));
}

TEST_CASE("outbound retry: a session that lasted clears the count", "[node][outbound_retry]") {
    outbound_retry retry;
    retry.session_ended(peer, 1s, t0);
    retry.session_ended(peer, 1s, t0 + 40s);
    REQUIRE(retry.short_sessions(peer) == 2);

    retry.session_ended(peer, outbound_retry::short_session, t0 + 200s);
    CHECK(retry.short_sessions(peer) == 0);
    CHECK_FALSE(retry.cooling(peer, t0 + 200s));

    retry.session_ended(peer, 1s, t0 + 300s);
    CHECK(retry.short_sessions(peer) == 1);
    CHECK_FALSE(retry.cooling(peer, t0 + 330s));
}

TEST_CASE("outbound retry: an unproductive session counts as a short one however long it lasted",
          "[node][outbound_retry]") {
    outbound_retry retry;
    retry.session_unproductive(peer, t0);
    CHECK(retry.short_sessions(peer) == 1);
    CHECK(retry.cooling(peer, t0 + 29s));

    retry.session_unproductive(peer, t0 + 40s);
    CHECK(retry.short_sessions(peer) == 2);
    CHECK(retry.cooling(peer, t0 + 40s + 59s));
    CHECK_FALSE(retry.cooling(peer, t0 + 40s + 60s));
}

TEST_CASE("outbound retry: a failed dial waits the base time and keeps the count",
          "[node][outbound_retry]") {
    outbound_retry retry;
    retry.connect_failed(other, t0);
    CHECK(retry.cooling(other, t0 + 29s));
    CHECK_FALSE(retry.cooling(other, t0 + 30s));
    CHECK(retry.short_sessions(other) == 0);

    retry.session_ended(peer, 1s, t0);
    retry.session_ended(peer, 1s, t0 + 40s);
    retry.connect_failed(peer, t0 + 110s);
    CHECK(retry.short_sessions(peer) == 2);
    // The failed dial does not shorten a wait already longer than its own.
    retry.session_ended(peer, 1s, t0 + 150s);
    retry.connect_failed(peer, t0 + 151s);
    CHECK(retry.cooling(peer, t0 + 150s + 119s));
}

TEST_CASE("outbound retry: the count outlives the wait, and is dropped an hour after it",
          "[node][outbound_retry]") {
    outbound_retry retry;
    retry.session_ended(peer, 1s, t0);
    retry.connect_failed(other, t0);

    retry.forget_expired(t0 + 31s);
    CHECK(retry.short_sessions(peer) == 1);

    retry.forget_expired(t0 + 30s + 3600s);
    CHECK(retry.short_sessions(peer) == 0);
    retry.session_ended(peer, 1s, t0 + 4000s);
    CHECK_FALSE(retry.cooling(peer, t0 + 4000s + 30s));
}

TEST_CASE("outbound retry: an I2P peer is left alone by its own address",
          "[node][outbound_retry]") {
    // I2P and onion peers have no IP: each is kept by its overlay address, not
    // by the unspecified address they would all share.
    auto const first = i2p(std::string(52, 'a'));
    auto const second = i2p("b" + std::string(51, 'a'));
    outbound_retry retry;
    retry.connect_failed(first, t0);
    CHECK(retry.cooling(first, t0 + 29s));
    CHECK_FALSE(retry.cooling(second, t0 + 1s));
    CHECK_FALSE(retry.cooling(normalized_address(::asio::ip::address_v4::any()), t0 + 1s));

    retry.session_ended(second, 200ms, t0);
    CHECK(retry.short_sessions(second) == 1);
    CHECK(retry.short_sessions(first) == 0);
}
