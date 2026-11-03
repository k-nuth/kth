// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_BLOCK_RELAY_HPP
#define KTH_NODE_BLOCK_RELAY_HPP

#include <cstdint>
#include <memory>

#include <asio/awaitable.hpp>

#include <kth/blockchain/header_index.hpp>
#include <kth/domain/message/header.hpp>
#include <kth/network/peer_manager.hpp>
#include <kth/node/define.hpp>

namespace kth::domain::message {
struct compact_block;
}

namespace kth::node {

enum class block_announcement { none, headers, inventory, compact };

/// Copy at most the last nine connected headers while the builder still holds
/// its reorg participation. An incomplete range is withheld.
[[nodiscard]] KND_API
domain::message::header::list connected_block_headers(blockchain::header_index const& index,
    uint32_t first, uint32_t last);

/// Queue an announcement without waiting for the peer. For a longer branch,
/// the caller need only supply its last nine headers: only the tip is sent.
/// `compact`, when set, is the cmpctblock for `incoming`'s single tip. A peer
/// in high-bandwidth mode that already knows the parent is offered that
/// instead of headers or inv. A null compact leaves the headers/inv choice.
[[nodiscard]] KND_API
block_announcement announce_blocks(network::peer_session& peer,
    domain::message::header::list const& incoming, bool caught_up,
    network::peer_session const* source = nullptr,
    domain::message::compact_block const* compact = nullptr);

/// The peer snapshot may wait for its strand; no peer send can wait for space.
KND_API ::asio::awaitable<void> announce_blocks(network::peer_manager& peers,
    domain::message::header::list incoming,
    std::shared_ptr<domain::message::compact_block const> compact = nullptr);

} // namespace kth::node

#endif // KTH_NODE_BLOCK_RELAY_HPP
