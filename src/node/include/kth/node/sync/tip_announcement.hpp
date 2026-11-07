// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_TIP_ANNOUNCEMENT_HPP
#define KTH_NODE_SYNC_TIP_ANNOUNCEMENT_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <asio/awaitable.hpp>

#include <kth/blockchain/header_index.hpp>
#include <kth/domain/message/header.hpp>
#include <kth/network/define.hpp>
#include <kth/network/peer_session.hpp>

namespace kth::node::sync {

// Announcing a newly connected tip to peers, as BCHN does: with the headers
// connected since the last announcement to a peer that asked for headers
// (BIP130 sendheaders), and with an inv of the tip otherwise.

/// BCHN's limit: past this many new blocks, or after a reorganization, the
/// headers would not connect for the peer, so the tip goes out as an inv.
constexpr size_t max_blocks_to_announce = 8;

/// The connected tip is announced only while it is this recent, as BCHN keeps
/// quiet during initial block download (its default maximum tip age).
constexpr uint32_t announcement_max_tip_age_seconds = 24 * 60 * 60;

struct tip_announcement {
    hash_digest tip;
    /// The headers after the last announced block up to the tip, in order.
    /// Empty when the tip goes out as an inv to every peer.
    domain::message::header::list headers;
};

/// What to announce for `tip` after `last_announced`. Nothing when the tip is
/// the last announced block or one of its ancestors (the chain did not move
/// forward).
[[nodiscard]] KN_API
std::optional<tip_announcement> next_tip_announcement(blockchain::header_index const& index,
    std::optional<hash_digest> const& last_announced, hash_digest const& tip);

/// Announce `tip` to `peer` from the peer's own service, after
/// `last_announced`, what the service last announced to it: headers to a peer
/// that sent sendheaders when they connect, an inv of the tip otherwise, on
/// the peer's data queue so they follow the headers it was served before.
/// Updates `last_announced` when the tip moved forward.
[[nodiscard]] KN_API
::asio::awaitable<void> announce_tip(blockchain::header_index const& index,
    network::peer_session& peer, hash_digest const& tip, std::optional<hash_digest>& last_announced);

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_TIP_ANNOUNCEMENT_HPP
