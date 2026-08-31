// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstdint>
#include <future>
#include <string>
#include <vector>

#include <kth/domain/message/compact_block.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/node/detail/block_announcements.hpp>
#include <kth/node/detail/header_request_delivery.hpp>
#include <kth/node/p2p_node.hpp>
#include <asio/use_future.hpp>
#include <kth/network/settings.hpp>

#include "header_peer_harness.hpp"

using namespace kth;
using namespace kth::node;
using namespace kth::test;

// =============================================================================
// Reading a block announcement, and holding on to it (#706)
// =============================================================================
//
// Three wire messages mean the same thing — a block exists that you may not
// have — and before this they ended in three different kinds of nowhere: no
// handler at all for `cmpctblock` and `inv`, and a per-request channel for an
// unsolicited `headers`, where it waited to be handed to some later request as
// if it had answered it.
//
// What each of the three understands is stated here without a peer, a socket or
// a node in the way, and the registry that holds what they produce is exercised
// on its own — because the property that matters about it is not what one call
// does but what a burst of them cannot lose.

namespace {

uint32_t const msg_version = domain::message::version::level::maximum;

domain::chain::header make_header(uint32_t nonce) {
    return domain::chain::header{
        1u, null_hash, null_hash, 1231006505u + nonce, 0x1d00ffffu, nonce};
}

data_chunk headers_payload(std::vector<domain::chain::header> const& elements) {
    domain::message::header::list carried(elements.begin(), elements.end());
    auto message = domain::message::headers::create(std::move(carried));
    REQUIRE(message.has_value());

    data_chunk out(message->serialized_size(msg_version));
    byte_writer writer(out);
    REQUIRE(message->to_data(writer, msg_version).has_value());
    return out;
}

data_chunk compact_block_payload(domain::chain::header const& header) {
    domain::message::compact_block message(header, 7u, {}, {});

    data_chunk out(message.serialized_size(msg_version));
    byte_writer writer(out);
    REQUIRE(message.to_data(writer, msg_version).has_value());
    return out;
}

data_chunk inventory_payload(domain::message::inventory_vector::list entries) {
    auto message = domain::message::inventory::create(std::move(entries));
    REQUIRE(message.has_value());

    data_chunk out(message->serialized_size(msg_version));
    byte_writer writer(out);
    REQUIRE(message->to_data(writer, msg_version).has_value());
    return out;
}

network::settings node_settings() {
    network::settings settings;
    settings.inbound_port = 0;
    settings.inbound_connections = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.hosts_file = "/tmp/kth_announce_hosts_nonexistent.dat";
    settings.banlist_file = "/tmp/kth_announce_banlist_nonexistent.dat";
    return settings;
}

} // namespace

// -----------------------------------------------------------------------------
// What each form says
// -----------------------------------------------------------------------------

TEST_CASE("announcements: an unsolicited headers announces every header it carries",
          "[announcements]") {
    auto const a = make_header(1);
    auto const b = make_header(2);
    auto const c = make_header(3);

    auto const hashes = kth::node::detail::announced_by_headers(headers_payload({a, b, c}), msg_version);

    REQUIRE(hashes.size() == 3u);
    CHECK(hashes[0] == kth::domain::chain::hash(a));
    CHECK(hashes[1] == kth::domain::chain::hash(b));
    CHECK(hashes[2] == kth::domain::chain::hash(c));
}

TEST_CASE("announcements: a cmpctblock announces the header it opens with",
          "[announcements]") {
    // The whole of BIP152 this needs. The compact body is not read and no
    // reconstruction is implied -- but the message is no longer accepted and
    // dropped, which is what left eight announcements per block unobserved.
    auto const header = make_header(11);

    auto const hashes = kth::node::detail::announced_by_compact_block(compact_block_payload(header), msg_version);

    REQUIRE(hashes.size() == 1u);
    CHECK(hashes[0] == kth::domain::chain::hash(header));
}

TEST_CASE("announcements: an inv announces its blocks and not its transactions",
          "[announcements]") {
    using domain::message::inventory_vector;

    auto const block_hash = kth::domain::chain::hash(make_header(21));
    auto const compact_hash = kth::domain::chain::hash(make_header(22));
    auto const tx_hash = kth::domain::chain::hash(make_header(23));

    auto const hashes = kth::node::detail::announced_by_inventory(inventory_payload({
        {inventory_vector::type_id::transaction, tx_hash},
        {inventory_vector::type_id::block, block_hash},
        {inventory_vector::type_id::compact_block, compact_hash}}), msg_version);

    REQUIRE(hashes.size() == 2u);
    CHECK(std::find(hashes.begin(), hashes.end(), block_hash) != hashes.end());
    CHECK(std::find(hashes.begin(), hashes.end(), compact_hash) != hashes.end());
    CHECK(std::find(hashes.begin(), hashes.end(), tx_hash) == hashes.end());
}

TEST_CASE("announcements: an inv of transactions only announces nothing",
          "[announcements]") {
    // Stated rather than implied: this path knows it is not handling
    // transactions, and says so instead of leaving a caller to wonder whether
    // the empty answer meant "none" or "not understood".
    using domain::message::inventory_vector;

    auto const hashes = kth::node::detail::announced_by_inventory(inventory_payload({
        {inventory_vector::type_id::transaction, kth::domain::chain::hash(make_header(31))}}),
        msg_version);

    CHECK(hashes.empty());
}

TEST_CASE("announcements: an unreadable message announces nothing",
          "[announcements]") {
    // Truncated on the wire, or simply not what the command said. Each form
    // answers "nothing", and none of them throws or reads past the buffer.
    data_chunk const rubbish{0xde, 0xad, 0xbe, 0xef};

    CHECK(kth::node::detail::announced_by_headers(rubbish, msg_version).empty());
    CHECK(kth::node::detail::announced_by_compact_block(rubbish, msg_version).empty());
    CHECK(kth::node::detail::announced_by_inventory(rubbish, msg_version).empty());
}

// -----------------------------------------------------------------------------
// The registry: what a burst cannot lose
// -----------------------------------------------------------------------------

TEST_CASE("announcements: eight announcements of one block register it once",
          "[announcements]") {
    // Eight peers announce the same block within a few hundred milliseconds.
    // Coalescing happens here, where nothing can be lost, and not at the
    // doorbell, where a dropped ring would have to be assumed equivalent to the
    // one already queued -- which it is not, since it may name another hash.
    auto const settings = node_settings();
    p2p_node node(settings);

    auto const hash = kth::domain::chain::hash(make_header(41));
    for (int i = 0; i < 8; ++i) {
        node.announce_blocks({hash});
    }

    auto const taken = node.take_announced_blocks();
    REQUIRE(taken.hashes.size() == 1u);
    CHECK(taken.hashes[0] == hash);
    CHECK_FALSE(taken.overflowed);
}

TEST_CASE("announcements: a drain takes everything registered, not one entry",
          "[announcements]") {
    // The doorbell has one slot, so the second ring onwards is dropped. That is
    // only safe because the hashes are registered before the ring and taken all
    // at once: whoever answers the first ring sees what the dropped ones would
    // have announced.
    auto const settings = node_settings();
    p2p_node node(settings);

    auto const first = kth::domain::chain::hash(make_header(51));
    auto const second = kth::domain::chain::hash(make_header(52));
    auto const third = kth::domain::chain::hash(make_header(53));

    node.announce_blocks({first});
    node.announce_blocks({second});
    node.announce_blocks({third});

    // The doorbell is indeed full, which is the situation this is about.
    CHECK_FALSE(node.block_announcements().try_send(std::error_code{}, blocks_announced{}));

    auto const taken = node.take_announced_blocks();
    REQUIRE(taken.hashes.size() == 3u);
    CHECK(std::find(taken.hashes.begin(), taken.hashes.end(), first) != taken.hashes.end());
    CHECK(std::find(taken.hashes.begin(), taken.hashes.end(), second) != taken.hashes.end());
    CHECK(std::find(taken.hashes.begin(), taken.hashes.end(), third) != taken.hashes.end());
    CHECK_FALSE(taken.overflowed);
}

TEST_CASE("announcements: a drain leaves the registry empty", "[announcements]") {
    auto const settings = node_settings();
    p2p_node node(settings);

    node.announce_blocks({kth::domain::chain::hash(make_header(61))});
    REQUIRE(node.take_announced_blocks().hashes.size() == 1u);

    // A second drain answers nothing rather than the same hash again, which is
    // what keeps one announcement from producing two follow-ups.
    CHECK(node.take_announced_blocks().hashes.empty());
}

TEST_CASE("announcements: the registry is bounded and says when it refused",
          "[announcements]") {
    // Peers decide how much arrives here, so the registry has a ceiling and its
    // membership test is a hash lookup rather than a scan. What matters is that
    // running out of room is REPORTED: a refusal dropped in silence could be
    // the one announcement that mattered, and the consumer would go on
    // believing everything it holds is everything that was said.
    auto const settings = node_settings();
    p2p_node node(settings);

    hash_list many;
    many.reserve(2000);
    for (uint32_t i = 0; i < 2000; ++i) {
        many.push_back(kth::domain::chain::hash(make_header(1000 + i)));
    }
    node.announce_blocks(many);

    auto const taken = node.take_announced_blocks();

    // Bounded, and honest about it.
    CHECK(taken.hashes.size() < many.size());
    CHECK(taken.overflowed);

    // And the flag does not outlive the take: the next round starts clean.
    node.announce_blocks({kth::domain::chain::hash(make_header(99))});
    auto const next = node.take_announced_blocks();
    CHECK(next.hashes.size() == 1u);
    CHECK_FALSE(next.overflowed);
}

TEST_CASE("announcements: announcing nothing rings no doorbell", "[announcements]") {
    auto const settings = node_settings();
    p2p_node node(settings);

    node.announce_blocks({});

    CHECK(node.take_announced_blocks().hashes.empty());
    // The slot is free, so nothing was rung.
    CHECK(node.block_announcements().try_send(std::error_code{}, blocks_announced{}));
}

// -----------------------------------------------------------------------------
// Telling a response from an announcement
// -----------------------------------------------------------------------------
//
// Nothing on the wire distinguishes them, and the count does not either: a real
// answer to `getheaders` can carry one header and an announcement can carry
// several. So the answer comes from state only a request can set, claimed once
// because one request is answered by one message.

TEST_CASE("attribution: a headers nobody asked for is never claimed as a response",
          "[announcements][attribution]") {
    ::asio::io_context ioc;
    network::settings config;
    auto peer = make_peer(ioc, config, 9101);

    // No request has been made, so the arriving message cannot be an answer to
    // one. This is what keeps it out of the per-request channel, where it used
    // to wait to be handed to some later request as if it had answered it.
    CHECK_FALSE(peer->claim_headers_response());
}

TEST_CASE("attribution: one request is answered once", "[announcements][attribution]") {
    ::asio::io_context ioc;
    network::settings config;
    auto peer = make_peer(ioc, config, 9102);

    peer->expect_headers_response();

    // The first message is the answer.
    CHECK(peer->claim_headers_response());

    // Everything after it is an announcement, however fast it follows. Without
    // this, a peer that sent its response and then announced a block would have
    // both taken as answers, and the second would sit in the channel.
    CHECK_FALSE(peer->claim_headers_response());
    CHECK_FALSE(peer->claim_headers_response());
}

TEST_CASE("attribution: a timed-out session is never armed again",
          "[announcements][attribution]") {
    // The structural half of the rule. After a timeout we gave up without
    // consuming the answer, so a `headers` may still be in flight with nothing
    // about it to recognise -- no local counter can identify an incoming
    // message. Arming the session again would let that late message answer a
    // request it never saw, so the session is retired instead.
    ::asio::io_context ioc;
    network::settings config;
    auto peer = make_peer(ioc, config, 9103);

    peer->expect_headers_response();
    peer->retire_from_header_requests();

    // Retiring drops the outstanding claim as well: the request that armed it
    // is the one that gave up.
    CHECK_FALSE(peer->claim_headers_response());

    CHECK(peer->retired_from_header_requests());

    // And arming is refused from here on, so the late message can only ever be
    // classified as an announcement.
    peer->expect_headers_response();
    CHECK_FALSE(peer->claim_headers_response());
}

TEST_CASE("attribution: a response claimed just before the timeout drained is harmless",
          "[announcements][attribution]") {
    // The exact interleaving: the read loop claims the arriving `headers`, and
    // before it can put it on the channel the requester times out, retires the
    // session and drains. The claimed message then lands in a channel nobody
    // will ever read.
    //
    // Replayed here through the same primitives the two sides use, in that
    // order, because the read loop itself needs a live connection to drive.
    // What the order has to guarantee is checked directly.
    ::asio::io_context ioc;
    network::settings config;
    auto peer = make_peer(ioc, config, 9301);

    peer->expect_headers_response();

    // Read loop: this message is the answer.
    REQUIRE(peer->claim_headers_response());

    // Requester, in the same instant: gives up, retires the session, drains.
    peer->retire_from_header_requests();
    while (peer->headers_responses().try_receive([](std::error_code, network::raw_message) {})) {
    }

    // Read loop, resuming: it tries to deliver what it claimed, and the door is
    // already shut. Retiring ends the session, and the order inside stop() is
    // what makes this true — close first, so no producer can enqueue any more,
    // then drain what was already stored. Draining before closing would leave a
    // window for exactly this delivery to land in an empty channel and stay
    // there for the life of the session.
    network::raw_message late;
    late.payload = data_chunk{0x01};
    CHECK_FALSE(peer->headers_responses().try_send(std::error_code{}, std::move(late)));

    // Nothing was stored, so there is nothing to hold on to through shutdown.
    CHECK_FALSE(peer->headers_responses().try_receive(
        [](std::error_code, network::raw_message) {}));

    // And it could not have been attributed to anything either: the session is
    // retired, so no request is ever made on it again.
    CHECK(peer->retired_from_header_requests());
    CHECK_FALSE(peer->claim_headers_response());
}

TEST_CASE("attribution: retiring a session ends it, so it is replaced",
          "[announcements][attribution]") {
    // Retiring without ending the session is a trap: it stays connected and
    // keeps its slot, so the connection manager never falls below target and
    // never dials a replacement. With enough timeouts every slot would be held
    // by a peer no header request may use, and header sync would have no
    // eligible peer and no way to ever get one — the shape of #705, rebuilt out
    // of the fix for it.
    ::asio::io_context ioc;
    network::settings config;
    auto peer = make_peer(ioc, config, 9501);

    REQUIRE_FALSE(peer->stopped());

    peer->retire_from_header_requests();

    // Ended. That is what makes a replacement happen, and a replacement is a
    // new session with a new nonce, which is safe to ask again.
    CHECK(peer->stopped());
    CHECK(peer->retired_from_header_requests());
}

TEST_CASE("attribution: every connected peer timing out leaves none of them connected",
          "[announcements][attribution]") {
    // The system-level statement. If all eight sessions time out on
    // `getheaders`, the node must not be left holding eight connections that
    // header sync cannot use: connected > 0 with eligible == 0 and a request
    // owed is only survivable if something can still produce an eligible
    // session, and here that something is the connection manager noticing the
    // slots are free.
    ::asio::io_context ioc;
    network::settings config;

    std::vector<peer_ptr> peers;
    for (uint64_t i = 0; i < 8; ++i) {
        peers.push_back(make_peer(ioc, config, 9600 + i));
    }
    for (auto const& peer : peers) {
        REQUIRE_FALSE(peer->stopped());
    }

    for (auto const& peer : peers) {
        peer->retire_from_header_requests();
    }

    // Not one of them is still occupying a slot.
    for (auto const& peer : peers) {
        CHECK(peer->stopped());
    }
}

TEST_CASE("attribution: retirement belongs to the session, not to the peer",
          "[announcements][attribution]") {
    // "For the life of the session" means exactly that. A peer that reconnects
    // is a new session with a new nonce, and it is a header source again --
    // otherwise one timeout would cost that address permanently, which is not
    // what the rule is for.
    ::asio::io_context ioc;
    network::settings config;

    auto first = make_peer(ioc, config, 9401);
    auto reconnected = make_peer(ioc, config, 9402);

    // Same endpoint: over unconnected sockets both resolve to the same
    // authority, which is what makes this about the session and not the address.
    REQUIRE(first->authority() == reconnected->authority());

    first->retire_from_header_requests();

    CHECK(first->retired_from_header_requests());
    CHECK_FALSE(reconnected->retired_from_header_requests());

    // And the new session can be armed, which the retired one cannot.
    reconnected->expect_headers_response();
    CHECK(reconnected->claim_headers_response());
    CHECK_FALSE(reconnected->stopped());
}

// -----------------------------------------------------------------------------
// Handing a header request to the download task
// -----------------------------------------------------------------------------

TEST_CASE("announcements: a header request waits for room rather than being dropped",
          "[announcements][delivery]") {
    // The request an announcement produces is the last thing that can raise it:
    // its hashes have already been taken off the node by the time this is sent,
    // so a request dropped for want of room is an announcement that nobody will
    // ever hear about again. It waits instead.
    ::asio::io_context ioc;

    kth::node::sync::header_download_input_channel channel(ioc, 1);

    // Full before the request is made.
    REQUIRE(channel.try_send(std::error_code{}, kth::node::sync::stop_request{}));

    auto delivery = ::asio::co_spawn(ioc,
        kth::node::detail::deliver_header_request(channel,
            kth::node::sync::header_request{965489u, null_hash, std::nullopt}),
        ::asio::use_future);
    drain(ioc);

    // Still waiting: nothing was dropped and nothing was reported as sent.
    CHECK(delivery.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout);

    // Room appears.
    std::optional<kth::node::sync::header_download_input> occupant;
    REQUIRE(channel.try_receive([&](std::error_code, kth::node::sync::header_download_input value) {
        occupant = std::move(value);
    }));
    drain(ioc);

    REQUIRE(delivery.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(delivery.get());

    // And the request is there, not lost. With try_send it never arrives.
    std::optional<kth::node::sync::header_download_input> arrived;
    REQUIRE(channel.try_receive([&](std::error_code, kth::node::sync::header_download_input value) {
        arrived = std::move(value);
    }));
    REQUIRE(arrived.has_value());
    auto const* request = std::get_if<kth::node::sync::header_request>(&*arrived);
    REQUIRE(request != nullptr);
    CHECK(request->from_height == 965489u);
}

TEST_CASE("announcements: a closed channel abandons the request instead of waiting",
          "[announcements][delivery]") {
    // The one outcome that gives up. Shutdown closes these channels, and a
    // request that waited for room on a channel nobody will ever read again
    // would hold the coordinator there.
    ::asio::io_context ioc;

    kth::node::sync::header_download_input_channel channel(ioc, 1);
    channel.close();

    auto delivery = ::asio::co_spawn(ioc,
        kth::node::detail::deliver_header_request(channel,
            kth::node::sync::header_request{965489u, null_hash, std::nullopt}),
        ::asio::use_future);
    drain(ioc);

    REQUIRE(delivery.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK_FALSE(delivery.get());
}
