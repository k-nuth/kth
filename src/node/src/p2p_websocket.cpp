// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license, see LICENSE.

#include <kth/node/p2p_node.hpp>

#if defined(KTH_WITH_WEBSOCKET)
#include <kth/network/websocket_transport.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/co_spawn.hpp>
#include <variant>

namespace kth::node {
namespace {
::asio::awaitable<void> wait_websocket_stop(std::stop_token token) {
    select_channel<int> never(0);
    auto executor = co_await ::asio::this_coro::executor;
    co_await ::asio::experimental::make_select(::asio::experimental::receive(never))
        .async_wait(executor, {}, token, ::asio::as_tuple(::asio::use_awaitable));
}
struct upgrade_complete { network::peer_session::ptr peer; };
constexpr auto accept_retry_delay = std::chrono::milliseconds(100);
using listener_event = std::variant<::asio::ip::tcp::socket, upgrade_complete>;
}

::asio::awaitable<void> p2p_node::run_websocket_inbound() {
    if (!settings_.ws_enabled) co_return;
    auto strand = ::asio::make_strand(pool_.get_executor());
    auto service = [this]() -> ::asio::awaitable<void> {
        using namespace ::asio::experimental;
        using namespace ::asio::experimental::awaitable_operators;
        auto executor = co_await ::asio::this_coro::executor;
        // One acceptor owner, one admission owner, four bounded upgrade workers.
        // Slow HTTP cannot serialize admission of every other client.
        // Validated at start.
        std::vector<::asio::ip::address> trusted_proxies;
        for (auto const& proxy : settings_.ws_trusted_proxies) {
            trusted_proxies.push_back(::asio::ip::make_address(proxy));
        }
        select_channel<listener_event> events(8);
        select_channel<::asio::ip::tcp::socket> upgrades(4);
        auto accept = [&]() -> ::asio::awaitable<void> {
            ::asio::ip::tcp::acceptor acceptor(executor);
            auto address = ::asio::ip::make_address(settings_.ws_bind);
            ::asio::ip::tcp::endpoint endpoint(address, settings_.ws_port);
            acceptor.open(endpoint.protocol());
            acceptor.set_option(::asio::socket_base::reuse_address(true));
            acceptor.bind(endpoint);
            acceptor.listen(static_cast<int>(settings_.ws_max_connections));
            spdlog::info("[p2p_node] WebSocket P2P listening on {}:{}{}", settings_.ws_bind,
                settings_.ws_port, settings_.ws_path);
            auto loop = [&]() -> ::asio::awaitable<void> {
                while (!stopped()) {
                    ::asio::ip::tcp::socket socket(executor);
                    auto [accept_ec] = co_await acceptor.async_accept(socket,
                        ::asio::as_tuple(::asio::use_awaitable));
                    if (accept_ec == ::asio::error::operation_aborted || stopped()) break;
                    if (accept_ec) {
                        // A failed accept (out of descriptors, an aborted
                        // connection) ends that connection, not the listener:
                        // pause briefly, so a persistent one does not spin.
                        spdlog::debug("[p2p_node] WebSocket accept failed: {}", accept_ec.message());
                        auto paused = co_await make_select(after(std::chrono::steady_clock::now() + accept_retry_delay))
                            .async_wait(executor, {}, stop_token(), ::asio::as_tuple(::asio::use_awaitable));
                        if (std::get<0>(paused)) break;
                        continue;
                    }
                    auto sent = co_await make_select(send(events, listener_event{std::move(socket)}))
                        .async_wait(executor, {}, stop_token(), ::asio::as_tuple(::asio::use_awaitable));
                    if (std::get<0>(sent)) break;
                }
            };
            co_await (loop() || wait_websocket_stop(stop_token()));
        };
        auto worker = [&]() -> ::asio::awaitable<void> {
            auto input = make_selector(executor, channel_select_options{}, stop_token(), receive(upgrades));
            while (!stopped()) {
                auto [ec, index] = co_await input.wait(::asio::as_tuple(::asio::use_awaitable));
                if (ec) break;
                (void)index;
                auto socket = std::move(*input.value<0>());
                network::websocket_server_options options{settings_.ws_path, settings_.ws_origins,
                    std::chrono::seconds(settings_.ws_handshake_seconds), trusted_proxies};
                auto result = co_await (network::async_accept_websocket(std::move(socket), settings_, std::move(options))
                    || wait_websocket_stop(stop_token()));
                if (result.index() != 0 || stopped()) break;
                auto upgraded = std::move(std::get<0>(result));
                upgrade_complete complete{upgraded ? std::move(*upgraded) : nullptr};
                auto sent = co_await make_select(send(events, listener_event{std::move(complete)}))
                    .async_wait(executor, {}, stop_token(), ::asio::as_tuple(::asio::use_awaitable));
                if (std::get<0>(sent)) break;
            }
        };
        auto owner = [&]() -> ::asio::awaitable<void> {
            std::vector<std::weak_ptr<network::peer_session>> admitted;
            size_t pending = 0;
            auto input = make_selector(executor, channel_select_options{}, stop_token(), receive(events));
            while (!stopped()) {
                auto [ec, index] = co_await input.wait(::asio::as_tuple(::asio::use_awaitable));
                if (ec) break;
                (void)index;
                auto event = std::move(*input.value<0>());
                if (auto* socket = std::get_if<::asio::ip::tcp::socket>(&event)) {
                    std::erase_if(admitted, [](auto const& weak) {
                        auto peer = weak.lock();
                        return !peer || peer->stopped();
                    });
                    if (admitted.size() + pending >= settings_.ws_max_connections) continue;
                    if (upgrades.try_send(std::move(*socket))) ++pending;
                } else {
                    --pending;
                    auto peer = std::move(std::get<upgrade_complete>(event).peer);
                    if (!peer) continue;
                    if (stopped() || peer_db_.is_banned(peer->authority()) ||
                        !new_peer_channel_->try_send(std::error_code{}, peer_event{peer, peer_direction::inbound, nullptr})) {
                        peer->stop();
                        continue;
                    }
                    admitted.push_back(peer);
                }
            }
        };
        // Every task is joined by this scope. Stop-token cancellation covers
        // accept/queue/HTTP waits, including stop before their initiation.
        auto guarded = [this](::asio::awaitable<void> operation) -> ::asio::awaitable<void> {
            try { co_await std::move(operation); }
            catch (std::exception const& e) {
                if (!stopped()) {
                    spdlog::error("[p2p_node] WebSocket listener failed: {}", e.what());
                    stop();
                }
            }
        };
        co_await (guarded(accept()) && guarded(owner()) && guarded(worker()) &&
            guarded(worker()) && guarded(worker()) && guarded(worker()));
        upgrades.close();
        events.close();
    };
    co_await ::asio::co_spawn(strand, service(), ::asio::use_awaitable);
}
} // namespace kth::node
#else
namespace kth::node {
::asio::awaitable<void> p2p_node::run_websocket_inbound() { co_return; }
}
#endif
