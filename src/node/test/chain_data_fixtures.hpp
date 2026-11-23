// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// TEST-ONLY. Not installed, not compiled into any library.
//
// What the tests of the served chain data stand on: a chain with a connected
// trunk and a side branch, and a peer session on one end of a loopback
// connection whose other end the test reads.
//
// Everything here lives in an anonymous namespace, so including it from two
// translation units is not an ODR problem.

#ifndef KTH_NODE_TEST_CHAIN_DATA_FIXTURES_HPP
#define KTH_NODE_TEST_CHAIN_DATA_FIXTURES_HPP

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
#include <kth/domain/message/merkle_block.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/domain/message/not_found.hpp>
#include <kth/node/handlers/chain_data.hpp>
#include <kth/node/handlers/ping.hpp>
#include <kth/domain/message/ping.hpp>
#include <kth/domain/message/pong.hpp>
#include <kth/node/p2p_node.hpp>

#include "../../blockchain/test/reorg_chain_fixture.hpp"

namespace kth::test {
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
    chain_fixture fixture{"chain_data"};
    std::vector<domain::chain::block> trunk;
    domain::chain::block side;

    served_chain() {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        auto& chain = fixture.chain();
        auto const base_time = uint32_t(zulu_time()) - 40u * block_spacing;
        auto prev = domain::chain::block::genesis_regtest().hash();
        for (uint32_t h = 1; h <= 6; ++h) {
            trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
        side = mine_block(trunk[1].hash(), 3, base_time + 3 * block_spacing + 1, 7, {}, 0);

        REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk.size());
        (void)fixture.organizer().add_headers(headers_of(std::vector{side}));
        REQUIRE(run_on_own_context(chain.store_block(to_light(side), 3)) == error::success);
        std::vector<domain::chain::block> const connected(trunk.begin(), trunk.begin() + 4);
        persist_headers(fixture, connected, 1);
        connect_bodies(fixture, connected, 1);
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
} // namespace kth::test

#endif // KTH_NODE_TEST_CHAIN_DATA_FIXTURES_HPP
