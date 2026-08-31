// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_BLOCK_ANNOUNCEMENTS_HPP
#define KTH_NODE_DETAIL_BLOCK_ANNOUNCEMENTS_HPP

// Not installed. Reading a block announcement off the wire, and nothing else.
//
// Three messages carry the same meaning — a block exists that you may not have
// — and the node's read loop turns each into the same list of hashes. Kept as
// pure functions so that what each of the three understands can be stated on
// its own, without a peer, a socket or a node to say it through.

#include <kth/domain/message/compact_block.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::node::detail {

/// The hashes announced by a `headers` message nobody asked for: every header
/// in it. An unparseable message announces nothing.
[[nodiscard]]
hash_list announced_by_headers(data_chunk const& payload, uint32_t version);

/// The hash announced by a `cmpctblock`: the block header it opens with. The
/// compact body is not read and no BIP152 support is implied.
[[nodiscard]]
hash_list announced_by_compact_block(data_chunk const& payload, uint32_t version);

/// The block hashes named by an `inv`, both plain and compact-block entries.
/// Transactions and everything else are not this path's business and are left
/// out — which is a decision, not an oversight: see the caller, which says so
/// when an `inv` announces no blocks.
[[nodiscard]]
hash_list announced_by_inventory(data_chunk const& payload, uint32_t version);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_BLOCK_ANNOUNCEMENTS_HPP
