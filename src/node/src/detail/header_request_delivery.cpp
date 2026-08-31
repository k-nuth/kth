// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/header_request_delivery.hpp>

#include <asio/as_tuple.hpp>
#include <asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

namespace kth::node::detail {

::asio::awaitable<bool> deliver_header_request(
    sync::header_download_input_channel& channel,
    sync::header_request request)
{
    auto [ec] = co_await channel.async_send(
        std::error_code{}, std::move(request), ::asio::as_tuple(::asio::use_awaitable));

    if (ec) {
        spdlog::debug("[sync_coordinator] Header request channel closed; request abandoned");
        co_return false;
    }
    co_return true;
}

} // namespace kth::node::detail
