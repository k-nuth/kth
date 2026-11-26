// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_RESULT_BRIDGE_HPP
#define KTH_NODE_DETAIL_RESULT_BRIDGE_HPP

// Not installed. Carries what a pipeline stage reports to the coordinator.
//
// The coordinator advances through consecutive results, so one that is lost
// leaves a gap nothing fills: the range stays in flight and no later body is
// asked for. The carrier therefore waits for room instead of dropping.
//
// Nothing waits on the carrier in turn. The stages put their results in `input`
// with sends that never suspend, so a carrier waiting for room holds back
// neither validation nor storage, nor a switch waiting for storage to park.

#include <string_view>
#include <system_error>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

#include <kth/node/sync/messages.hpp>

namespace kth::node::detail {

/// Carry each result of `input` to `events`, in order, waiting for room. Ends
/// when `input` is closed or when `events` is: a send waiting for room is
/// completed by the close of `events`. `name` labels its logs.
template <typename Input>
::asio::awaitable<void> result_bridge(
    Input& input,
    sync::sync_coordinator_event_channel& events,
    std::string_view name)
{
    spdlog::debug("[{}] Started", name);
    while (true) {
        auto [ec, result] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            spdlog::debug("[{}] Channel closed: {}", name, ec.message());
            break;
        }
        auto [send_ec] = co_await events.async_send(std::error_code{},
            std::move(result), ::asio::as_tuple(::asio::use_awaitable));
        if (send_ec) {
            spdlog::debug("[{}] Coordinator gone: {}", name, send_ec.message());
            break;
        }
    }
    spdlog::debug("[{}] Task ended", name);
}

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_RESULT_BRIDGE_HPP
