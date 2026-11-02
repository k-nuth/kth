// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/block_relay.hpp>

#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/inventory.hpp>

namespace kth::node {

domain::message::header::list connected_block_headers(blockchain::header_index const& index,
    uint32_t first, uint32_t last) {
    domain::message::header::list incoming;
    auto const start = last - first >= 8 ? last - 8 : first;
    for (uint64_t height = start; height <= last; ++height) {
        auto const entry = index.active_at(int32_t(height));
        if (entry == blockchain::header_index::null_index) {
            return {};
        }
        incoming.emplace_back(index.get_header(entry));
    }
    return incoming;
}

block_announcement announce_blocks(network::peer_session& peer,
    domain::message::header::list const& incoming, bool caught_up,
    network::peer_session const* source) {
    if ( ! caught_up || incoming.empty() || &peer == source || peer.stopped()) {
        return block_announcement::none;
    }

    auto const tip = domain::chain::hash(incoming.back());
    if (peer.knows_block(tip)) {
        return block_announcement::none;
    }
    bool const use_headers = peer.wants_headers() && incoming.size() <= 8 &&
        peer.knows_block(incoming.front().previous_block_hash());

    // Blocks are remembered only once their announcement is queued. A peer
    // whose queue is full misses this one and learns the block when it next
    // asks for headers, so it is not recorded as knowing a block it was never
    // told about.
    if (use_headers) {
        // Both factories below can only reject a count above their wire cap.
        auto const message = domain::message::headers::create(incoming);
        if ( ! peer.try_send(*message)) {
            return block_announcement::none;
        }
        for (auto const& header : incoming) {
            peer.remember_block(domain::chain::hash(header));
        }
        return block_announcement::headers;
    }

    hash_list hashes;
    if ( ! peer.wants_headers() && incoming.size() <= 8) {
        for (auto const& header : incoming) {
            auto const hash = domain::chain::hash(header);
            if (hash != tip && ! peer.knows_block(hash)) {
                hashes.push_back(hash);
            }
        }
    }
    hashes.push_back(tip);
    auto const message = domain::message::inventory::create(hashes,
        domain::message::inventory::type_id::block);
    if ( ! peer.try_send(*message)) {
        return block_announcement::none;
    }
    for (auto const& hash : hashes) {
        peer.remember_block(hash);
    }
    return block_announcement::inventory;
}

::asio::awaitable<void> announce_blocks(network::peer_manager& peers,
    domain::message::header::list incoming) {
    for (auto const& peer : co_await peers.all()) {
        (void)announce_blocks(*peer, incoming, true);
    }
    co_return;
}

} // namespace kth::node
