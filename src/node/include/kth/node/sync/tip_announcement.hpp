// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_TIP_ANNOUNCEMENT_HPP
#define KTH_NODE_SYNC_TIP_ANNOUNCEMENT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <optional>
#include <vector>

#include <asio/awaitable.hpp>

#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/message/compact_block.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/domain/message/header.hpp>
#include <kth/network/define.hpp>
#include <kth/network/peer_session.hpp>

namespace kth::node::sync {

// Announcing a newly connected tip to peers, as BCHN does: with the headers
// connected since the last announcement to a peer that asked for headers
// (BIP130 sendheaders), and with an inv of the tip otherwise. A peer that
// asked for high-bandwidth compact blocks (BIP152 sendcmpct(1, 1)) gets the
// one new block as a cmpctblock instead.

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

/// The cmpctblock of a connected block, to announce it with. Null when it
/// cannot be built.
using compact_block_source = std::function<
    ::asio::awaitable<domain::message::compact_block::const_ptr>(hash_digest const& hash)>;

/// Announce `tip` to `peer` from the peer's own service, after
/// `last_announced`, what the service last announced to it: headers to a peer
/// that sent sendheaders when they connect, an inv of the tip otherwise, on
/// the peer's data queue so they follow the headers it was served before. A
/// peer that prefers compact blocks and is owed only the block after the last
/// announcement gets that block's cmpctblock from `compact`, when it gives
/// one. Updates `last_announced` when the tip moved forward.
[[nodiscard]] KN_API
::asio::awaitable<void> announce_tip(blockchain::header_index const& index,
    network::peer_session& peer, hash_digest const& tip, std::optional<hash_digest>& last_announced,
    compact_block_source compact = {});

/// The cmpctblock of the latest tip announced with one, built once from the
/// stored block and handed to every peer's service that asks for it, as BCHN
/// builds one for the block it last connected.
///
/// Ownership: run() is the one task that holds the cmpctblock and builds the
/// next. The peers' services ask it through a bounded channel and wait for
/// its answer; they ask at once when a tip connects, and all of them get the
/// one it built.
struct KN_API compact_tip_source {
    /// The chain outlives run().
    explicit compact_tip_source(blockchain::block_chain const& chain);

    compact_tip_source(compact_tip_source const&) = delete;
    compact_tip_source& operator=(compact_tip_source const&) = delete;

    /// The cmpctblock of `hash`, from a peer's service. Null when the block
    /// cannot be read, or run() has ended.
    [[nodiscard]]
    ::asio::awaitable<domain::message::compact_block::const_ptr> get(hash_digest const& hash);

    /// Until `stop` is requested. Every request it took is answered first.
    ::asio::awaitable<void> run(std::stop_token stop);

private:
    using answer_channel = select_channel<domain::message::compact_block::const_ptr>;

    struct request {
        hash_digest hash;
        std::shared_ptr<answer_channel> answer;
    };

    blockchain::block_chain const& chain_;
    select_channel<request> requests_;
};

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_TIP_ANNOUNCEMENT_HPP
