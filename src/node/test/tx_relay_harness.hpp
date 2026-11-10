// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// TEST-ONLY. Not installed, not compiled into any library.
//
// Peers for the transaction relay controls: real sessions over loopback, whose
// getdata can be read off the wire as a peer would read it.
//
// Everything here is `inline` or lives in an anonymous namespace, so including
// it from two translation units is not an ODR problem.

#ifndef KTH_NODE_TEST_TX_RELAY_HARNESS_HPP
#define KTH_NODE_TEST_TX_RELAY_HARNESS_HPP

#include <test_helpers.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>

#include <kth/domain/message/get_data.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/settings.hpp>

namespace kth::test {
namespace {

using namespace std::chrono_literals;

network::settings relay_settings() {
    network::settings settings;
    settings.inbound_port = 0;
    settings.inbound_connections = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.hosts_file = "/tmp/kth_tx_relay_hosts_nonexistent.dat";
    settings.banlist_file = "/tmp/kth_tx_relay_banlist_nonexistent.dat";
    return settings;
}

// An outbound full node: asked without delay.
void make_preferred(network::peer_session::ptr const& peer) {
    auto version = std::make_shared<domain::message::version>();
    version->set_services(domain::message::version::service::node_network);
    peer->set_peer_version(version);
}

// A session on one end of a loopback connection. Its write loop runs only
// once read_requests() starts it, so until then what is sent waits queued.
struct live_peer {
    ::asio::ip::tcp::socket client;
    network::peer_session::ptr session;
    bool running{false};
    data_chunk bytes;
};

live_peer connect(::asio::io_context& ioc, network::settings const& config, uint64_t nonce) {
    ::asio::ip::tcp::acceptor acceptor(ioc, {::asio::ip::address_v4::loopback(), 0});
    ::asio::ip::tcp::socket client(ioc);
    ::asio::ip::tcp::socket server(ioc);
    client.connect(acceptor.local_endpoint());
    acceptor.accept(server);
    auto session = std::make_shared<network::peer_session>(std::move(server), config, false);
    session->set_nonce(nonce);
    make_preferred(session);
    return {std::move(client), std::move(session)};
}

// The transactions each getdata the peer was sent asks for, after running
// `ioc` for `wait` (or until `expected` getdata arrived).
std::vector<hash_list> read_requests(::asio::io_context& ioc, live_peer& peer,
                                     std::chrono::milliseconds wait, size_t expected = 1) {
    if ( ! peer.running) {
        ::asio::co_spawn(ioc, [session = peer.session]() -> ::asio::awaitable<void> {
            (void)co_await session->run();
        }, ::asio::detached);
        peer.running = true;
    }

    std::vector<hash_list> out;
    auto const deadline = std::chrono::steady_clock::now() + wait;
    while (std::chrono::steady_clock::now() < deadline) {
        ioc.restart();
        ioc.run_for(5ms);
        while (peer.client.available() > 0) {
            data_chunk more(peer.client.available());
            ::asio::read(peer.client, ::asio::buffer(more));
            peer.bytes.insert(peer.bytes.end(), more.begin(), more.end());
        }
        out.clear();
        size_t at = 0;
        while (at + 24 <= peer.bytes.size()) {
            char command[13] = {};
            std::memcpy(command, peer.bytes.data() + at + 4, 12);
            uint32_t length = 0;
            std::memcpy(&length, peer.bytes.data() + at + 16, 4);
            if (at + 24 + length > peer.bytes.size()) break;
            if (std::string(command) == domain::message::get_data::command) {
                data_chunk payload(peer.bytes.begin() + long(at + 24),
                                   peer.bytes.begin() + long(at + 24 + length));
                byte_reader reader(payload);
                auto parsed = domain::message::get_data::from_data(reader, peer.session->negotiated_version());
                REQUIRE(parsed.has_value());
                hash_list txids;
                parsed->to_hashes(txids, domain::message::inventory_vector::type_id::transaction);
                out.push_back(std::move(txids));
            }
            at += 24 + length;
        }
        if (out.size() >= expected) {
            break;
        }
    }
    return out;
}

bool contains(std::vector<hash_list> const& requests, hash_digest const& txid) {
    for (auto const& txids : requests) {
        if (std::find(txids.begin(), txids.end(), txid) != txids.end()) {
            return true;
        }
    }
    return false;
}

} // namespace
} // namespace kth::test

#endif // KTH_NODE_TEST_TX_RELAY_HARNESS_HPP
