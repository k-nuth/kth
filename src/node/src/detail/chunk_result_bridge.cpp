// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/chunk_result_bridge.hpp>

#include <system_error>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

namespace kth::node::detail {

::asio::awaitable<void> chunk_result_bridge(
    sync::chunk_validated_channel& input,
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
        // Awaited: a result dropped here leaves a gap in the consecutive chunks
        // and a range that stays in flight, so later bodies are never requested.
        auto [send_ec] = co_await events.async_send(std::error_code{},
            std::move(result), ::asio::as_tuple(::asio::use_awaitable));
        if (send_ec) {
            spdlog::debug("[{}] Coordinator channel closed", name);
            break;
        }
    }
    spdlog::info("[{}] Task ended", name);
}

} // namespace kth::node::detail
