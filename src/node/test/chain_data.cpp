// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include "sync_harness.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/message/block.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/not_found.hpp>
#include <kth/node/handlers/chain_data.hpp>
#include <kth/node/handlers/ping.hpp>
#include <kth/domain/message/ping.hpp>
#include <kth/domain/message/pong.hpp>
#include <kth/node/p2p_node.hpp>

#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;
using node::handlers::chain_data::block_request_allowed;
using node::handlers::chain_data::headers_after_locator;

namespace {

template <typename Awaitable>
auto run_on_own_context(Awaitable&& operation) {
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, std::forward<Awaitable>(operation), ::asio::use_future);
    io.run();
    return result.get();
}

// A trunk of six blocks whose first four are connected and whose last two are
// headers only, plus a stored one-block side branch off height 2.
struct served_chain {
    test::chain_fixture fixture{"chain_data"};
    std::vector<domain::chain::block> trunk;
    domain::chain::block side;

    served_chain() {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        auto& chain = fixture.chain();
        auto const base_time = uint32_t(zulu_time()) - 40u * test::block_spacing;
        auto prev = domain::chain::block::genesis_regtest().hash();
        for (uint32_t h = 1; h <= 6; ++h) {
            trunk.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
        side = test::mine_block(trunk[1].hash(), 3, base_time + 3 * test::block_spacing + 1, 7, {}, 0);

        REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
        (void)fixture.organizer().add_headers(test::headers_of(std::vector{side}));
        REQUIRE(run_on_own_context(chain.store_block(test::to_light(side), 3)) == error::success);
        std::vector<domain::chain::block> const connected(trunk.begin(), trunk.begin() + 4);
        test::persist_headers(fixture, connected, 1);
        test::connect_bodies(fixture, connected, 1);
        REQUIRE(chain.chain_view()->connected_tip_height == 4u);
    }

    blockchain::header_index const& index() { return fixture.chain().headers(); }
    hash_digest tip() { return fixture.chain().chain_view()->tip_hash; }

    hash_list hashes(domain::message::header::list const& headers) const {
        hash_list out;
        for (auto const& header : headers) {
            out.push_back(domain::chain::hash(header));
        }
        return out;
    }

    hash_list trunk_hashes(size_t first_height, size_t last_height) const {
        hash_list out;
        for (auto h = first_height; h <= last_height; ++h) {
            out.push_back(trunk[h - 1].hash());
        }
        return out;
    }
};

hash_digest unknown_hash() {
    hash_digest hash{};
    hash[0] = 0x42;
    return hash;
}

} // namespace

TEST_CASE("headers_after_locator serves the connected chain after the fork point", "[chain_data]") {
    served_chain c;

    // After the first locator entry on the connected chain, up to the
    // connected tip: the headers above it are not offered.
    CHECK(c.hashes(headers_after_locator(c.index(), c.tip(), {c.trunk[1].hash()}, null_hash)) ==
        c.trunk_hashes(3, 4));
    // Entries off the connected chain are skipped: the side block, then a
    // header above the tip.
    CHECK(c.hashes(headers_after_locator(c.index(), c.tip(),
        {c.side.hash(), c.trunk[5].hash(), c.trunk[0].hash()}, null_hash)) == c.trunk_hashes(2, 4));
    // With nothing known, the answer starts after genesis.
    CHECK(c.hashes(headers_after_locator(c.index(), c.tip(), {unknown_hash()}, null_hash)) ==
        c.trunk_hashes(1, 4));
    // The stop hash is included and ends the answer; so does the limit.
    CHECK(c.hashes(headers_after_locator(c.index(), c.tip(), {c.trunk[0].hash()}, c.trunk[2].hash())) ==
        c.trunk_hashes(2, 3));
    CHECK(c.hashes(headers_after_locator(c.index(), c.tip(), {c.trunk[0].hash()}, null_hash, 2)) ==
        c.trunk_hashes(2, 3));
    // A stop hash before the fork point, or off the connected chain, is ignored.
    CHECK(c.hashes(headers_after_locator(c.index(), c.tip(), {c.trunk[1].hash()}, c.trunk[0].hash())) ==
        c.trunk_hashes(3, 4));
    CHECK(c.hashes(headers_after_locator(c.index(), c.tip(), {c.trunk[1].hash()}, c.trunk[5].hash())) ==
        c.trunk_hashes(3, 4));
    // At the tip there is nothing to add.
    CHECK(headers_after_locator(c.index(), c.tip(), {c.trunk[3].hash()}, null_hash).empty());
    CHECK(headers_after_locator(c.index(), c.tip(), {c.trunk[0].hash()}, null_hash, 0).empty());
}

TEST_CASE("headers_after_locator with an empty locator answers the stop header alone", "[chain_data]") {
    served_chain c;
    CHECK(c.hashes(headers_after_locator(c.index(), c.tip(), {}, c.trunk[2].hash())) ==
        c.trunk_hashes(3, 3));
    CHECK(headers_after_locator(c.index(), c.tip(), {}, c.trunk[4].hash()).empty());
    CHECK(headers_after_locator(c.index(), c.tip(), {}, c.side.hash()).empty());
    CHECK(headers_after_locator(c.index(), c.tip(), {}, null_hash).empty());
}

TEST_CASE("block_request_allowed admits only stored blocks on the connected chain", "[chain_data]") {
    served_chain c;
    for (size_t h = 1; h <= 4; ++h) {
        CAPTURE(h);
        CHECK(block_request_allowed(c.index(), c.tip(), c.trunk[h - 1].hash()));
    }
    CHECK_FALSE(block_request_allowed(c.index(), c.tip(), c.side.hash()));
    CHECK_FALSE(block_request_allowed(c.index(), c.tip(), c.trunk[4].hash()));
    CHECK_FALSE(block_request_allowed(c.index(), c.tip(), unknown_hash()));
    CHECK_FALSE(block_request_allowed(c.index(), unknown_hash(), c.trunk[0].hash()));
}

namespace {

// A peer session on one end of a loopback connection; the test reads what is
// sent to the peer off the other end.
struct loopback_peer {
    ::asio::io_context ioc;
    ::asio::io_context client_ioc;
    ::asio::ip::tcp::socket client{client_ioc};
    network::peer_session::ptr session;
    ::asio::executor_work_guard<::asio::io_context::executor_type> guard{::asio::make_work_guard(ioc)};
    std::thread runner;

    loopback_peer() {
        network::settings settings;
        settings.identifier = 0xdab5bffa;   // regtest
        ::asio::ip::tcp::acceptor acceptor(ioc, {::asio::ip::address_v4::loopback(), 0});
        ::asio::ip::tcp::socket server(ioc);
        client.connect(acceptor.local_endpoint());
        acceptor.accept(server);
        session = std::make_shared<network::peer_session>(std::move(server), settings, true);
        runner = std::thread([this] { ioc.run(); });
        ::asio::co_spawn(ioc, [peer = session]() -> ::asio::awaitable<void> {
            (void)co_await peer->run();
        }, ::asio::detached);
    }

    ~loopback_peer() {
        session->stop();
        std::error_code ignored;
        client.close(ignored);
        guard.reset();
        ioc.stop();
        if (runner.joinable()) runner.join();
    }

    loopback_peer(loopback_peer const&) = delete;
    loopback_peer& operator=(loopback_peer const&) = delete;

    struct message {
        std::string command;
        data_chunk payload;
    };

    // Every message sent to the peer until nothing more arrives for `quiet`.
    std::vector<message> received(std::chrono::milliseconds quiet = std::chrono::milliseconds(500)) {
        std::vector<message> out;
        while (true) {
            data_chunk heading(24);
            if ( ! read_exact(heading, std::chrono::steady_clock::now() + quiet)) return out;
            char command[13] = {};
            std::memcpy(command, heading.data() + 4, 12);
            uint32_t length = 0;
            std::memcpy(&length, heading.data() + 16, 4);
            data_chunk payload(length);
            if (length > 0 && ! read_exact(payload, std::chrono::steady_clock::now() + quiet)) return out;
            out.push_back({command, std::move(payload)});
        }
    }

    bool read_exact(data_chunk& out, std::chrono::steady_clock::time_point deadline) {
        while (client.available() < out.size()) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ::asio::read(client, ::asio::buffer(out));
        return true;
    }
};

template <typename Handler>
node::message_result serve(loopback_peer& peer, Handler&& handler) {
    auto result = ::asio::co_spawn(peer.ioc, std::forward<Handler>(handler), ::asio::use_future);
    REQUIRE(result.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    return result.get();
}

} // namespace

TEST_CASE("getheaders is answered with the connected chain after the locator", "[chain_data]") {
    served_chain c;
    loopback_peer peer;
    domain::message::get_headers const request({c.trunk[0].hash()}, null_hash);
    CHECK(serve(peer, node::handlers::chain_data::handle_get_headers(c.fixture.chain(), *peer.session, request)) ==
        node::message_result::handled);

    auto const sent = peer.received();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].command == domain::message::headers::command);
    byte_reader reader(sent[0].payload);
    auto const headers = domain::message::headers::from_data(reader, peer.session->negotiated_version());
    REQUIRE(headers);
    CHECK(c.hashes(headers->elements()) == c.trunk_hashes(2, 4));
}

TEST_CASE("headers_after_locator answers one chain, the one ending at the tip it is given", "[chain_data]") {
    served_chain c;
    // A reorganization changes the tip between a peer's locator and the
    // answer. The answer walks back from the tip it was given, so it is one
    // chain whichever that tip is: from the side branch's tip, a locator on
    // the trunk past the fork is not on it, and the answer starts after
    // genesis and ends at the side block.
    auto const from_side = headers_after_locator(c.index(), c.side.hash(), {c.trunk[3].hash()}, null_hash);
    CHECK(c.hashes(from_side) == hash_list{c.trunk[0].hash(), c.trunk[1].hash(), c.side.hash()});
    for (auto const& tip : {c.tip(), c.side.hash()}) {
        auto const headers = headers_after_locator(c.index(), tip, {c.side.hash(), c.trunk[3].hash()}, null_hash);
        for (size_t i = 1; i < headers.size(); ++i) {
            CHECK(headers[i].previous_block_hash() == domain::chain::hash(headers[i - 1]));
        }
    }
}

TEST_CASE("getheaders from a peer on a branch that lost a reorganization is answered, not refused", "[chain_data]") {
    served_chain c;
    loopback_peer peer;
    // The peer's best is the side block, which the connected chain left.
    domain::message::get_headers const request({c.side.hash(), c.trunk[0].hash()}, null_hash);
    CHECK(serve(peer, node::handlers::chain_data::handle_get_headers(c.fixture.chain(), *peer.session, request)) ==
        node::message_result::handled);

    auto const sent = peer.received();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].command == domain::message::headers::command);
    byte_reader reader(sent[0].payload);
    auto const headers = domain::message::headers::from_data(reader, peer.session->negotiated_version());
    REQUIRE(headers);
    CHECK(c.hashes(headers->elements()) == c.trunk_hashes(2, 4));
}

TEST_CASE("getdata sends the requested connected blocks and a notfound for the rest", "[chain_data]") {
    served_chain c;
    loopback_peer peer;
    using domain::message::inventory_vector;
    using type_id = inventory_vector::type_id;
    auto const request = domain::message::get_data::create({
        {type_id::block, c.trunk[1].hash()},
        {type_id::transaction, c.trunk[0].transactions().front().hash()},   // confirmed, not pooled
        {type_id::block, c.side.hash()},                                    // off the connected chain
        {type_id::compact_block, c.trunk[3].hash()},                        // answered in full
        {type_id::block, c.trunk[4].hash()},                                // header only
        {type_id::block, unknown_hash()}});
    REQUIRE(request);
    CHECK(serve(peer, node::handlers::chain_data::handle_get_data(c.fixture.chain(), *peer.session, *request)) ==
        node::message_result::handled);

    auto const sent = peer.received();
    REQUIRE(sent.size() == 3);
    hash_list blocks;
    for (size_t i = 0; i < 2; ++i) {
        REQUIRE(sent[i].command == domain::message::block::command);
        byte_reader reader(sent[i].payload);
        auto const block = domain::message::block::from_data(reader, peer.session->negotiated_version());
        REQUIRE(block);
        blocks.push_back(block->hash());
    }
    CHECK(blocks == hash_list{c.trunk[1].hash(), c.trunk[3].hash()});

    REQUIRE(sent[2].command == domain::message::not_found::command);
    byte_reader reader(sent[2].payload);
    auto const missing = domain::message::not_found::from_data(reader, peer.session->negotiated_version());
    REQUIRE(missing);
    CHECK(missing->inventories() == inventory_vector::list{
        {type_id::transaction, c.trunk[0].transactions().front().hash()},
        {type_id::block, c.side.hash()},
        {type_id::block, c.trunk[4].hash()},
        {type_id::block, unknown_hash()}});
}

TEST_CASE("getheaders with a locator longer than BCHN accepts disconnects the peer", "[chain_data]") {
    served_chain c;
    loopback_peer peer;
    hash_list locator(max_get_headers_locator, c.trunk[0].hash());
    domain::message::get_headers const accepted(locator, null_hash);
    CHECK(serve(peer, node::handlers::chain_data::handle_get_headers(c.fixture.chain(), *peer.session, accepted)) ==
        node::message_result::handled);

    locator.push_back(c.trunk[0].hash());
    domain::message::get_headers const refused(locator, null_hash);
    CHECK(serve(peer, node::handlers::chain_data::handle_get_headers(c.fixture.chain(), *peer.session, refused)) ==
        node::message_result::disconnect);

    // Only the accepted request was answered.
    auto const sent = peer.received();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].command == domain::message::headers::command);
}

namespace {

// A session whose writer has not started: what is queued for the peer stays
// queued, as for a peer that is not reading. The test holds the other end of
// the socket and starts the writer when it wants to read.
struct idle_peer {
    ::asio::io_context io;
    ::asio::io_context client_io;
    ::asio::ip::tcp::socket client{client_io};
    network::peer_session::ptr session;

    idle_peer() {
        network::settings settings;
        settings.identifier = 0xdab5bffa;   // regtest
        ::asio::ip::tcp::acceptor acceptor(io, {::asio::ip::address_v4::loopback(), 0});
        ::asio::ip::tcp::socket server(io);
        client.connect(acceptor.local_endpoint());
        acceptor.accept(server);
        session = std::make_shared<network::peer_session>(std::move(server), settings, true);
    }

    template <typename Message>
    network::raw_message wire(Message const& message) const {
        network::raw_message raw;
        raw.payload = to_data_chunk(message, session->negotiated_version());
        raw.heading = domain::message::heading(0xdab5bffa, Message::command, uint32_t(raw.payload.size()), 0);
        return raw;
    }

    // The command of each message the writer puts on the socket, `count` of them.
    std::vector<std::string> read_commands(size_t count) {
        std::vector<std::string> out;
        for (size_t i = 0; i < count; ++i) {
            data_chunk heading(24);
            ::asio::read(client, ::asio::buffer(heading));
            char command[13] = {};
            std::memcpy(command, heading.data() + 4, 12);
            uint32_t length = 0;
            std::memcpy(&length, heading.data() + 16, 4);
            data_chunk payload(length);
            if (length > 0) ::asio::read(client, ::asio::buffer(payload));
            out.emplace_back(command);
        }
        return out;
    }
};

void register_chain_data(node::message_dispatcher& dispatcher, blockchain::block_chain& chain) {
    dispatcher.register_served_handler<domain::message::get_headers>(
        [&chain](network::peer_session& peer, domain::message::get_headers const& request) {
            return node::handlers::chain_data::handle_get_headers(chain, peer, request);
        });
    dispatcher.register_served_handler<domain::message::get_data>(
        [&chain](network::peer_session& peer, domain::message::get_data const& request) {
            return node::handlers::chain_data::handle_get_data(chain, peer, request);
        });
    dispatcher.register_handler<domain::message::ping>(node::handlers::ping::handle);
}

} // namespace

TEST_CASE("serving blocks to a peer that is not reading does not hold its pings", "[chain_data][service]") {
    served_chain c;
    idle_peer peer;
    node::message_dispatcher dispatcher;
    register_chain_data(dispatcher, c.fixture.chain());

    // More blocks requested than any outbound queue holds while nothing
    // drains it, so serving them waits.
    using type_id = domain::message::inventory_vector::type_id;
    domain::message::inventory_vector::list wanted;
    for (size_t round = 0; round < 30; ++round) {
        for (size_t h = 1; h <= 4; ++h) {
            wanted.emplace_back(type_id::block, c.trunk[h - 1].hash());
        }
    }
    auto const request = domain::message::get_data::create(wanted);
    REQUIRE(request);

    auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
    // The read loop's order: the request, then a ping.
    auto reading = ::asio::co_spawn(peer.io, [&]() -> ::asio::awaitable<bool> {
        if ( ! co_await dispatcher.dispatch(*peer.session, peer.wire(*request))) co_return false;
        co_return co_await dispatcher.dispatch(*peer.session, peer.wire(domain::message::ping{7}));
    }, ::asio::use_future);
    // Until the service has filled the data queue and waits on it.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (reading.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
        peer.io.poll();
    }

    // The read loop went on to the ping and answered it, while the service
    // is still waiting to queue the blocks.
    REQUIRE(reading.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    CHECK(reading.get());
    CHECK(service.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);

    // Once the peer reads, the pong goes out first, then every block in order.
    std::thread runner([&] {
        ::asio::co_spawn(peer.io, [session = peer.session]() -> ::asio::awaitable<void> {
            (void)co_await session->run();
        }, ::asio::detached);
        peer.io.run();
    });
    auto const commands = peer.read_commands(1 + wanted.size());
    CHECK(commands.front() == domain::message::pong::command);
    CHECK(std::count(commands.begin() + 1, commands.end(), domain::message::block::command) ==
        std::ptrdiff_t(wanted.size()));

    // Stopping the session ends the service: nothing is left behind.
    peer.session->stop();
    runner.join();
    REQUIRE(service.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    service.get();
}

TEST_CASE("a peer with too many requests pending is disconnected, and the service ends on stop",
          "[chain_data][service]") {
    served_chain c;
    idle_peer peer;
    node::message_dispatcher dispatcher;
    register_chain_data(dispatcher, c.fixture.chain());
    auto const request = peer.wire(domain::message::get_headers({c.trunk[0].hash()}, null_hash));

    // No service running: what is admitted stays queued.
    auto admitted = ::asio::co_spawn(peer.io, [&]() -> ::asio::awaitable<size_t> {
        size_t count = 0;
        while (count <= network::peer_session::served_requests_capacity &&
               co_await dispatcher.dispatch(*peer.session, request)) {
            ++count;
        }
        co_return count;
    }, ::asio::use_future);
    peer.io.run();
    CHECK(admitted.get() == network::peer_session::served_requests_capacity);

    // A service waiting on an empty queue is woken by the stop.
    std::error_code ec;
    while (peer.session->served_requests().try_receive(ec)) {}
    peer.io.restart();
    auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
    peer.io.poll();
    CHECK(service.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    peer.session->stop();
    peer.io.run();
    REQUIRE(service.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    service.get();
}
