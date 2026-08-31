// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_HEADER_REQUEST_DELIVERY_HPP
#define KTH_NODE_DETAIL_HEADER_REQUEST_DELIVERY_HPP

// Not installed. Handing a header request to the download task.
//
// Its own function because the difference between "sent" and "sent if there
// happens to be room" is the difference between a walk that continues and a
// node that stops asking — and because a decision worth stating is worth being
// able to test on its own.

#include <asio/awaitable.hpp>

#include <kth/node/sync/messages.hpp>

namespace kth::node::detail {

/// Hand a header request to the download task, waiting for room rather than
/// giving up when there is none.
///
/// A request dropped on a full channel is a walk that never happens, and for an
/// announcement it is worse than that: the hashes have already been taken off
/// the node, so nothing is left to raise it again. The only outcome that
/// abandons the request is a closed channel, which happens on shutdown and
/// nowhere else.
///
/// Answers whether the request was accepted.
[[nodiscard]]
::asio::awaitable<bool> deliver_header_request(
    sync::header_download_input_channel& channel,
    sync::header_request request);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_HEADER_REQUEST_DELIVERY_HPP
