// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/this_coro.hpp>
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

compact_tip_source::compact_tip_source(blockchain::block_chain const& chain)
    : chain_(chain)
    , requests_(16)
{}

::asio::awaitable<domain::message::compact_block::const_ptr> compact_tip_source::get(hash_digest const& hash) {
    auto executor = co_await ::asio::this_coro::executor;
    auto const answer = std::make_shared<answer_channel>(1);
    // Waits for room, then for the answer: run() answers every request it
    // takes, and closes the channel when it ends.
    auto asked = co_await ::asio::experimental::make_select(
        ::asio::experimental::send(requests_, request{hash, answer})
    ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    if (std::get<0>(asked)) {
        co_return nullptr;
    }
    auto completion = co_await ::asio::experimental::make_select(
        ::asio::experimental::receive(*answer)
    ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    if (ec) {
        co_return nullptr;
    }
    co_return std::move(*selected.value<0>());
}

::asio::awaitable<void> compact_tip_source::run(std::stop_token stop) {
    auto executor = co_await ::asio::this_coro::executor;
    // Owned here: the one cmpctblock held, and the block it is of.
    hash_digest held_hash = null_hash;
    domain::message::compact_block::const_ptr held;

    auto const answer = [&](request const& asked) -> ::asio::awaitable<void> {
        if ( ! held || held_hash != asked.hash) {
            auto const block = co_await chain_.fetch_block(asked.hash);
            if ( ! block || block->first->transactions().empty()) {
                spdlog::debug("[tip_announcement] No cmpctblock for {}: the block could not be read",
                    encode_hash(asked.hash));
                (void)asked.answer->try_send(nullptr);
                co_return;
            }
            held = std::make_shared<domain::message::compact_block const>(
                domain::message::compact_block::factory_from_block(*block->first));
            held_hash = asked.hash;
        }
        auto shared = held;
        (void)asked.answer->try_send(std::move(shared));
    };

    while ( ! stop.stop_requested()) {
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(requests_)
        ).async_wait(executor, {}, stop, ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) {
            break;
        }
        co_await answer(*selected.value<0>());
    }

    // Nobody is left waiting: later requests are refused, and those already
    // queued are told there is none.
    requests_.close();
    std::error_code drained;
    while (auto const left = requests_.try_receive(drained)) {
        (void)left->answer->try_send(nullptr);
    }
}

} // namespace kth::node::sync
