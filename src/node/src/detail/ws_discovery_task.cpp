// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license, see LICENSE.

#include <kth/node/p2p_node.hpp>
#include <kth/node/detail/ws_discovery_task.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/ip/tcp.hpp>
#include <deque>
#include <map>

namespace kth::node {
namespace {
::asio::awaitable<void> discovery_stop(std::stop_token token) {
    select_channel<int> never(0);
    auto executor = co_await ::asio::this_coro::executor;
    co_await ::asio::experimental::make_select(::asio::experimental::receive(never))
        .async_wait(executor, {}, token, ::asio::as_tuple(::asio::use_awaitable));
}
uint32_t discovery_now() { return static_cast<uint32_t>(zulu_time()); }

// Classification only: no HTTP fetch, outbound listener probe or credentials.
// All returned addresses must be public, including mixed A/AAAA answers.
::asio::awaitable<bool> public_dns(network::ws_discovery::endpoint const& endpoint) {
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::ip::tcp::resolver resolver(executor);
    ::asio::steady_timer deadline(executor, std::chrono::seconds(3));
    using namespace ::asio::experimental::awaitable_operators;
    auto result = co_await (resolver.async_resolve(endpoint.host, std::to_string(endpoint.port),
        ::asio::as_tuple(::asio::use_awaitable)) || deadline.async_wait(::asio::as_tuple(::asio::use_awaitable)));
    if (result.index() != 0) co_return false;
    auto& [ec, addresses] = std::get<0>(result);
    if (ec || addresses.empty()) co_return false;
    auto now = discovery_now();
    for (auto const& answer : addresses) {
        auto numeric = endpoint;
        numeric.host = answer.endpoint().address().to_string();
        if (!network::ws_discovery::validate(numeric, now, false)) co_return false;
    }
    co_return true;
}
struct verified_endpoint { network::ws_discovery::endpoint endpoint; bool public_address; };
}

::asio::awaitable<void> p2p_node::run_websocket_discovery() {
    if (!settings_.ws_discovery) co_return;
    namespace ws = network::ws_discovery;
    using namespace ::asio::experimental;
    using namespace ::asio::experimental::awaitable_operators;
    auto executor = co_await ::asio::this_coro::executor;
    select_channel<ws::endpoint> verification_input(16);
    select_channel<verified_endpoint> verification_output(16);

    // A separate owned resolver task ensures a slow DNS server cannot hold up
    // queries, relay, or the store owner. Requests/results are bounded messages.
    auto verifier = [&]() -> ::asio::awaitable<void> {
        auto input = make_selector(executor, channel_select_options{}, stop_token(), receive(verification_input));
        while (!stopped()) {
            auto [ec, index] = co_await input.wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec) break;
            (void)index;
            auto endpoint = std::move(*input.value<0>());
            auto result = co_await (public_dns(endpoint) || discovery_stop(stop_token()));
            if (result.index() != 0 || stopped()) break;
            auto message = verified_endpoint{std::move(endpoint), std::get<0>(result)};
            auto sent = co_await make_select(send(verification_output, std::move(message)))
                .async_wait(executor, {}, stop_token(), ::asio::as_tuple(::asio::use_awaitable));
            if (std::get<0>(sent)) break;
        }
    };

    auto owner = [&]() -> ::asio::awaitable<void> {
        ws::endpoint_store store(settings_.ws_allow_private);
        std::deque<ws::endpoint> debt;
        std::map<std::string, ws::endpoint> pending;
        auto admit = [&](ws::endpoint endpoint, bool own) {
            auto now = discovery_now();
            if (!(own ? store.ingest_own(endpoint, now) : store.ingest(endpoint, now))) return;
            if (settings_.ws_allow_private) {
                (void)store.verify(endpoint, ws::verification::explicit_lan, now);
                return;
            }
            std::error_code address_ec;
            (void)::asio::ip::make_address(endpoint.host, address_ec);
            // Numeric public records were classified synchronously by ingest.
            if (!address_ec) return;
            // Duplicates retain their original lease. Ask the store for that
            // canonical generation before scheduling asynchronous approval.
            auto stored = store.lookup(endpoint, now);
            if (!stored) return;
            endpoint = std::move(*stored);
            auto key = ws::url(endpoint);
            auto found = pending.find(key);
            if (found != pending.end()) {
                found->second = std::move(endpoint);
            } else if (pending.size() < ws::maximum_store) {
                pending.emplace(key, endpoint);
                debt.push_back(std::move(endpoint));
            }
        };
        auto pump = [&] {
            while (!debt.empty()) {
                auto copy = debt.front();
                if (!verification_input.try_send(std::move(copy))) break;
                debt.pop_front();
            }
        };
        auto publish = [&] {
            auto now = discovery_now();
            for (auto const& url : settings_.ws_public_endpoints) {
                auto endpoint = ws::from_url(url, now + ws::maximum_ttl, now, settings_.ws_allow_private);
                if (endpoint) admit(std::move(*endpoint), true);
            }
            pump();
        };
        publish();
        auto input = make_selector(executor, channel_select_options{}, stop_token(),
            receive(ws_discovery_->commands), receive(verification_output));
        while (!stopped()) {
            auto [ec, index] = co_await input.wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec) break;
            if (index == 1) {
                auto result = std::move(*input.value<1>());
                if (result.public_address) {
                    (void)store.verify(result.endpoint, ws::verification::public_address_verified, discovery_now());
                }
                auto found = pending.find(ws::url(result.endpoint));
                if (found != pending.end()) {
                    if (found->second.expires != result.endpoint.expires) {
                        debt.push_back(found->second);
                    } else {
                        pending.erase(found);
                    }
                }
                // Completion reactivates refused work, including a newer lease
                // that replaced an expired record during the DNS operation.
                pump();
                continue;
            }
            auto event = std::move(*input.value<0>());
            if (auto* query = std::get_if<detail::ws_query>(&event)) {
                publish();
                auto now = discovery_now();
                auto payload = ws::encode(ws::response{query->request.id,
                    store.select(query->request.transports, now)}, now, settings_.ws_allow_private);
                if (payload && !query->peer->stopped() &&
                    !query->peer->try_send_raw(detail::ws_packet(settings_.identifier, ws::response_command, std::move(*payload)))) {
                    query->peer->stop(error::channel_stopped);
                }
            } else {
                auto& endpoints = std::get<detail::ws_candidates>(event).endpoints;
                for (auto& endpoint : endpoints) admit(std::move(endpoint), false);
                pump();
                spdlog::info("[ws_discovery] Accepted endpoint response ({} candidates)", endpoints.size());
            }
        }
    };
    try { co_await (owner() && verifier()); }
    catch (std::exception const& e) {
        spdlog::error("[ws_discovery] Owner task failed: {}", e.what());
        stop();
    }
    verification_input.close();
    verification_output.close();
}
}
