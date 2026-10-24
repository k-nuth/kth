// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_BLOCK_ANNOUNCEMENTS_HPP
#define KTH_NODE_DETAIL_BLOCK_ANNOUNCEMENTS_HPP

// Not installed. Reading block headers and block announcements off the wire,
// and nothing else.
//
// `headers` and `cmpctblock` carry headers, and both go to the node's header
// ingest. `inv` carries only hashes, which are registered as announcements.
// Kept as pure functions so that what each of the three understands can be
// stated on its own, without a peer, a socket or a node to say it through.

#include <optional>

#include <kth/domain/message/compact_block.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::node::detail {

/// The headers a `headers` message carries, empty ones included: an empty
/// `headers` is how a peer says it has nothing past our locator. Nothing for an
/// unparseable message.
[[nodiscard]]
std::optional<domain::message::header::list> headers_carried(data_chunk const& payload, uint32_t version);

/// The block header a `cmpctblock` opens with. The compact body is not read and
/// no BIP152 support is implied. Nothing for an unparseable message.
[[nodiscard]]
std::optional<domain::message::header> header_of_compact_block(data_chunk const& payload, uint32_t version);

/// The block hashes named by an `inv`, both plain and compact-block entries.
/// Transactions and everything else are not this path's business and are left
/// out — which is a decision, not an oversight: see the caller, which says so
/// when an `inv` announces no blocks.
[[nodiscard]]
hash_list announced_by_inventory(data_chunk const& payload, uint32_t version);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_BLOCK_ANNOUNCEMENTS_HPP
