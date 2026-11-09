// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>

#include <kth/blockchain/header_index.hpp>
#include <kth/domain/message/compact_block.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/send_compact.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sync/tip_announcement.hpp>

using namespace kth;
using node::sync::max_blocks_to_announce;
using node::sync::next_tip_announcement;

namespace {

// A trunk of twenty headers from genesis and a five-header branch off height
// ten, in a standalone index.
struct indexed_chain {
    blockchain::header_index index{100};
    hash_list trunk;     // trunk[h] is the header at height h
    hash_list branch;    // branch[i] is at height 11 + i

    indexed_chain() {
        auto prev = null_hash;
        for (uint32_t h = 0; h <= 20; ++h) {
            trunk.push_back(add(prev, h, 0));
            prev = trunk.back();
        }
        prev = trunk[10];
        for (uint32_t h = 11; h <= 15; ++h) {
            branch.push_back(add(prev, h, 1));
            prev = branch.back();
        }
    }

    hash_digest add(hash_digest const& prev, uint32_t height, uint32_t fork) {
        domain::chain::header const header{1, prev, null_hash, height * 600, 0x207fffff, height + fork * 1000};
        auto const hash = domain::chain::hash(header);
        REQUIRE(index.add(hash, header).inserted);
        return hash;
    }

    hash_list hashes(domain::message::header::list const& headers) const {
        hash_list out;
        for (auto const& header : headers) {
            out.push_back(domain::chain::hash(header));
        }
        return out;
    }
};

} // namespace

TEST_CASE("a tip a few blocks ahead is announced with the headers since the last one", "[tip_announcement]") {
    indexed_chain c;
    auto const one = next_tip_announcement(c.index, c.trunk[10], c.trunk[11]);
    REQUIRE(one);
    CHECK(one->tip == c.trunk[11]);
    CHECK(c.hashes(one->headers) == hash_list{c.trunk[11]});

    auto const several = next_tip_announcement(c.index, c.trunk[10], c.trunk[10 + max_blocks_to_announce]);
    REQUIRE(several);
    CHECK(c.hashes(several->headers) ==
        hash_list(c.trunk.begin() + 11, c.trunk.begin() + 11 + max_blocks_to_announce));
}

TEST_CASE("a tip the headers would not connect for goes out as an inv", "[tip_announcement]") {
    indexed_chain c;
    // More than eight new blocks.
    auto const far = next_tip_announcement(c.index, c.trunk[10], c.trunk[11 + max_blocks_to_announce]);
    REQUIRE(far);
    CHECK(far->tip == c.trunk[11 + max_blocks_to_announce]);
    CHECK(far->headers.empty());
    // A reorganization onto a longer branch.
    auto const reorg = next_tip_announcement(c.index, c.trunk[12], c.branch[4]);
    REQUIRE(reorg);
    CHECK(reorg->headers.empty());
    // A sibling at the same height.
    auto const sibling = next_tip_announcement(c.index, c.trunk[11], c.branch[0]);
    REQUIRE(sibling);
    CHECK(sibling->headers.empty());
    // Nothing announced yet, or a last announcement the index does not hold.
    auto const first = next_tip_announcement(c.index, std::nullopt, c.trunk[5]);
    REQUIRE(first);
    CHECK(first->headers.empty());
    hash_digest unknown{};
    unknown[0] = 0x42;
    auto const forgotten = next_tip_announcement(c.index, unknown, c.trunk[5]);
    REQUIRE(forgotten);
    CHECK(forgotten->headers.empty());
}

TEST_CASE("a tip that did not move forward is not announced", "[tip_announcement]") {
    indexed_chain c;
    CHECK_FALSE(next_tip_announcement(c.index, c.trunk[12], c.trunk[12]));
    // Stepped back onto an ancestor, as after a rejected block.
    CHECK_FALSE(next_tip_announcement(c.index, c.trunk[12], c.trunk[11]));
    hash_digest unknown{};
    unknown[0] = 0x42;
    CHECK_FALSE(next_tip_announcement(c.index, c.trunk[12], unknown));
}

namespace {

// A session whose writer never runs: nothing queued for the peer leaves.
struct idle_session {
    ::asio::io_context io;
    ::asio::io_context client_io;
    ::asio::ip::tcp::socket client{client_io};
    network::peer_session::ptr session;

    idle_session() {
        network::settings settings;
        settings.identifier = 0xdab5bffa;   // regtest
        ::asio::ip::tcp::acceptor acceptor(io, {::asio::ip::address_v4::loopback(), 0});
        ::asio::ip::tcp::socket server(io);
        client.connect(acceptor.local_endpoint());
        acceptor.accept(server);
        session = std::make_shared<network::peer_session>(std::move(server), settings, true);
    }
};

} // namespace

TEST_CASE("tips owed to a peer that is not reading are one debt, not one task per block",
          "[tip_announcement]") {
    idle_session peer;
    auto const baseline = peer.session.use_count();
    hash_digest tip{};
    for (size_t i = 0; i < 128; ++i) {
        tip[0] = uint8_t(i);
        peer.session->announce_tip(tip);
    }
    peer.io.poll();
    // Nothing was started that holds the session, and only the latest tip is owed.
    CHECK(peer.session.use_count() == baseline);
    std::error_code ec;
    auto const owed = peer.session->tip_announcements().try_receive(ec);
    REQUIRE(owed);
    CHECK(*owed == tip);
    CHECK_FALSE(peer.session->tip_announcements().try_receive(ec));
    peer.session->stop();
}

TEST_CASE("the peer's service announces the latest owed tip from what it last announced",
          "[tip_announcement]") {
    indexed_chain c;
    idle_session peer;
    peer.session->set_prefers_headers(true);
    node::message_dispatcher dispatcher;
    dispatcher.set_tip_announcement_handler(
        [&c](network::peer_session& session, hash_digest const& tip, std::optional<hash_digest>& last) {
            return node::sync::announce_tip(c.index, session, tip, last);
        });

    // Two tips owed before the service runs: one announcement, of the
    // latest. Nothing was announced to this peer before, so it is taken to
    // hold that tip's parent and gets the one header.
    peer.session->announce_tip(c.trunk[10]);
    peer.session->announce_tip(c.trunk[11]);
    auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
    peer.io.poll();
    std::error_code ec;
    auto const first = peer.session->data_outbound().try_receive(ec);
    REQUIRE(first);
    CHECK_FALSE(peer.session->data_outbound().try_receive(ec));

    // Then a tip nine blocks past the last announced one: an inv.
    peer.session->announce_tip(c.trunk[20]);
    peer.io.poll();
    auto const second = peer.session->data_outbound().try_receive(ec);
    REQUIRE(second);

    peer.session->stop();
    peer.io.run();
    REQUIRE(service.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    service.get();

    auto const command_of = [](data_chunk const& raw) {
        char command[13] = {};
        std::memcpy(command, raw.data() + 4, 12);
        return std::string(command);
    };
    REQUIRE(command_of(*first) == domain::message::headers::command);
    byte_reader reader(*first);
    REQUIRE(reader.skip(24));
    auto const headers = domain::message::headers::from_data(reader, peer.session->negotiated_version());
    REQUIRE(headers);
    CHECK(c.hashes(headers->elements()) == hash_list{c.trunk[11]});
    CHECK(command_of(*second) == domain::message::inventory::command);
}

// -----------------------------------------------------------------------------
// Compact blocks (BIP152)
// -----------------------------------------------------------------------------

namespace {

std::string command_of(data_chunk const& raw) {
    char command[13] = {};
    std::memcpy(command, raw.data() + 4, 12);
    return std::string(command);
}

} // namespace

TEST_CASE("sendcmpct records the high-bandwidth flag of a version 1 request only",
          "[tip_announcement][compact_blocks]") {
    idle_session peer;
    network::settings settings;
    settings.identifier = 0xdab5bffa;   // regtest
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.hosts_file = "/tmp/kth_sendcmpct_hosts_nonexistent.dat";
    settings.banlist_file = "/tmp/kth_sendcmpct_banlist_nonexistent.dat";
    node::p2p_node p2p(settings);

    // Through the dispatcher the read loop hands every message to.
    auto const receive = [&](bool high_bandwidth, uint64_t version) {
        domain::message::send_compact const message(high_bandwidth, version);
        network::raw_message raw;
        raw.payload = to_data_chunk(message, peer.session->negotiated_version());
        raw.heading = domain::message::heading(0xdab5bffa, domain::message::send_compact::command,
            uint32_t(raw.payload.size()), 0);
        auto dispatched = ::asio::co_spawn(peer.io, p2p.dispatcher().dispatch(*peer.session, raw),
            ::asio::use_future);
        peer.io.restart();
        peer.io.run();
        return dispatched.get();
    };

    CHECK_FALSE(peer.session->prefers_compact_blocks());
    // Another version says nothing about the one this node speaks.
    CHECK(receive(true, 2));
    CHECK_FALSE(peer.session->prefers_compact_blocks());
    CHECK(receive(true, 1));
    CHECK(peer.session->prefers_compact_blocks());
    CHECK(receive(false, 2));
    CHECK(peer.session->prefers_compact_blocks());
    // Low bandwidth after high: announcements go back to headers or inv.
    CHECK(receive(false, 1));
    CHECK_FALSE(peer.session->prefers_compact_blocks());

    // Malformed: a mode that is neither 0 nor 1, or a version cut short.
    // Neither parses, and neither changes what was recorded.
    CHECK(receive(true, 1));
    for (auto const& payload : {data_chunk{2, 1, 0, 0, 0, 0, 0, 0, 0}, data_chunk{0, 1, 0, 0, 0, 0, 0, 0}}) {
        network::raw_message raw;
        raw.payload = payload;
        raw.heading = domain::message::heading(0xdab5bffa, domain::message::send_compact::command,
            uint32_t(raw.payload.size()), 0);
        auto dispatched = ::asio::co_spawn(peer.io, p2p.dispatcher().dispatch(*peer.session, raw),
            ::asio::use_future);
        peer.io.restart();
        peer.io.run();
        CHECK(dispatched.get());
        CHECK(peer.session->prefers_compact_blocks());
    }
    peer.session->stop();
}

TEST_CASE("a peer that prefers compact blocks is announced the one new block as a cmpctblock",
          "[tip_announcement][compact_blocks]") {
    indexed_chain c;
    idle_session peer;
    peer.session->set_prefers_headers(true);
    peer.session->set_prefers_compact_blocks(true);

    // What full_node's cache gives: here, a cmpctblock of the header alone.
    size_t built = 0;
    node::sync::compact_block_source const compact =
        [&](hash_digest const& hash) -> ::asio::awaitable<domain::message::compact_block::const_ptr> {
            ++built;
            co_return std::make_shared<domain::message::compact_block const>(
                c.index.get_header(c.index.find(hash)), 7u,
                domain::message::compact_block::short_id_list{},
                domain::message::prefilled_transaction::list{});
        };
    node::sync::compact_block_source const none =
        [](hash_digest const&) -> ::asio::awaitable<domain::message::compact_block::const_ptr> {
            co_return nullptr;
        };

    // Announce `tip` after `last` and answer the command that went out.
    std::optional<hash_digest> last = c.trunk[10];
    auto const announce = [&](hash_digest const& tip, node::sync::compact_block_source source) {
        auto done = ::asio::co_spawn(peer.io,
            node::sync::announce_tip(c.index, *peer.session, tip, last, std::move(source)),
            ::asio::use_future);
        peer.io.restart();
        peer.io.run();
        done.get();
        std::error_code ec;
        auto const sent = peer.session->data_outbound().try_receive(ec);
        REQUIRE(sent);
        CHECK_FALSE(peer.session->data_outbound().try_receive(ec));
        return command_of(*sent);
    };

    // One block past the last announcement: its cmpctblock.
    CHECK(announce(c.trunk[11], compact) == domain::message::compact_block::command);
    CHECK(built == 1);
    // Two past: the peer could not connect it, so headers, and none is built.
    CHECK(announce(c.trunk[13], compact) == domain::message::headers::command);
    CHECK(built == 1);
    // No cmpctblock to give, or nothing to give one: headers.
    CHECK(announce(c.trunk[14], none) == domain::message::headers::command);
    CHECK(announce(c.trunk[15], {}) == domain::message::headers::command);
    // A peer that asked for low bandwidth: headers, and none is built.
    peer.session->set_prefers_compact_blocks(false);
    CHECK(announce(c.trunk[16], compact) == domain::message::headers::command);
    CHECK(built == 1);
    peer.session->stop();
}
