// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_HANDLERS_CHAIN_DATA_HPP
#define KTH_NODE_HANDLERS_CHAIN_DATA_HPP

#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/constants/common.hpp>
#include <kth/domain/message/get_data.hpp>
#include <kth/domain/message/get_headers.hpp>
#include <kth/domain/message/header.hpp>
#include <kth/network/define.hpp>
#include <kth/network/peer_session.hpp>

#include <asio/awaitable.hpp>

namespace kth::node {

enum class message_result;

namespace handlers::chain_data {

// What peers are served is the connected chain: the blocks the node validated,
// ending at the published tip. Headers the node holds above it, or on another
// branch, are not offered, as in BCHN. Everything is resolved from the tip's
// hash through parent links, which never change once an entry is published, so
// a reorganization in progress cannot mix two branches into one answer.

/// The headers after the first locator hash on the connected chain ending at
/// `tip`, up to `stop` (included) and at most `limit` of them. With no locator
/// hash on that chain the answer starts after genesis. An empty locator asks
/// for the `stop` header alone.
[[nodiscard]] KN_API
domain::message::header::list headers_after_locator(blockchain::header_index const& index,
    hash_digest const& tip, hash_list const& locator, hash_digest const& stop,
    size_t limit = max_get_headers);

/// Whether the block is on the connected chain ending at `tip` and its body is
/// stored, so it may be sent to a peer.
[[nodiscard]] KN_API
bool block_request_allowed(blockchain::header_index const& index, hash_digest const& tip,
    hash_digest const& hash);

// Both handlers run in the peer's service, not in its read loop (registered
// with register_served_message_handler), and send on the peer's data queue:
// a slow peer holds only its own service, while its read loop keeps reading
// and its pings are answered on the control queue. Requests are served in
// arrival order, so answers keep the order of the requests.

/// getheaders -> headers. A locator longer than BCHN accepts disconnects.
[[nodiscard]] KN_API
::asio::awaitable<message_result> handle_get_headers(blockchain::block_chain& chain,
    network::peer_session& peer, domain::message::get_headers const& request);

/// getdata -> one block message per requested block, in request order, and a
/// notfound for the blocks that cannot be served. A compact block request is
/// answered with the full block. Transaction entries are not handled here.
[[nodiscard]] KN_API
::asio::awaitable<message_result> handle_get_data(blockchain::block_chain& chain,
    network::peer_session& peer, domain::message::get_data const& request);

/// getdata -> one tx message per requested transaction the mempool holds, in
/// request order, and a notfound for the requested transactions it does not.
/// Block entries are not handled here.
[[nodiscard]] KN_API
::asio::awaitable<message_result> handle_get_transactions(blockchain::block_chain& chain,
    network::peer_session& peer, domain::message::get_data const& request);

} // namespace handlers::chain_data
} // namespace kth::node

#endif // KTH_NODE_HANDLERS_CHAIN_DATA_HPP
