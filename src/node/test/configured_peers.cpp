// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <array>
#include <fstream>
#include <stdexcept>
#include <asio/cancel_after.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/detail/p2p_node_test_seam.hpp>
#include <kth/node/detail/configured_peers.hpp>

#include "child_process_harness.hpp"

using namespace kth;
using namespace kth::test;
using namespace std::chrono_literals;

namespace {
using tcp = ::asio::ip::tcp;

struct directory_cleanup {
    std::filesystem::path path;
    ~directory_cleanup() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

network::settings configured_settings(std::filesystem::path const& dir, uint16_t port) {
    network::settings settings(domain::config::network::regtest);
    settings.user_agent = "/ConfiguredPeerTest:1/";
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    settings.threads = 2;
    settings.seeds.clear();
    settings.peers_file = dir / "peers.dat";
    settings.hosts_file = dir / "hosts.dat";
    settings.banlist_file = dir / "bans.dat";
    settings.channel_handshake_seconds = 2;
    settings.connect_timeout_seconds = 2;
    settings.relay_transactions = false;
    settings.peers.emplace_back("127.0.0.1", port);
    return settings;
}

// A real version/verack exchange on a bounded accept. Returning no session is
// an observed missing reconnect, not an assertion that abandons a running node.
::asio::awaitable<network::peer_session::ptr> complete_handshake(
        tcp::socket socket, network::settings const& settings, uint64_t nonce,
        bool* exchanged = nullptr) {
    auto peer = std::make_shared<network::peer_session>(std::move(socket), settings, true);
    auto config = network::make_handshake_config(settings, 100, nonce);
    config.ws_discovery = false;
    auto result = co_await network::perform_handshake_direct(*peer, config);
    if ( ! result) {
        spdlog::error("[configured_peer_test] Handshake failed: {}", result.error().message());
        peer->stop();
        co_return nullptr;
    }
    if (exchanged) *exchanged = true;
    // A verack read by this side can still be queued at the node. Wait for a
    // post-handshake capability message so a subsequent close cannot turn the
    // intended established-session case into a failed initial handshake.
    auto const deadline = std::chrono::steady_clock::now() + 8s;
    while (true) {
        auto message = co_await peer->read_message_direct(deadline);
        if ( ! message) {
            peer->stop();
            co_return nullptr;
        }
        if (message->heading.command() == domain::message::send_headers::command) break;
    }
    co_return peer;
}

::asio::awaitable<network::peer_session::ptr> accept_handshake(
        tcp::acceptor& acceptor, network::settings const& settings, uint64_t nonce,
        std::chrono::seconds accept_limit = 8s) {
    auto [ec, socket] = co_await acceptor.async_accept(
        ::asio::cancel_after(accept_limit, ::asio::as_tuple(::asio::use_awaitable)));
    if (ec) co_return nullptr;
    co_return co_await complete_handshake(std::move(socket), settings, nonce);
}

::asio::awaitable<void> keep_session_healthy() {
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer healthy(executor, 6s);
    co_await healthy.async_wait(::asio::use_awaitable);
}
} // namespace

TEST_CASE("configured peer child: reconnect after a completed session",
          "[.configured-peer-child]") {
    watchdog guard(60s, "configured peer reconnect");
    auto const dir = claimed_dir("kth_configured_peer");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, acceptor.local_endpoint().port());
    settings.manual_attempt_limit = 1;
    // Repeating an endpoint must not create competing reconnect owners.
    settings.peers.push_back(settings.peers.front());
    node::p2p_node node(settings);
    bool first_handshake = false;
    bool second_handshake = false;
    bool third_handshake = false;
    bool duplicate_attempt = false;
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        auto const started = co_await node.start();
        if (started) co_return;
        task_group tasks("configured_peer_test", context.get_executor());
        tasks.spawn("node", node.run());
        auto first = co_await accept_handshake(acceptor, settings, 101);
        first_handshake = bool(first);
        if (first) {
            {
                auto [ec, socket] = co_await acceptor.async_accept(
                    ::asio::cancel_after(2s, ::asio::as_tuple(::asio::use_awaitable)));
                duplicate_attempt = !ec;
            }
            co_await keep_session_healthy();
            first->stop();
            auto second = co_await accept_handshake(acceptor, settings, 102);
            second_handshake = bool(second);
            if (second) {
                co_await keep_session_healthy();
                second->stop();
                auto third = co_await accept_handshake(acceptor, settings, 103);
                third_handshake = bool(third);
                if (third) third->stop();
            }
        }
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(first_handshake);
    CHECK_FALSE(duplicate_attempt);
    CHECK(second_handshake);
    CHECK(third_handshake);
    announce_completion();
}

TEST_CASE("configured peers reconnect after completed sessions", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: reconnect after a completed session"));
}

TEST_CASE("configured peer child: retry a failed handshake", "[.configured-peer-child]") {
    watchdog guard(60s, "configured peer initial failure");
    auto const dir = claimed_dir("kth_configured_retry");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, acceptor.local_endpoint().port());
    node::p2p_node node(settings);
    bool first_accepted = false;
    bool retried_handshake = false;
    std::chrono::steady_clock::duration retry_elapsed{};
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        if (co_await node.start()) co_return;
        task_group tasks("configured_retry_test", context.get_executor());
        tasks.spawn("node", node.run());
        auto [ec, socket] = co_await acceptor.async_accept(
            ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
        first_accepted = !ec;
        if ( ! ec) {
            // TCP succeeds, but the first peer supplies no version/verack.
            auto const failed_at = std::chrono::steady_clock::now();
            socket.close();
            auto retry = co_await accept_handshake(acceptor, settings, 201);
            retry_elapsed = std::chrono::steady_clock::now() - failed_at;
            retried_handshake = bool(retry);
            if (retry) retry->stop();
        }
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(first_accepted);
    CHECK(retried_handshake);
    // A lower bound only: slow runners can delay delivery, never make it early.
    CHECK(retry_elapsed >= 1s);
    announce_completion();
}

TEST_CASE("configured peers retry failed handshakes", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: retry a failed handshake"));
}

TEST_CASE("configured peer child: stop at the configured attempt limit", "[.configured-peer-child]") {
    watchdog guard(60s, "configured peer attempt limit");
    auto const dir = claimed_dir("kth_configured_limit");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, acceptor.local_endpoint().port());
    settings.manual_attempt_limit = 2;
    node::p2p_node node(settings);
    unsigned accepted = 0;
    bool extra_attempt = false;
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        if (co_await node.start()) co_return;
        task_group tasks("configured_limit_test", context.get_executor());
        tasks.spawn("node", node.run());
        for (unsigned i = 0; i < 2; ++i) {
            auto [ec, socket] = co_await acceptor.async_accept(
                ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
            if (ec) break;
            ++accepted;
            socket.close();
        }
        if (accepted == 2) {
            auto [ec, socket] = co_await acceptor.async_accept(
                ::asio::cancel_after(3s, ::asio::as_tuple(::asio::use_awaitable)));
            extra_attempt = !ec;
        }
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(accepted == 2);
    CHECK_FALSE(extra_attempt);
    announce_completion();
}

TEST_CASE("configured peers honor the attempt limit", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: stop at the configured attempt limit"));
}

TEST_CASE("configured peer child: stop cancels retry registration and an armed retry",
          "[.configured-peer-child]") {
    watchdog guard(30s, "configured retry cancellation");
    auto const dir = claimed_dir("kth_configured_cancel");
    directory_cleanup cleanup{dir};
    for (bool before_registration : {true, false}) {
        ::asio::io_context context;
        auto settings = configured_settings(dir, 1);
        node::p2p_node node(settings);
        auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<bool> {
            if (before_registration) {
                node.stop();
            } else {
                // This context has one runner. The posted stop cannot run until
                // the coroutine suspends with the retry selection registered.
                ::asio::post(context, [&] { node.stop(); });
            }
            co_return co_await node::detail::p2p_node_test_seam::wait_configured_retry(node, 24h);
        }, ::asio::use_future);
        context.run();
        CHECK_FALSE(scenario.get());
    }
    announce_completion();
}

TEST_CASE("configured peer retry waits end on stop", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: stop cancels retry registration and an armed retry"));
}

TEST_CASE("configured peer child: concurrent endpoints retain one outbound per IP",
          "[.configured-peer-child]") {
    watchdog guard(60s, "configured peer concurrent admission");
    auto const dir = claimed_dir("kth_configured_ip");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor first(context, {::asio::ip::address_v4::loopback(), 0});
    tcp::acceptor second(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, first.local_endpoint().port());
    settings.manual_attempt_limit = 1;
    settings.peers.emplace_back("127.0.0.1", second.local_endpoint().port());
    node::p2p_node node(settings);
    bool both_attempted = false;
    unsigned handshakes = 0;
    size_t live = 0;
    bool busy_endpoint_recovered = false;
    bool busy_tcp_attempt = false;
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        if (co_await node.start()) co_return;
        task_group tasks("configured_ip_test", context.get_executor());
        tasks.spawn("node", node.run());
        // Neither server answers until both TCP connects have completed. Both
        // attempts therefore pass the pre-handshake exists_by_ip check.
        auto [first_ec, a] = co_await first.async_accept(
            ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
        auto [second_ec, b] = co_await second.async_accept(
            ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
        both_attempted = !first_ec && !second_ec;
        if (both_attempted) {
            using namespace ::asio::experimental::awaitable_operators;
            auto peers = co_await (complete_handshake(std::move(a), settings, 301) &&
                complete_handshake(std::move(b), settings, 302));
            handshakes = unsigned(bool(std::get<0>(peers))) + unsigned(bool(std::get<1>(peers)));
            live = node.connection_count();
            if (handshakes == 1) {
                auto& occupied = std::get<0>(peers) ? second : first;
                auto [ec, socket] = co_await occupied.async_accept(
                    ::asio::cancel_after(3s, ::asio::as_tuple(::asio::use_awaitable)));
                busy_tcp_attempt = !ec;
                if (auto peer = std::get<0>(peers)) peer->stop();
                if (auto peer = std::get<1>(peers)) peer->stop();
                // The rejected endpoint is occupied, not failing. Even with a
                // one-attempt limit it must remain owed when the winner closes.
                auto& available = std::get<0>(peers) ? second : first;
                auto replacement = co_await accept_handshake(available, settings, 303);
                busy_endpoint_recovered = bool(replacement);
                if (replacement) replacement->stop();
            } else {
                if (auto peer = std::get<0>(peers)) peer->stop();
                if (auto peer = std::get<1>(peers)) peer->stop();
            }
        }
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(both_attempted);
    CHECK(handshakes == 1);
    CHECK(live == 1);
    CHECK(busy_endpoint_recovered);
    CHECK_FALSE(busy_tcp_attempt);
    announce_completion();
}

TEST_CASE("configured peers serialize admission by IP", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: concurrent endpoints retain one outbound per IP"));
}

TEST_CASE("configured peer child: stop ends an in-flight handshake", "[.configured-peer-child]") {
    watchdog guard(30s, "configured peer stop during handshake");
    auto const dir = claimed_dir("kth_configured_handshake_stop");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, acceptor.local_endpoint().port());
    settings.channel_handshake_seconds = 3600;
    node::p2p_node node(settings);
    bool handshake_started = false;
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        if (co_await node.start()) co_return;
        task_group tasks("configured_handshake_stop_test", context.get_executor());
        tasks.spawn("node", node.run());
        auto [ec, socket] = co_await acceptor.async_accept(
            ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
        if ( ! ec) {
            // Observe the version header, then deliberately send no answer.
            std::array<uint8_t, 24> heading{};
            auto [read_ec, count] = co_await ::asio::async_read(socket, ::asio::buffer(heading),
                ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
            handshake_started = !read_ec && count == heading.size();
        }
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(handshake_started);
    announce_completion();
}

TEST_CASE("configured peer handshakes end on stop", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: stop ends an in-flight handshake"));
}

TEST_CASE("configured peer child: retry a rejection before supervisor handoff",
          "[.configured-peer-child]") {
    watchdog guard(60s, "configured peer pre-handoff rejection");
    auto const dir = claimed_dir("kth_configured_rejected");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, acceptor.local_endpoint().port());
    settings.manual_attempt_limit = 2;
    node::p2p_node node(settings);
    unsigned accepted = 0;
    bool all_closed = true;
    size_t live = 1;
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        if (co_await node.start()) co_return;
        node.peer_db().ban(::asio::ip::address_v4::loopback(), 1h, network::ban_reason::manually_added);
        task_group tasks("configured_rejected_test", context.get_executor());
        tasks.spawn("node", node.run());
        for (unsigned i = 0; i < 2; ++i) {
            auto [ec, socket] = co_await acceptor.async_accept(
                ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
            if (ec) break;
            ++accepted;
            // The node closes the socket before any supervisor handoff.
            std::array<uint8_t, 1> byte{};
            auto [read_ec, count] = co_await socket.async_read_some(::asio::buffer(byte),
                ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
            if (read_ec != ::asio::error::eof || count != 0) {
                all_closed = false;
                break;
            }
        }
        live = node.connection_count();
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(accepted == 2);
    CHECK(all_closed);
    CHECK(live == 0);
    announce_completion();
}

TEST_CASE("configured peers retain pre-handoff retry obligations", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: retry a rejection before supervisor handoff"));
}

TEST_CASE("configured peer child: a stable session resets failed attempts",
          "[.configured-peer-child]") {
    watchdog guard(60s, "configured peer failed attempt reset");
    auto const dir = claimed_dir("kth_configured_reset");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, acceptor.local_endpoint().port());
    settings.manual_attempt_limit = 2;
    node::p2p_node node(settings);
    unsigned rejected = 0;
    unsigned completed = 0;
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        if (co_await node.start()) co_return;
        task_group tasks("configured_reset_test", context.get_executor());
        tasks.spawn("node", node.run());
        for (unsigned i = 0; i < 2; ++i) {
            auto [ec, socket] = co_await acceptor.async_accept(
                ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
            if (ec) break;
            ++rejected;
            socket.close();
            auto peer = co_await accept_handshake(acceptor, settings, 400 + i);
            if ( ! peer) break;
            ++completed;
            if (i == 0) co_await keep_session_healthy();
            peer->stop();
        }
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(rejected == 2);
    CHECK(completed == 2);
    announce_completion();
}

TEST_CASE("configured peers reset failed attempts after a stable session", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: a stable session resets failed attempts"));
}

TEST_CASE("configured peer child: a thrown attempt completes before shutdown",
          "[.configured-peer-child]") {
    watchdog guard(30s, "configured peer throwing attempt");
    ::asio::io_context context;
    std::stop_source stop;
    auto throwing = []() -> awaitable_expected<network::peer_session::ptr> {
        co_await ::asio::post(::asio::use_awaitable);
        throw std::runtime_error("injected configured dial failure");
        co_return nullptr;
    };
    auto result = ::asio::co_spawn(context,
        node::detail::run_configured_attempt(throwing(), stop.get_token()), ::asio::use_future);
    context.run();
    auto outcome = result.get();
    REQUIRE_FALSE(outcome);
    CHECK(outcome.error() == error::operation_failed);
    CHECK_FALSE(stop.stop_requested());
    announce_completion();
}

TEST_CASE("configured peer exceptions do not wait for shutdown", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: a thrown attempt completes before shutdown"));
}

TEST_CASE("configured peer child: an admitted handler exception releases the IP",
          "[.configured-peer-child]") {
    watchdog guard(120s, "configured peer handler exception");
    for (bool served : {false, true}) {
        auto const dir = claimed_dir("kth_configured_handler_throw");
        directory_cleanup cleanup{dir};
        ::asio::io_context context;
        tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
        auto settings = configured_settings(dir, acceptor.local_endpoint().port());
        select_channel<bool> called(1);
        node::p2p_node node(settings);
        auto handler = [&](network::peer_session&, domain::message::get_headers const&)
                -> ::asio::awaitable<node::message_result> {
            (void)called.try_send(true);
            throw std::runtime_error("injected admitted handler failure");
            co_return node::message_result::continue_processing;
        };
        if (served) {
            node.register_served_message_handler<domain::message::get_headers>(handler);
        } else {
            node.register_message_handler<domain::message::get_headers>(handler);
        }
        bool established = false;
        bool triggered = false;
        bool observed = false;
        bool reconnected = false;
        size_t admitted_before = 0;
        size_t admitted_after = 0;
        auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
            if (co_await node.start()) co_return;
            task_group tasks("configured_handler_throw_test", context.get_executor());
            tasks.spawn("node", node.run());
            auto first = co_await accept_handshake(acceptor, settings, 501);
            established = bool(first);
            if (first) {
                admitted_before = node.connection_count();
                triggered = co_await first->send_direct(domain::message::get_headers{}) == error::success;
                using namespace ::asio::experimental;
                auto [ec, selected] = co_await make_select(receive(called),
                    after(std::chrono::steady_clock::now() + 8s))
                    .async_wait(context.get_executor(), {}, std::stop_token{},
                        ::asio::as_tuple(::asio::use_awaitable));
                observed = !ec && selected.index == 0;
                if (observed) {
                    // Keep the original transport alive on this side. A real
                    // successful replacement proves its old registry entry was
                    // removed, rather than hidden by shutting down the node.
                    auto replacement = co_await accept_handshake(acceptor, settings, 502, 45s);
                    reconnected = bool(replacement);
                    admitted_after = node.connection_count();
                    if (replacement) replacement->stop();
                }
                first->stop();
            }
            node.stop();
            co_await tasks.join();
        }, ::asio::use_future);
        context.run();
        scenario.get();
        node.join();
        INFO("served handler: " << served);
        CHECK(established);
        CHECK(admitted_before == 1);
        CHECK(triggered);
        CHECK(observed);
        CHECK(reconnected);
        CHECK(admitted_after == 1);
    }
    announce_completion();
}

TEST_CASE("configured peers reconnect after admitted handlers throw", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: an admitted handler exception releases the IP"));
}

TEST_CASE("configured peer child: a discovered admission loser remains usable",
          "[.configured-peer-child]") {
    watchdog guard(90s, "discovered peer admission conflict");
    auto const dir = claimed_dir("kth_discovered_admission");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor manual(context, {::asio::ip::address_v4::loopback(), 0});
    tcp::acceptor discovered(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, manual.local_endpoint().port());
    settings.peers.clear();
    settings.outbound_connections = 1;
    {
        std::ofstream hosts(settings.hosts_file);
        hosts << "127.0.0.1:" << discovered.local_endpoint().port() << '\n';
    }
    node::p2p_node node(settings);
    bool candidate_seeded = false;
    bool manual_connected = false;
    bool both_attempted = false;
    bool loser_started = false;
    bool loser_exchanged = false;
    bool loser_admitted = true;
    bool recovered = false;
    bool candidate_retained = false;
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        if (co_await node.start()) co_return;
        candidate_seeded = node.peer_db().import_hosts_cache(settings.hosts_file) == 1;
        task_group tasks("discovered_admission_test", context.get_executor());
        tasks.spawn("node", node.run());
        tasks.spawn("manual", [&]() -> ::asio::awaitable<void> {
            manual_connected = bool(co_await node.connect("127.0.0.1", manual.local_endpoint().port()));
        });
        auto [a_ec, a] = co_await manual.async_accept(
            ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
        auto [b_ec, b] = co_await discovered.async_accept(
            ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
        both_attempted = !a_ec && !b_ec;
        if (both_attempted) {
            // Peeking at version proves the discovered attempt passed the first
            // IP check. Do not consume it: its later handshake still reads it.
            std::array<uint8_t, 1> byte{};
            auto [peek_ec, size] = co_await b.async_receive(::asio::buffer(byte),
                tcp::socket::message_peek,
                ::asio::cancel_after(8s, ::asio::as_tuple(::asio::use_awaitable)));
            loser_started = !peek_ec && size == 1;
            if (loser_started) {
                auto winner = co_await complete_handshake(std::move(a), settings, 601);
                if (winner) {
                    auto loser = co_await complete_handshake(std::move(b), settings, 602, &loser_exchanged);
                    loser_admitted = bool(loser);
                    if (loser) loser->stop();
                    // Keep the winner healthy, so its own short-session
                    // cooldown cannot explain a missing discovered retry.
                    ::asio::steady_timer healthy(context, 6s);
                    co_await healthy.async_wait(::asio::use_awaitable);
                    winner->stop();
                    auto replacement = co_await accept_handshake(discovered, settings, 603);
                    recovered = bool(replacement);
                    candidate_retained = node.peer_db().exists(infrastructure::config::authority(discovered.local_endpoint()));
                    if (replacement) replacement->stop();
                }
            }
        }
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(candidate_seeded);
    CHECK(both_attempted);
    CHECK(manual_connected);
    CHECK(loser_started);
    CHECK(loser_exchanged);
    CHECK_FALSE(loser_admitted);
    CHECK(candidate_retained);
    CHECK(recovered);
    announce_completion();
}

TEST_CASE("discovered peers survive admission conflicts", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: a discovered admission loser remains usable"));
}

TEST_CASE("configured peer child: repeated short sessions retain backoff",
          "[.configured-peer-child]") {
    watchdog guard(180s, "configured peer repeated short sessions");
    auto const dir = claimed_dir("kth_configured_short_sessions");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, acceptor.local_endpoint().port());
    node::p2p_node node(settings);
    unsigned established = 0;
    std::vector<std::chrono::steady_clock::duration> waits;
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        if (co_await node.start()) co_return;
        task_group tasks("configured_short_sessions_test", context.get_executor());
        tasks.spawn("node", node.run());
        std::optional<std::chrono::steady_clock::time_point> closed_at;
        for (unsigned i = 0; i < 3; ++i) {
            auto peer = co_await accept_handshake(acceptor, settings, 700 + i,
                i == 0 ? 8s : (i == 1 ? 45s : 75s));
            if ( ! peer) break;
            ++established;
            if (closed_at) waits.push_back(std::chrono::steady_clock::now() - *closed_at);
            closed_at = std::chrono::steady_clock::now();
            // A successful handshake followed immediately by remote EOF.
            peer->stop();
        }
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(established == 3);
    REQUIRE(waits.size() == 2);
    CHECK(waits[0] >= 30s);
    CHECK(waits[1] >= 60s);
    announce_completion();
}

TEST_CASE("configured peers back off after repeated short sessions", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: repeated short sessions retain backoff"));
}

TEST_CASE("configured peer child: draining a handler does not extend transport lifetime",
          "[.configured-peer-child]") {
    watchdog guard(90s, "configured peer transport lifetime");
    auto const dir = claimed_dir("kth_configured_handler_drain");
    directory_cleanup cleanup{dir};
    ::asio::io_context context;
    tcp::acceptor acceptor(context, {::asio::ip::address_v4::loopback(), 0});
    auto settings = configured_settings(dir, acceptor.local_endpoint().port());
    select_channel<bool> entered(1);
    select_channel<bool> release(1);
    node::p2p_node node(settings);
    node.register_served_message_handler<domain::message::get_headers>(
        [&](network::peer_session& peer, domain::message::get_headers const&)
                -> ::asio::awaitable<node::message_result> {
            peer.stop();
            (void)entered.try_send(true);
            using namespace ::asio::experimental;
            co_await make_select(receive(release)).async_wait(
                co_await ::asio::this_coro::executor, {}, std::stop_token{},
                ::asio::as_tuple(::asio::use_awaitable));
            co_return node::message_result::continue_processing;
        });
    bool established = false;
    bool handler_entered = false;
    bool transport_closed = false;
    bool reconnected = false;
    size_t registered_while_draining = 0;
    std::chrono::steady_clock::duration retry_wait{};
    auto scenario = ::asio::co_spawn(context, [&]() -> ::asio::awaitable<void> {
        if (co_await node.start()) co_return;
        task_group tasks("configured_handler_drain_test", context.get_executor());
        tasks.spawn("node", node.run());
        auto first = co_await accept_handshake(acceptor, settings, 801);
        established = bool(first);
        if (first) {
            (void)co_await first->send_direct(domain::message::get_headers{});
            using namespace ::asio::experimental;
            auto [ec, selected] = co_await make_select(receive(entered),
                after(std::chrono::steady_clock::now() + 8s))
                .async_wait(context.get_executor(), {}, std::stop_token{},
                    ::asio::as_tuple(::asio::use_awaitable));
            handler_entered = !ec && selected.index == 0;
            if (handler_entered) {
                auto const deadline = std::chrono::steady_clock::now() + 8s;
                while (true) {
                    auto message = co_await first->read_message_direct(deadline);
                    if ( ! message) {
                        transport_closed = message.error() != kth::error::channel_timeout;
                        break;
                    }
                }
                // Only the handler remains. Holding it past the healthy-session
                // threshold must neither release the registry nor earn health.
                co_await keep_session_healthy();
                registered_while_draining = node.connection_count();
            }
            auto const released_at = std::chrono::steady_clock::now();
            (void)release.try_send(true);
            if (handler_entered) {
                auto replacement = co_await accept_handshake(acceptor, settings, 802, 45s);
                retry_wait = std::chrono::steady_clock::now() - released_at;
                reconnected = bool(replacement);
                if (replacement) replacement->stop();
            }
            first->stop();
        }
        release.close();
        node.stop();
        co_await tasks.join();
    }, ::asio::use_future);
    context.run();
    scenario.get();
    node.join();
    CHECK(established);
    CHECK(handler_entered);
    CHECK(transport_closed);
    CHECK(registered_while_draining == 1);
    CHECK(reconnected);
    CHECK(retry_wait >= 30s);
    announce_completion();
}

TEST_CASE("configured peer health excludes handler drain time", "[node][configured_peers]") {
    require_clean_child(run_child("configured peer child: draining a handler does not extend transport lifetime"));
}
