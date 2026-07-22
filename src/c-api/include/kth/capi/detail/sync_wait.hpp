// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_SYNC_WAIT_HPP_
#define KTH_CAPI_DETAIL_SYNC_WAIT_HPP_

#include <utility>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

namespace kth::capi {

// Drive an awaitable to completion synchronously on the chain's executor and
// return the awaited value (typically std::expected<T, code> for fetch_*
// methods, or code for organize()). This is how the synchronous C-API wraps the
// node's asio::awaitable interface. Templated on the chain type so this header
// stays free of the heavy block_chain include.
template <typename Chain, typename Awaitable>
auto sync_wait(Chain& chain, Awaitable awaitable) {
    auto future = ::asio::co_spawn(
        chain.executor(), std::move(awaitable), ::asio::use_future);
    return future.get();
}

} // namespace kth::capi

#endif // KTH_CAPI_DETAIL_SYNC_WAIT_HPP_
