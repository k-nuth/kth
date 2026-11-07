// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_HANDLERS_CHAIN_DATA_HPP
#define KTH_NODE_HANDLERS_CHAIN_DATA_HPP

#include <cstddef>
#include <optional>

#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/constants/common.hpp>
#include <kth/domain/message/block.hpp>
#include <kth/domain/message/block_transactions.hpp>
#include <kth/domain/message/get_block_transactions.hpp>
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

/// A compact block request (BIP152) is answered with a cmpctblock for a block
/// at most this far below the connected tip, as in BCHN. A peer that asks for
/// an older one is unlikely to hold its transactions, and gets the full block.
constexpr size_t max_compact_block_depth = 5;

/// The blocktxn answering `request` from `block` (BIP152): its transactions at
/// the requested positions, in that order. Nothing when the request names
/// another block or a position past the block's last transaction.
[[nodiscard]] KN_API
std::optional<domain::message::block_transactions> block_transactions_for(
    domain::message::block const& block, domain::message::get_block_transactions const& request);

// The handlers run in the peer's service, not in its read loop (registered
// with register_served_message_handler), and send on the peer's data queue:
// a slow peer holds only its own service, while its read loop keeps reading
// and its pings are answered on the control queue. Requests are served in
// arrival order, so answers keep the order of the requests.

/// getheaders -> headers. A locator longer than BCHN accepts disconnects.
[[nodiscard]] KN_API
::asio::awaitable<message_result> handle_get_headers(blockchain::block_chain& chain,
    network::peer_session& peer, domain::message::get_headers const& request);

/// getdata -> one block message per requested block, in request order, and a
/// notfound for the blocks that cannot be served. A compact block request for
/// a block near the connected tip is answered with a cmpctblock that prefills
/// the coinbase, and for an older one with the full block. Transaction entries
/// are not handled here.
[[nodiscard]] KN_API
::asio::awaitable<message_result> handle_get_data(blockchain::block_chain& chain,
    network::peer_session& peer, domain::message::get_data const& request);

/// getblocktxn -> blocktxn from the stored block, for a block on the connected
/// chain. A request for no transaction, or for a position past the block,
/// disconnects, as in BCHN. A block that cannot be served is not answered.
[[nodiscard]] KN_API
::asio::awaitable<message_result> handle_get_block_transactions(blockchain::block_chain& chain,
    network::peer_session& peer, domain::message::get_block_transactions const& request);

} // namespace handlers::chain_data
} // namespace kth::node

#endif // KTH_NODE_HANDLERS_CHAIN_DATA_HPP
