// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include "child_process_harness.hpp"
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/use_future.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/node/detail/p2p_node_test_seam.hpp>

using namespace kth;
using namespace kth::test;
using namespace std::chrono_literals;
using namespace ::asio::experimental::awaitable_operators;

namespace {
struct close_observations {
    unsigned completed{0};
    std::vector<int> scores;
    std::vector<bool> banned;
    bool explicit_misbehavior_bans{false};
};
void checked(bool condition, char const* message) {
    if (!condition) { throw std::runtime_error(message); }
}
::asio::awaitable<void> repeated_connections(node::p2p_node& target,
    network::settings const& config, node::peer_direction direction,
    close_observations& observed) {
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::ip::tcp::acceptor listener(executor,
        {::asio::ip::address_v4::loopback(), 0});
    auto last_authority = infrastructure::config::authority::any();
    for (unsigned i = 0; i != 6; ++i) {
        ::asio::ip::tcp::socket client_socket(executor);
        co_await client_socket.async_connect(listener.local_endpoint(), ::asio::use_awaitable);
        auto accepted = co_await listener.async_accept(::asio::use_awaitable);
        auto peer = std::make_shared<network::peer_session>(std::move(accepted),
            config, direction == node::peer_direction::inbound);
        last_authority = peer->authority();
        network::peer_session client(std::move(client_socket), config, false);
        co_await node::detail::p2p_node_test_seam::admit_peer(target, peer, direction);
        auto handshake = co_await network::perform_handshake_direct(client,
            network::make_handshake_config(config, 0, 0x1000 + i));
        if (!handshake) throw std::runtime_error("fixture BCH handshake: " + handshake.error().message());
        auto [connected_ec, connected] = co_await target.peer_events().async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        checked(!connected_ec && connected.event == node::peer_event_type::connected &&
            connected.peer == peer, "missing connected notification");
        // Real transport EOF after the BCH handshake. Notification follows the
        // production close-policy block; no polling or cleanup hides its effect.
        client.stop();
        auto [closed_ec, closed] = co_await target.peer_events().async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        checked(!closed_ec && closed.event == node::peer_event_type::disconnected &&
            closed.peer == peer, "missing disconnected notification");
        checked(std::chrono::steady_clock::now() - peer->connection_time() < 5s,
            "fixture did not exercise the quick-disconnect branch");
        auto record = target.peer_db().get(last_authority);
        checked(bool(record), "completed handshake did not record peer");
        observed.scores.push_back(record->reputation_score);
        observed.banned.push_back(target.is_banned(last_authority));
        ++observed.completed;
    }
    observed.explicit_misbehavior_bans = target.report_misbehavior(last_authority,
        100, "test control: invalid protocol data") && target.is_banned(last_authority);
}
}

TEST_CASE("p2p child: short established sessions do not accumulate bans", "[.p2p-child]") {
    watchdog guard(90s, "short established sessions and shutdown");
    for (auto direction : {node::peer_direction::inbound, node::peer_direction::outbound}) {
        auto const directory = claimed_dir("kth_quick_disconnect");
        {
        network::settings config(domain::config::network::regtest);
        config.inbound_port = 0;
        config.outbound_connections = 0;
        config.host_pool_capacity = 64;
        config.seeds.clear();
        config.peers.clear();
        config.threads = 1;
        config.peers_file = (directory / "peers.dat").string();
        config.hosts_file = (directory / "hosts.dat").string();
        config.banlist_file = (directory / "bans.dat").string();
        config.channel_handshake_seconds = 5;
        config.user_agent = "/KTH-close-policy-test:1/";
        config.user_agent_blacklist.clear();
        ::asio::io_context context;
        node::p2p_node target(config);
        close_observations observed;
        std::exception_ptr failure;
        auto outcome = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
            checked(co_await target.start() == error::success, "node start failed");
            co_await (
                [&]() -> ::asio::awaitable<void> { co_await target.run(); }() &&
                [&]() -> ::asio::awaitable<void> {
                    try { co_await repeated_connections(target, config, direction, observed); }
                    catch (...) { failure = std::current_exception(); }
                    target.stop();
                }());
        }, ::asio::use_future);
        context.run();
        target.stop();
        target.join();
        outcome.get();
        if (failure) { std::rethrow_exception(failure); }
        INFO("direction: " << (direction == node::peer_direction::inbound ? "inbound" : "outbound"));
        REQUIRE(observed.completed == 6u);
        for (auto score : observed.scores) { CHECK(score == 0); }
        for (auto banned : observed.banned) { CHECK_FALSE(banned); }
        CHECK(observed.explicit_misbehavior_bans);
        } // Destroy the node and peer database before removing their files.
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    announce_completion();
}
TEST_CASE("short established sessions do not accumulate protocol bans", "[node][p2p][connection_close]") {
    require_clean_child(run_child("p2p child: short established sessions do not accumulate bans"));
}
