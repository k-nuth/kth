// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <kth/domain/message/compact_block.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/node/detail/block_announcements.hpp>
#include <kth/node/detail/header_ingest.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/network/settings.hpp>

#include "header_peer_harness.hpp"

using namespace kth;
using namespace kth::node;
using namespace kth::test;

// =============================================================================
// Reading headers and block announcements, and where they go (#706, #710)
// =============================================================================
//
// `headers` and `cmpctblock` carry block headers; `inv` carries only hashes.
// Every header goes to the node's one header ingest, answer or not, because
// the wire cannot tell the two apart. The hashes of an `inv` are registered as
// announcements.
//
// What each form understands is stated here without a peer, a socket or a node
// in the way; the registry is exercised on its own, because what matters about
// it is what a burst cannot lose; and the routing is driven through the same
// entry point the read loop calls.

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

TEST_CASE("announcements: a headers carries every header in it, in order",
          "[announcements]") {
    auto const a = make_header(1);
    auto const b = make_header(2);
    auto const c = make_header(3);

    auto const carried = kth::node::detail::headers_carried(headers_payload({a, b, c}), msg_version);

    REQUIRE(carried.has_value());
    REQUIRE(carried->size() == 3u);
    CHECK(kth::domain::chain::hash((*carried)[0]) == kth::domain::chain::hash(a));
    CHECK(kth::domain::chain::hash((*carried)[1]) == kth::domain::chain::hash(b));
    CHECK(kth::domain::chain::hash((*carried)[2]) == kth::domain::chain::hash(c));
}

TEST_CASE("announcements: an empty headers is read as empty, not as unreadable",
          "[announcements]") {
    // An empty `headers` is how a peer says it has nothing past our locator,
    // and the walk needs to hear exactly that. Unreadable is a different
    // answer: nothing to report at all.
    auto const empty = kth::node::detail::headers_carried(headers_payload({}), msg_version);
    REQUIRE(empty.has_value());
    CHECK(empty->empty());
}

TEST_CASE("announcements: a cmpctblock carries the header it opens with",
          "[announcements]") {
    // The whole of BIP152 this needs. The compact body is not read and no
    // reconstruction is implied -- but the message is no longer accepted and
    // dropped, which is what left eight announcements per block unobserved.
    auto const header = make_header(11);

    auto const carried = kth::node::detail::header_of_compact_block(compact_block_payload(header), msg_version);

    REQUIRE(carried.has_value());
    CHECK(kth::domain::chain::hash(*carried) == kth::domain::chain::hash(header));
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

    CHECK_FALSE(kth::node::detail::headers_carried(rubbish, msg_version).has_value());
    CHECK_FALSE(kth::node::detail::header_of_compact_block(rubbish, msg_version).has_value());
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
// Routing: what the read loop does with each, through its own entry point
// -----------------------------------------------------------------------------

namespace {

network::raw_message wire(std::string const& command, data_chunk payload) {
    network::raw_message raw;
    raw.heading = domain::message::heading(0xdab5bffa, command, uint32_t(payload.size()), 0);
    raw.payload = std::move(payload);
    return raw;
}

// Headers that do not chain: the ingest does not validate, so they need not.
std::vector<domain::chain::header> some_headers(uint32_t first, size_t count) {
    std::vector<domain::chain::header> out;
    for (size_t i = 0; i < count; ++i) {
        out.push_back(make_header(first + uint32_t(i)));
    }
    return out;
}

// What the read loop handed the ingest, in order: the node's command channel,
// drained. No ingest task runs in these controls, so it all is still there.
std::vector<kth::node::detail::ingest_submit> submitted(p2p_node& node) {
    std::vector<kth::node::detail::ingest_submit> out;
    while (node.header_ingest().commands.try_receive(
               [&](std::error_code, kth::node::detail::ingest_command command) {
                   if (auto* submit = std::get_if<kth::node::detail::ingest_submit>(&command)) {
                       out.push_back(std::move(*submit));
                   }
               })) {
    }
    return out;
}

bool debt_rung(p2p_node& node) {
    return node.header_ingest().debt.try_receive([](std::error_code, kth::node::detail::headers_owed) {});
}

} // namespace

TEST_CASE("routing: every headers goes to the ingest and is consumed, asked for or not",
          "[announcements][routing]") {
    // The context first: the node's channels hold the sessions, and they must
    // go before the context their strands live on.
    ::asio::io_context ioc;
    auto const settings = node_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9401);

    // Nothing anywhere says whether this answers a getheaders. It does not
    // matter: it is consumed and submitted either way.
    CHECK(node.observe_block_message(peer, wire(domain::message::headers::command, headers_payload(some_headers(1, 3)))));
    // An empty one too: it is the walk's "nothing past your tip".
    CHECK(node.observe_block_message(peer, wire(domain::message::headers::command, headers_payload({}))));

    auto const got = submitted(node);
    REQUIRE(got.size() == 2u);
    CHECK(got[0].peer == peer);
    CHECK(got[0].headers.size() == 3u);
    CHECK(got[1].headers.empty());

    // Nothing was registered as an announcement: a header goes to one place.
    CHECK(node.take_announced_blocks().hashes.empty());
    CHECK_FALSE(debt_rung(node));
}

TEST_CASE("routing: a cmpctblock header goes to the ingest and the message carries on",
          "[announcements][routing]") {
    ::asio::io_context ioc;
    auto const settings = node_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9402);

    auto const header = make_header(77);
    CHECK_FALSE(node.observe_block_message(peer, wire(domain::message::compact_block::command, compact_block_payload(header))));

    auto const got = submitted(node);
    REQUIRE(got.size() == 1u);
    REQUIRE(got[0].headers.size() == 1u);
    CHECK(kth::domain::chain::hash(got[0].headers[0]) == kth::domain::chain::hash(header));
    CHECK(node.take_announced_blocks().hashes.empty());
}

TEST_CASE("routing: an inv registers its blocks, carries on, and submits no headers",
          "[announcements][routing]") {
    using domain::message::inventory_vector;
    ::asio::io_context ioc;
    auto const settings = node_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9403);

    auto const hash = kth::domain::chain::hash(make_header(88));
    CHECK_FALSE(node.observe_block_message(peer,
        wire(domain::message::inventory::command, inventory_payload({{inventory_vector::type_id::block, hash}}))));

    auto const taken = node.take_announced_blocks();
    REQUIRE(taken.hashes.size() == 1u);
    CHECK(taken.hashes[0] == hash);
    CHECK(submitted(node).empty());
}

TEST_CASE("routing: other commands and unreadable headers are left alone",
          "[announcements][routing]") {
    ::asio::io_context ioc;
    auto const settings = node_settings();
    p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9404);

    CHECK_FALSE(node.observe_block_message(peer, wire(domain::message::block::command, data_chunk{1, 2, 3})));
    CHECK_FALSE(node.observe_block_message(peer, wire(domain::message::address::command, data_chunk{})));

    // Consumed — it is a `headers` — but there is nothing to submit, and no
    // debt either: an unreadable message carried no header we lost.
    CHECK(node.observe_block_message(peer, wire(domain::message::headers::command, data_chunk{0xde, 0xad})));
    CHECK(submitted(node).empty());
    CHECK_FALSE(debt_rung(node));
}

TEST_CASE("routing: a submission the ingest cannot take returns at once, owed and unscored",
          "[announcements][routing][saturation]") {
    // The read loop calls this and must never wait on it. With nothing taking
    // submissions the command channel fills; the next is refused on the spot —
    // our pressure, not the peer's misbehaviour — and rung as a debt.
    ::asio::io_context ioc;
    auto const settings = node_settings();
    p2p_node node(settings);
    auto flooding = make_peer(ioc, settings, 9405);

    auto const capacity = kth::node::detail::header_ingest_channels::command_capacity;
    for (size_t i = 0; i < capacity; ++i) {
        CHECK(node.observe_block_message(flooding,
            wire(domain::message::headers::command, headers_payload(some_headers(100 + uint32_t(i) * 10, 1)))));
    }
    REQUIRE_FALSE(debt_rung(node));

    // One past capacity: consumed, refused, owed — and returned.
    CHECK(node.observe_block_message(flooding,
        wire(domain::message::headers::command, headers_payload(some_headers(5, 2)))));
    CHECK(debt_rung(node));
    CHECK(submitted(node).size() == capacity);

    // Zero score, on the session and in the database.
    CHECK(flooding->misbehavior_score() == 0);
    auto const record = node.peer_db().get(flooding->authority());
    CHECK(( ! record || record->reputation_score == 0));

    // Faithful negation: the same lookup does see a score when one is
    // reported, so the zero above is a measurement and not a blind spot.
    (void)node.report_misbehavior(flooding, 10, "control");
    auto const reported = node.peer_db().get(flooding->authority());
    REQUIRE(reported.has_value());
    CHECK(reported->reputation_score == 10);
}
