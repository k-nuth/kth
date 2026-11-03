// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_PEER_EVENTS_HPP
#define KTH_NODE_DETAIL_PEER_EVENTS_HPP

#include <chrono>
#include <expected>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/this_coro.hpp>

#include <kth/node/p2p_node.hpp>

namespace kth::node::detail {

// The sync layer's wait for the next peer event, the deadline or the
// network's stop. One selection, so exactly one of them commits: a deadline
// that fires with an event ready leaves it in peer_events() for the next wait.
// It replaced `receive || timer`, which could drop that event, and whose
// cancellation of the losing branch raced its completion on the thread pool
// (#772). The stop token may be requested from any thread, before or during
// the wait. Errors: channel_timeout at the deadline, operation_aborted on
// stop, eof once the channel is closed and empty.
[[nodiscard]]
inline awaitable_expected<peer_notification> wait_for_peer_event(
    p2p_node& network, std::chrono::steady_clock::time_point deadline)
{
    auto executor = co_await ::asio::this_coro::executor;
    auto completion = co_await ::asio::experimental::make_select(
        ::asio::experimental::receive(network.peer_events()),
        ::asio::experimental::after(deadline)
    ).async_wait(executor, {}, network.stop_token(), ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    if (ec) {
        co_return std::unexpected(ec);
    }
    if (selected.index == 1) {
        co_return std::unexpected(error::channel_timeout);
    }
    co_return std::move(*selected.value<0>());
}

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_PEER_EVENTS_HPP
