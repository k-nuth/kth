// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/block_serving.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

#include <kth/domain/message/compact_block.hpp>
#include <kth/network/peer_session.hpp>

namespace kth::node {

namespace {

using blockchain::header_index;
using blockchain::header_status;

// The height of `hash` when it is on the active chain.
std::optional<int64_t> active_height(header_index const& index, hash_digest const& hash) {
    auto const idx = index.find(hash);
    if (idx == header_index::null_index) {
        return std::nullopt;
    }
    auto const height = index.get_height(idx);
    if (index.active_at(height) != idx) {
        return std::nullopt;
    }
    return height;
}

} // namespace

domain::message::headers answer_header_request(
    domain::message::get_headers const& request, blockchain::block_chain const& chain,
    size_t limit) {
    auto const heights = chain.get_last_heights();
    if ( ! heights) {
        return {};
    }
    auto const& index = chain.headers();
    // The active chain holds headers ahead of the blocks; offer only what a
    // peer can then download.
    auto const top = std::min<int64_t>(heights->block, index.active_tip_height());

    domain::message::header::list answer;
    if (request.start_hashes().empty()) {
        auto const height = active_height(index, request.stop_hash());
        if (height && *height <= top) {
            answer.emplace_back(index.get_header(index.active_at(int32_t(*height))));
        }
        return domain::message::headers::create(std::move(answer)).value_or(domain::message::headers{});
    }

    int64_t fork = 0;
    for (auto const& hash : request.start_hashes()) {
        if (auto const height = active_height(index, hash)) {
            fork = *height;
            break;
        }
    }

    auto last = std::min(top, fork + int64_t(std::min(limit, max_get_headers)));
    if (auto const stop = active_height(index, request.stop_hash()); stop && *stop > fork) {
        last = std::min(last, *stop);
    }
    for (auto height = fork + 1; height <= last; ++height) {
        answer.emplace_back(index.get_header(index.active_at(int32_t(height))));
    }
    return domain::message::headers::create(std::move(answer)).value_or(domain::message::headers{});
}

bool is_block_request(domain::message::inventory_vector const& entry) {
    using type_id = domain::message::inventory_vector::type_id;
    return entry.type() == type_id::block || entry.type() == type_id::compact_block;
}

std::optional<domain::message::compact_block> compact_block_reply(domain::message::block const& block) {
    if (block.transactions().empty()) {
        return std::nullopt;
    }
    return domain::message::compact_block::factory_from_block(block);
}

std::optional<domain::message::block_transactions> block_transactions_reply(
    domain::message::block const& block, domain::message::get_block_transactions const& request) {
    auto const& indexes = request.indexes();
    // create/from_data guarantee increasing indexes. Check the last before
    // reserving, so even an oversized request cannot allocate past this block.
    if (request.block_hash() != block.hash()
        || ( ! indexes.empty() && indexes.back() >= block.transactions().size())) {
        return std::nullopt;
    }
    domain::chain::transaction::list transactions;
    transactions.reserve(indexes.size());
    for (auto const index : indexes) {
        transactions.push_back(block.transactions()[static_cast<size_t>(index)]);
    }
    return domain::message::block_transactions(request.block_hash(), std::move(transactions));
}

::asio::awaitable<domain::message::block::const_ptr> servable_block(
    blockchain::block_chain const& chain, hash_digest const& hash) {
    auto const& index = chain.headers();
    auto const idx = index.find(hash);
    if (idx == header_index::null_index) {
        co_return nullptr;
    }
    auto const status = index.get_status(idx);
    if (blockchain::has_flag(status, header_status::failed_valid)
        || blockchain::has_flag(status, header_status::failed_child)) {
        co_return nullptr;
    }
    auto const block = co_await chain.fetch_block(hash);
    co_return block ? block->first : nullptr;
}

::asio::awaitable<std::optional<domain::message::compact_block>> servable_compact_block(
    blockchain::block_chain const& chain, hash_digest const& hash) {
    auto const block = co_await servable_block(chain, hash);
    if ( ! block) {
        co_return std::nullopt;
    }
    // Check after fetching: the chain may have reorganized during disk I/O.
    auto const view = chain.chain_view();
    auto const& index = chain.headers();
    auto const height = active_height(index, hash);
    if ( ! view || ! height || size_t(*height) > view->connected_tip_height
        || index.get_ancestor(index.find(view->tip_hash), int32_t(*height)) != index.find(hash)) {
        co_return std::nullopt;
    }
    co_return compact_block_reply(*block);
}

::asio::awaitable<void> serve_block_transactions(blockchain::block_chain const& chain,
    network::peer_session& peer, network::raw_message const& raw) {
    if ( ! peer.supports_compact_blocks()) {
        co_return;
    }
    byte_reader reader(raw.payload);
    auto const request = domain::message::get_block_transactions::from_data(reader, peer.negotiated_version());
    if ( ! request || reader.remaining_size() != 0) {
        co_return;
    }
    auto const block = co_await servable_block(chain, request->block_hash());
    if ( ! block) {
        co_return;
    }
    if (auto const answer = block_transactions_reply(*block, *request)) {
        if (co_await peer.send(*answer) == error::success) {
            peer.remember_block(request->block_hash());
        }
    }
    co_return;
}

::asio::awaitable<code> send_compact_block(blockchain::block_chain const& chain,
    network::peer_session& peer, domain::message::block const& block) {
    if (peer.supports_compact_blocks()) {
        if (auto const compact = co_await servable_compact_block(chain, block.hash())) {
            co_return co_await peer.send(*compact);
        }
    }
    co_return co_await peer.send(block);
}

} // namespace kth::node
