// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license, see LICENSE.

// Test-only native peer for real-browser transport/discovery interoperability.
#include <kth/node/p2p_node.hpp>
#include <kth/node/handlers/chain_data.hpp>
#include <kth/node/detail/header_ingest.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include "../../blockchain/test/reorg_chain_fixture.hpp"
#include "../../blockchain/test/regtest_miner.hpp"

#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <asio/use_future.hpp>
#include <csignal>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: kth_ws_fixture TCP_PORT WS_PORT ORIGIN [WS_URL ...]\n";
        return 2;
    }
    using namespace kth;
    auto tcp_port = static_cast<uint16_t>(std::stoul(argv[1]));
    auto ws_port = static_cast<uint16_t>(std::stoul(argv[2]));
    std::string tag = "websocket_" + std::to_string(ws_port);
    test::chain_fixture fixture(tag.c_str());
    if (!fixture.created() || !fixture.start()) return 3;
    domain::chain::header::list headers;
    auto previous = domain::chain::hash(domain::chain::block::genesis_regtest().header());
    for (uint32_t height = 1; height <= 3; ++height) {
        auto block = test::mine_block(previous, height, 1296688602 + height * 600, height, {}, 0);
        headers.push_back(block.header());
        previous = domain::chain::hash(headers.back());
    }
    domain::message::header::list announced(headers.begin(), headers.end());
    if (fixture.organizer().add_headers(announced).headers_added != headers.size()) return 4;
    if (fixture.chain().organize_headers_batch(headers, 1)) return 4;

    network::settings settings(domain::config::network::regtest);
    settings.threads = 2;
    settings.inbound_port = tcp_port;
    settings.inbound_connections = 32;
    settings.outbound_connections = 0;
    settings.host_pool_capacity = 0;
    settings.seeds.clear();
    settings.peers.clear();
    settings.peers_file = fixture.dir() / "peers.json";
    settings.hosts_file = fixture.dir() / "hosts.cache";
    settings.banlist_file = fixture.dir() / "banlist.dat";
    settings.user_agent = "/KnuthWSFixture:1/";
    settings.channel_handshake_seconds = 5;
    settings.ws_enabled = true;
    settings.ws_port = ws_port;
    std::string origins(argv[3]);
    auto comma = origins.find(',');
    settings.ws_origins = {origins.substr(0, comma)};
    if (comma != std::string::npos) settings.ws_origins.push_back(origins.substr(comma + 1));
    settings.ws_discovery = true;
    settings.ws_allow_private = true; // This fixture is deliberately loopback-only.
    for (int i = 4; i < argc; ++i) {
        std::string endpoint(argv[i]);
        if (endpoint.starts_with("tcp://")) {
            auto parsed = infrastructure::config::endpoint::parse_from(endpoint);
            if (!parsed) throw std::runtime_error("Invalid fixture TCP peer URL");
            settings.peers.push_back(std::move(*parsed));
        }
        else settings.ws_public_endpoints.emplace_back(endpoint);
    }

    ::asio::io_context context; // Outlives every socket retained by network.
    node::p2p_node network(settings);
    network.set_top_block({previous, 3});
    // The node's header walk (handlers::chain_data), up to this fixture's
    // header tip: it has headers, not blocks, and the node serves headers only
    // as far as its connected blocks.
    auto const tip = previous;
    network.register_served_message_handler<domain::message::get_headers>(
        [&fixture, tip](network::peer_session& peer, domain::message::get_headers const& request)
            -> ::asio::awaitable<node::message_result> {
            auto headers = domain::message::headers::create(node::handlers::chain_data::headers_after_locator(
                fixture.chain().headers(), tip, request.start_hashes(), request.stop_hash()));
            if (headers) (void)co_await peer.send_data(*headers);
            co_return node::message_result::handled;
        });

    constexpr uint64_t ask_browser_headers = 0x4845414445525301ULL;
    constexpr uint64_t browser_headers_received = 0x4845414445525302ULL;
    network.register_message_handler<domain::message::ping>(
        [](network::peer_session& peer, domain::message::ping const& ping)
            -> ::asio::awaitable<node::message_result> {
            co_await peer.send(domain::message::pong(ping.nonce()));
            if (ping.nonce() == ask_browser_headers) {
                auto genesis = domain::chain::hash(domain::chain::block::genesis_regtest().header());
                co_await network::send_get_headers(peer, hash_list{genesis}, null_hash);
            }
            co_return node::message_result::handled;
        });
    auto observe_browser_headers = [&]() -> ::asio::awaitable<void> {
        while (!network.stopped()) {
            auto [ec, event] = co_await network.header_ingest().commands.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) break;
            auto* batch = std::get_if<node::detail::ingest_submit>(&event);
            if (!batch || batch->headers.size() != headers.size()) continue;
            bool same = true;
            for (size_t i = 0; i < headers.size(); ++i) {
                if (domain::chain::hash(batch->headers[i]) != domain::chain::hash(headers[i])) same = false;
            }
            if (same) co_await batch->peer->send(domain::message::pong(browser_headers_received));
        }
    };

    ::asio::signal_set signals(context, SIGINT, SIGTERM);
    auto run = [&]() -> ::asio::awaitable<void> {
        auto ec = co_await network.start();
        if (ec) throw std::runtime_error(ec.message());
        auto stop = [&]() -> ::asio::awaitable<void> {
            co_await signals.async_wait(::asio::use_awaitable);
            network.stop();
        };
        using namespace ::asio::experimental::awaitable_operators;
        co_await ((network.run() || stop()) && observe_browser_headers());
        network.stop();
    };
    auto future = ::asio::co_spawn(context, run(), ::asio::use_future);
    context.run();
    try { future.get(); }
    catch (std::exception const& e) { std::cerr << e.what() << '\n'; network.stop(); network.join(); return 5; }
    network.join();
}
