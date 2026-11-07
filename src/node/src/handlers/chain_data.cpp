// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/handlers/chain_data.hpp>

#include <algorithm>

#include <kth/domain/message/block.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/not_found.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/node/p2p_node.hpp>

#include <spdlog/spdlog.h>

namespace kth::node::handlers::chain_data {

namespace {

using index_t = blockchain::header_index::index_t;
constexpr auto null_index = blockchain::header_index::null_index;

// The connected chain as seen from its tip: an entry is on it when the tip's
// ancestor at the entry's height is the entry itself.
struct connected_chain {
    blockchain::header_index const& index;
    index_t tip;
    int32_t tip_height;

    [[nodiscard]]
    bool contains(index_t idx) const {
        if (idx == null_index) {
            return false;
        }
        auto const height = index.get_height(idx);
        return height >= 0 && height <= tip_height && index.get_ancestor(tip, height) == idx;
    }
};

} // namespace

domain::message::header::list headers_after_locator(blockchain::header_index const& index,
    hash_digest const& tip, hash_list const& locator, hash_digest const& stop, size_t limit) {

    auto const tip_idx = index.find(tip);
    if (tip_idx == null_index || limit == 0) {
        return {};
    }
    connected_chain const chain{index, tip_idx, index.get_height(tip_idx)};
    auto const stop_idx = stop == null_hash ? null_index : index.find(stop);

    if (locator.empty()) {
        if ( ! chain.contains(stop_idx)) {
            return {};
        }
        return {index.get_header(stop_idx)};
    }

    // The fork point: the first locator entry on the connected chain, or genesis.
    int32_t fork_height = 0;
    for (auto const& hash : locator) {
        auto const idx = index.find(hash);
        if (chain.contains(idx)) {
            fork_height = index.get_height(idx);
            break;
        }
    }

    auto const first = int64_t(fork_height) + 1;
    auto last = std::min(int64_t(chain.tip_height), first + int64_t(limit) - 1);
    if (chain.contains(stop_idx) && index.get_height(stop_idx) >= first) {
        last = std::min(last, int64_t(index.get_height(stop_idx)));
    }
    if (last < first) {
        return {};
    }

    // Walked down from the last one through parent links, then filled in order.
    domain::message::header::list headers(size_t(last - first + 1));
    auto idx = index.get_ancestor(tip_idx, int32_t(last));
    for (auto at = headers.size(); at > 0; --at) {
        headers[at - 1] = index.get_header(idx);
        idx = index.get_parent_index(idx);
    }
    return headers;
}

bool block_request_allowed(blockchain::header_index const& index, hash_digest const& tip,
    hash_digest const& hash) {

    auto const tip_idx = index.find(tip);
    if (tip_idx == null_index) {
        return false;
    }
    connected_chain const chain{index, tip_idx, index.get_height(tip_idx)};
    auto const idx = index.find(hash);
    return chain.contains(idx) && index.has_block_data(idx);
}

::asio::awaitable<message_result> handle_get_headers(blockchain::block_chain& chain,
    network::peer_session& peer, domain::message::get_headers const& request) {

    if (request.start_hashes().size() > max_get_headers_locator) {
        spdlog::debug("[chain_data] getheaders locator of {} entries from [{}]; disconnecting",
            request.start_hashes().size(), peer.authority());
        co_return message_result::disconnect;
    }
    auto const view = chain.chain_view();
    if ( ! view) {
        co_return message_result::handled;
    }
    auto headers = domain::message::headers::create(headers_after_locator(chain.headers(),
        view->tip_hash, request.start_hashes(), request.stop_hash()));
    if ( ! headers) {
        co_return message_result::handled;
    }
    spdlog::debug("[chain_data] Sending {} header(s) to [{}]", headers->elements().size(),
        peer.authority());
    (void)co_await peer.send_data(*headers);
    co_return message_result::handled;
}

::asio::awaitable<message_result> handle_get_data(blockchain::block_chain& chain,
    network::peer_session& peer, domain::message::get_data const& request) {

    using type_id = domain::message::inventory_vector::type_id;
    auto const view = chain.chain_view();
    domain::message::inventory_vector::list missing;
    for (auto const& item : request.inventories()) {
        if (item.type() != type_id::block && item.type() != type_id::compact_block) {
            continue;
        }
        if (view && block_request_allowed(chain.headers(), view->tip_hash, item.hash())) {
            auto const block = co_await chain.fetch_block(item.hash());
            if (block) {
                if (co_await peer.send_data(*block->first)) {
                    // The peer is gone; nothing more can be sent to it.
                    co_return message_result::handled;
                }
                continue;
            }
            spdlog::debug("[chain_data] Block {} requested by [{}] could not be read: {}",
                encode_hash(item.hash()), peer.authority(), block.error().message());
        }
        missing.push_back(item);
    }
    if ( ! missing.empty()) {
        if (auto const reply = domain::message::not_found::create(std::move(missing))) {
            (void)co_await peer.send_data(*reply);
        }
    }
    co_return message_result::handled;
}

::asio::awaitable<message_result> handle_get_transactions(blockchain::block_chain& chain,
    network::peer_session& peer, domain::message::get_data const& request) {

    using type_id = domain::message::inventory_vector::type_id;
    // Read beside the organizers' writes, which the pool allows.
    auto const& pool = chain.mempool_ref();
    domain::message::inventory_vector::list missing;
    for (auto const& item : request.inventories()) {
        if (item.type() != type_id::transaction) {
            continue;
        }
        if (auto const tx = pool.get(item.hash())) {
            if (co_await peer.send_data(*tx)) {
                // The peer is gone; nothing more can be sent to it.
                co_return message_result::handled;
            }
            continue;
        }
        missing.push_back(item);
    }
    if ( ! missing.empty()) {
        if (auto const reply = domain::message::not_found::create(std::move(missing))) {
            (void)co_await peer.send_data(*reply);
        }
    }
    co_return message_result::handled;
}

} // namespace kth::node::handlers::chain_data
