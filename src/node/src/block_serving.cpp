// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/block_serving.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

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

} // namespace kth::node
