// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/tip_announcement.hpp>

#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/inventory.hpp>

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
    network::peer_session& peer, hash_digest const& tip, std::optional<hash_digest>& last_announced) {

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
    code ec;
    if ( ! announcement->headers.empty() && peer.prefers_headers()) {
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

} // namespace kth::node::sync
