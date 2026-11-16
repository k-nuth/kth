// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/p2p_node.hpp>
#include <kth/node/detail/configured_peers.hpp>

#include <algorithm>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/channel_select.hpp>

namespace kth::node {
using namespace ::asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

namespace {
::asio::awaitable<void> configured_peer_stop(std::stop_token token) {
    select_channel<bool> never(0);
    auto executor = co_await ::asio::this_coro::executor;
    co_await ::asio::experimental::make_select(::asio::experimental::receive(never))
        .async_wait(executor, {}, token, ::asio::as_tuple(::asio::use_awaitable));
}
} // namespace

awaitable_expected<network::peer_session::ptr> detail::run_configured_attempt(
        awaitable_expected<network::peer_session::ptr> operation, std::stop_token stop) {
    auto executor = co_await ::asio::this_coro::executor;
    // as_tuple turns a thrown connect into a normal completion of the left
    // branch. operator|| must not wait for shutdown to make its first success.
    auto attempt = co_await (::asio::co_spawn(executor, std::move(operation),
        ::asio::as_tuple(::asio::use_awaitable)) || configured_peer_stop(stop));
    if (stop.stop_requested() || attempt.index() != 0) {
        co_return std::unexpected(error::service_stopped);
    }
    auto& [exception, result] = std::get<0>(attempt);
    if (exception) {
        try {
            std::rethrow_exception(exception);
        } catch (std::exception const& e) {
            spdlog::warn("[p2p_node] Configured connection attempt threw: {}", e.what());
        } catch (...) {
            spdlog::warn("[p2p_node] Configured connection attempt threw an unknown exception");
        }
        co_return std::unexpected(error::operation_failed);
    }
    co_return std::move(result);
}

::asio::awaitable<bool> p2p_node::wait_configured_retry(std::chrono::seconds delay) {
    auto executor = co_await ::asio::this_coro::executor;
    auto [ec, selected] = co_await ::asio::experimental::make_select(
        ::asio::experimental::after(std::chrono::steady_clock::now() + delay))
        .async_wait(executor, {}, stop_token(), ::asio::as_tuple(::asio::use_awaitable));
    co_return !ec && !stopped();
}

::asio::awaitable<void> p2p_node::maintain_configured_peer(std::string host, uint16_t port) {
    using namespace ::asio::experimental;
    auto executor = co_await ::asio::this_coro::executor;
    auto delay = 1s;
    uint64_t failures = 0;
    while ( ! stopped()) {
        auto ended = std::make_shared<peer_lifecycle_channel>(1);
        // Both branches run on this owner's strand. Stop also covers DNS,
        // connect, admission and handshake, not just the later retry wait.
        auto result = co_await detail::run_configured_attempt(connect(host, port, ended), stop_token());
        if (stopped()) co_return;

        // A close is latched even if the session ended before connect returned.
        // In particular, never race a replacement against removal of the old
        // peer from the manager. No payload can be lost to a full channel.
        auto [ended_ec, completion] = co_await make_select(receive(*ended))
            .async_wait(executor, {}, stop_token(), ::asio::as_tuple(::asio::use_awaitable));
        if (stopped()) co_return;

        bool const healthy = result && !ended_ec && completion.value<0>()->admitted &&
            completion.value<0>()->connected_for >= 5s;
        bool const occupied = !result && result.error() == error::address_in_use;
        if (healthy) {
            failures = 0;
            delay = 1s;
        } else if ( ! occupied) {
            ++failures;
            if (result) {
                // Align with the supervisor's short-session cooldown. A version
                // exchange alone must not reset a failing endpoint's backoff.
                delay = std::max(delay, 30s);
                spdlog::warn("[p2p_node] Configured peer {}:{} disconnected before becoming stable", host, port);
            } else {
                spdlog::warn("[p2p_node] Failed to connect to configured peer {}:{} - {}",
                    host, port, result.error().message());
            }
            // Zero means unlimited; only a stable session starts a new episode.
            if (settings_.manual_attempt_limit && failures >= settings_.manual_attempt_limit) {
                spdlog::warn("[p2p_node] Configured peer {}:{} reached its attempt limit", host, port);
                co_return;
            }
        } else {
            // An inbound or discovered session may already hold this IP. It
            // owes a later retry, not a failed-handshake budget charge.
            spdlog::debug("[p2p_node] Configured peer {}:{} has an already connected IP", host, port);
        }

        if ( ! co_await wait_configured_retry(delay)) co_return;
        if ( ! healthy) delay = std::min(delay * 2, 60s);
    }
}

} // namespace kth::node
