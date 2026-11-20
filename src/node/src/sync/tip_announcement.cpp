// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/tip_announcement.hpp>

#include <memory>
#include <utility>

#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

#include <spdlog/spdlog.h>

namespace kth::node::sync {

std::optional<tip_announcement> next_tip_announcement(blockchain::header_index const& index,
    std::optional<hash_digest> const& last_announced, hash_digest const& tip) {

    using blockchain::header_index;
    auto const tip_idx = index.find(tip);
    if (tip_idx == header_index::null_index) {
        return std::nullopt;
    }
    auto const tip_height = index.get_height(tip_idx);
    tip_announcement announcement{tip, {}};
    if ( ! last_announced) {
        return announcement;
    }

    auto const last_idx = index.find(*last_announced);
    if (last_idx != header_index::null_index) {
        auto const last_height = index.get_height(last_idx);
        if (last_height >= tip_height) {
            // Not forward: the same tip, a tip that moved down onto an
            // ancestor, or a sibling branch no longer than the announced one.
            if (index.get_ancestor(last_idx, tip_height) == tip_idx) {
                return std::nullopt;
            }
            return announcement;
        }
        if (index.get_ancestor(tip_idx, last_height) == last_idx &&
                size_t(tip_height - last_height) <= max_blocks_to_announce) {
            announcement.headers.resize(size_t(tip_height - last_height));
            auto idx = tip_idx;
            for (auto at = announcement.headers.size(); at > 0; --at) {
                announcement.headers[at - 1] = index.get_header(idx);
                idx = index.get_parent_index(idx);
            }
        }
    }
    return announcement;
}

::asio::awaitable<void> announce_tip(blockchain::header_index const& index,
    network::peer_session& peer, hash_digest const& tip, std::optional<hash_digest>& last_announced,
    compact_block_source compact) {

    // Nothing announced to this peer yet: it is taken to hold the tip's
    // parent, what the node connected before. If it does not, the headers do
    // not connect for it and it asks with getheaders, which is served.
    auto base = last_announced;
    if ( ! base) {
        auto const idx = index.find(tip);
        auto const parent = idx == blockchain::header_index::null_index
            ? blockchain::header_index::null_index : index.get_parent_index(idx);
        if (parent != blockchain::header_index::null_index) {
            base = index.get_hash(parent);
        }
    }
    auto const announcement = next_tip_announcement(index, base, tip);
    if ( ! announcement) {
        co_return;
    }
    last_announced = tip;
    // BIP152 high bandwidth: one new block on what the peer was last told
    // about goes out whole but for the transactions its mempool should hold.
    // Further than that the peer could not connect it, as with headers.
    domain::message::compact_block::const_ptr compact_tip;
    if (announcement->headers.size() == 1 && peer.prefers_compact_blocks() && compact) {
        compact_tip = co_await compact(tip);
    }
    code ec;
    if (compact_tip) {
        ec = co_await peer.send_data(*compact_tip);
    } else if ( ! announcement->headers.empty() && peer.prefers_headers()) {
        auto headers = domain::message::headers::create(announcement->headers);
        if ( ! headers) {
            co_return;
        }
        ec = co_await peer.send_data(*headers);
    } else {
        using domain::message::inventory_vector;
        auto inventory = domain::message::inventory::create({{inventory_vector::type_id::block, tip}});
        if ( ! inventory) {
            co_return;
        }
        ec = co_await peer.send_data(*inventory);
    }
    if (ec) {
        spdlog::debug("[tip_announcement] Could not announce to [{}]: {}", peer.authority(), ec.message());
    }
}

::asio::awaitable<domain::message::compact_block::const_ptr> compact_tip_cache::get(
    blockchain::block_chain const& chain, hash_digest const& hash) {

    {
        std::lock_guard const lock(mutex_);
        if (compact_ && hash_ == hash) {
            co_return compact_;
        }
    }
    auto const block = co_await chain.fetch_block(hash);
    if ( ! block || block->first->transactions().empty()) {
        spdlog::debug("[tip_announcement] No cmpctblock for {}: the block could not be read",
            encode_hash(hash));
        co_return nullptr;
    }
    // Built outside the lock: services that miss together each build one,
    // and the last one built is kept. Every one is a valid cmpctblock.
    auto built = std::make_shared<domain::message::compact_block const>(
        domain::message::compact_block::factory_from_block(*block->first));
    std::lock_guard const lock(mutex_);
    hash_ = hash;
    compact_ = built;
    co_return built;
}

} // namespace kth::node::sync
