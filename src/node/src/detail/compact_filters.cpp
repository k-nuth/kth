// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/compact_filters.hpp>

#include <stop_token>
#include <system_error>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/this_coro.hpp>

#include <kth/blockchain/header_index.hpp>
#include <kth/domain/chain/compact_filter.hpp>
#include <kth/node/detail/compact_filter_store.hpp>
#include <kth/node/p2p_node.hpp>

#include <spdlog/spdlog.h>

namespace kth::node::detail {

namespace {

using index_t = blockchain::header_index::index_t;
constexpr auto null_index = blockchain::header_index::null_index;

// A chain as seen from its last block: an entry is on it when the last
// block's ancestor at the entry's height is the entry itself. Entries and
// their parent links never change once published, so what is read through
// this stays one chain whatever the node connects meanwhile.
struct chain_to {
    blockchain::header_index const& index;
    index_t last;
    int32_t height;

    [[nodiscard]]
    bool contains(index_t idx) const {
        if (idx == null_index) {
            return false;
        }
        auto const at = index.get_height(idx);
        return at >= 0 && at <= height && index.get_ancestor(last, at) == idx;
    }

    [[nodiscard]]
    index_t at(size_t at_height) const {
        return index.get_ancestor(last, int32_t(at_height));
    }
};

std::optional<chain_to> connected_chain(blockchain::block_chain const& chain) {
    auto const view = chain.chain_view();
    if ( ! view) {
        return std::nullopt;
    }
    auto const& index = chain.headers();
    auto const tip = index.find(view->tip_hash);
    if (tip == null_index) {
        return std::nullopt;
    }
    return chain_to{index, tip, index.get_height(tip)};
}

// The connected chain up to `stop`, when `stop` is on it.
std::optional<chain_to> connected_chain_to(blockchain::block_chain const& chain, hash_digest const& stop) {
    auto const connected = connected_chain(chain);
    if ( ! connected) {
        return std::nullopt;
    }
    auto const idx = connected->index.find(stop);
    if ( ! connected->contains(idx)) {
        return std::nullopt;
    }
    return chain_to{connected->index, idx, connected->index.get_height(idx)};
}

// =============================================================================
// The index
// =============================================================================

enum class progress {
    // Every block of the connected chain has its record.
    caught_up,
    // The slice ended with blocks still to read.
    behind,
    // A block could not be read. Tried again at the next look at the chain.
    stalled,
};

struct index_owner {
    index_owner(compact_filter_blocks blocks, kth::path file, compact_filter_index_pace pace)
        : blocks_(std::move(blocks))
        , store_(std::move(file))
        , pace_(pace)
    {}

    void load() {
        store_.load();
        // Which block the last record belongs to is the file's word; whether
        // that block is still on the connected chain is settled at the first
        // look at it.
        tip_ = store_.empty() ? null_index : blocks_.chain.headers().find(store_.tip());
        if ( ! store_.empty()
            && (tip_ == null_index || blocks_.chain.headers().get_height(tip_) != int32_t(store_.size() - 1))) {
            spdlog::warn("[compact filters] the filter headers stored are of a block this node does not have "
                "at height {}; they are computed again", store_.size() - 1);
            drop_all();
        }
        if ( ! store_.empty()) {
            spdlog::info("[compact filters] filter headers loaded up to height {}", store_.size() - 1);
        }
    }

    // Reads up to a slice of the blocks that have no record yet.
    ::asio::awaitable<progress> advance() {
        auto const connected = connected_chain(blocks_.chain);
        if ( ! connected) {
            co_return progress::caught_up;
        }
        follow(*connected);
        auto const tip_height = size_t(connected->height);
        for (size_t read = 0; read < pace_.slice && store_.size() <= tip_height; ++read) {
            auto const height = store_.size();
            auto const idx = connected->at(height);
            auto const block = connected->index.get_hash(idx);
            auto const filter = co_await build_compact_filter(blocks_, block);
            if ( ! filter) {
                spdlog::warn("[compact filters] no filter for the block at height {}: {}; the filter headers "
                    "stop below it until the chain moves", height, filter.error().message());
                save();
                co_return progress::stalled;
            }
            store_.append(bitcoin_hash(*filter));
            tip_ = idx;
            if (store_.size() % pace_.save_every == 0) {
                save();
            }
            if (store_.size() % (pace_.save_every * 50) == 0) {
                spdlog::info("[compact filters] filter headers computed up to height {}", height);
            }
        }
        if (store_.size() <= tip_height) {
            co_return progress::behind;
        }
        save();
        co_return progress::caught_up;
    }

    void answer(compact_filter_index_query& query) {
        std::visit([this](auto& asked) {
            if (asked.reply) {
                (void)asked.reply->try_send(answer_to(asked.request));
            }
        }, query);
    }

    void save() {
        if (store_.saved()) {
            return;
        }
        auto const tip = tip_ == null_index ? null_hash : blocks_.chain.headers().get_hash(tip_);
        // A failure is logged by the store and tried again at the next save:
        // the records stay in memory.
        (void)store_.save(tip);
    }

private:
    void drop_all() {
        store_.truncate(0);
        tip_ = null_index;
    }

    // Drops the records of blocks that are not on `connected`: what a
    // reorganization left behind, from the end.
    void follow(chain_to const& connected) {
        while ( ! store_.empty() && ! connected.contains(tip_)) {
            store_.truncate(store_.size() - 1);
            tip_ = store_.empty() ? null_index : connected.index.get_parent_index(tip_);
        }
    }

    // The connected chain up to `stop`, when the records reach it.
    std::optional<chain_to> recorded_chain_to(hash_digest const& stop) {
        auto const connected = connected_chain(blocks_.chain);
        if ( ! connected) {
            return std::nullopt;
        }
        // A query may arrive before the ring of the reorganization it follows.
        follow(*connected);
        auto const idx = connected->index.find(stop);
        if ( ! connected->contains(idx)) {
            return std::nullopt;
        }
        auto const height = connected->index.get_height(idx);
        if (size_t(height) >= store_.size()) {
            return std::nullopt;
        }
        return chain_to{connected->index, idx, height};
    }

    std::optional<domain::message::compact_filter_headers>
    answer_to(domain::message::get_compact_filter_headers const& request) {
        if (request.filter_type != domain::message::regular_compact_filter) {
            return std::nullopt;
        }
        auto const chain = recorded_chain_to(request.stop_hash);
        if ( ! chain || uint64_t(request.start_height) > uint64_t(chain->height)) {
            return std::nullopt;
        }
        auto const first = size_t(request.start_height);
        auto const count = size_t(chain->height) - first + 1;
        if (count > domain::message::max_compact_filter_headers) {
            return std::nullopt;
        }
        domain::message::compact_filter_headers answer;
        answer.filter_type = request.filter_type;
        answer.stop_hash = request.stop_hash;
        answer.previous_filter_header = store_.header_before(first);
        answer.filter_hashes.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            answer.filter_hashes.push_back(store_.at(first + i).filter_hash);
        }
        return answer;
    }

    std::optional<domain::message::compact_filter_checkpoint>
    answer_to(domain::message::get_compact_filter_checkpoint const& request) {
        if (request.filter_type != domain::message::regular_compact_filter) {
            return std::nullopt;
        }
        auto const chain = recorded_chain_to(request.stop_hash);
        if ( ! chain) {
            return std::nullopt;
        }
        auto const count = size_t(chain->height) / pace_.interval;
        if (count > domain::message::max_compact_filter_checkpoints) {
            return std::nullopt;
        }
        domain::message::compact_filter_checkpoint answer;
        answer.filter_type = request.filter_type;
        answer.stop_hash = request.stop_hash;
        answer.filter_headers.reserve(count);
        for (size_t i = 1; i <= count; ++i) {
            answer.filter_headers.push_back(store_.at(i * pace_.interval).filter_header);
        }
        return answer;
    }

    compact_filter_blocks const blocks_;
    compact_filter_store store_;
    compact_filter_index_pace const pace_;
    // The block of the last record.
    index_t tip_{null_index};
};

template <typename Request, typename Answer>
::asio::awaitable<std::optional<Answer>> ask(compact_filter_channels& channels, Request const& request) {
    using query = compact_filter_query<Request, Answer>;
    auto reply = std::make_shared<typename query::reply_channel>(1);
    // Refused when the index is behind on its queries, or ended. Nothing of a
    // refused request is kept.
    if ( ! channels.queries.try_send(compact_filter_index_query{query{request, reply}})) {
        co_return std::nullopt;
    }
    auto const executor = co_await ::asio::this_coro::executor;
    auto completion = co_await ::asio::experimental::make_select(
        ::asio::experimental::receive(*reply)
    ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, answered] = completion;
    if (ec) {
        co_return std::nullopt;
    }
    co_return std::move(*answered.template value<0>());
}

} // namespace

compact_filter_channels::compact_filter_channels()
    : queries(query_capacity)
    , tip_events(1)
{}

void compact_filter_channels::tip_moved() {
    (void)tip_events.try_send(std::monostate{});
}

void compact_filter_channels::close() {
    queries.close();
    tip_events.close();
}

compact_filter_blocks::compact_filter_blocks(blockchain::block_chain const& chain, domain::chain::block genesis)
    : chain(chain)
    , genesis(std::move(genesis))
    , genesis_hash(this->genesis.hash())
{}

awaitable_expected<data_chunk> build_compact_filter(compact_filter_blocks const& blocks,
    hash_digest const& block) {
    if (block == blocks.genesis_hash) {
        auto const filter = domain::chain::build_regular_filter(blocks.genesis);
        if ( ! filter) {
            co_return std::unexpected(error::operation_failed);
        }
        co_return filter->to_data();
    }
    auto const fetched = co_await blocks.chain.fetch_block(block);
    if ( ! fetched) {
        co_return std::unexpected(fetched.error());
    }
    auto const filter = domain::chain::build_regular_filter(*fetched->first);
    if ( ! filter) {
        co_return std::unexpected(error::operation_failed);
    }
    co_return filter->to_data();
}

std::optional<hash_list> compact_filter_range(blockchain::block_chain const& chain, uint32_t start_height,
    hash_digest const& stop, size_t limit) {
    auto const range = connected_chain_to(chain, stop);
    if ( ! range || uint64_t(start_height) > uint64_t(range->height)) {
        return std::nullopt;
    }
    auto const count = size_t(range->height) - start_height + 1;
    if (count > limit) {
        return std::nullopt;
    }
    // Walked down from the stop block through parent links, then in order.
    hash_list blocks(count);
    auto idx = range->last;
    for (auto at = count; at > 0; --at) {
        blocks[at - 1] = range->index.get_hash(idx);
        idx = range->index.get_parent_index(idx);
    }
    return blocks;
}

::asio::awaitable<void> run_compact_filter_index(compact_filter_channels& channels,
    compact_filter_blocks blocks, kth::path file, compact_filter_index_pace pace) {
    spdlog::debug("[compact filters] Index started");
    auto const executor = co_await ::asio::this_coro::executor;
    index_owner owner(std::move(blocks), std::move(file), pace);
    owner.load();

    // Whether the chain is to be looked at: at the start, at each ring, and
    // while a slice left blocks to read.
    bool look = true;
    while (true) {
        if (look) {
            look = co_await owner.advance() == progress::behind;
        }
        // What was asked meanwhile, before more blocks are read: at most what
        // the channel holds, so a steady flow of queries does not keep the
        // index from the chain.
        std::error_code ec;
        for (size_t answered = 0; answered < compact_filter_channels::query_capacity; ++answered) {
            auto query = channels.queries.try_receive(ec);
            if ( ! query) {
                break;
            }
            owner.answer(*query);
        }
        if (ec == ::asio::error::eof) {
            break;                                  // closed: the node is stopping
        }
        if (look) {
            continue;
        }

        // The ring first: a query that follows it is answered from the chain
        // it announced.
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(channels.tip_events),
            ::asio::experimental::receive(channels.queries)
        ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [wait_ec, selected] = completion;
        if (wait_ec) {
            break;
        }
        if (selected.index == 0) {
            look = true;
        } else {
            owner.answer(*selected.template value<1>());
        }
    }

    // What the channel still held when it was closed is answered: nothing
    // more can be put in it.
    std::error_code drain_ec;
    while (auto query = channels.queries.try_receive(drain_ec)) {
        owner.answer(*query);
    }
    owner.save();
    spdlog::debug("[compact filters] Index ended");
}

::asio::awaitable<std::optional<domain::message::compact_filter_headers>>
ask_compact_filter_headers(compact_filter_channels& channels,
    domain::message::get_compact_filter_headers const& request) {
    return ask<domain::message::get_compact_filter_headers, domain::message::compact_filter_headers>(
        channels, request);
}

::asio::awaitable<std::optional<domain::message::compact_filter_checkpoint>>
ask_compact_filter_checkpoint(compact_filter_channels& channels,
    domain::message::get_compact_filter_checkpoint const& request) {
    return ask<domain::message::get_compact_filter_checkpoint, domain::message::compact_filter_checkpoint>(
        channels, request);
}

::asio::awaitable<message_result> handle_get_compact_filters(compact_filter_blocks const& blocks,
    network::peer_session& peer, domain::message::get_compact_filters const& request) {
    if (request.filter_type != domain::message::regular_compact_filter) {
        spdlog::debug("[compact filters] getcfilters for filter type {} from [{}]; not answered",
            request.filter_type, peer.authority());
        co_return message_result::handled;
    }
    auto const range = compact_filter_range(blocks.chain, request.start_height, request.stop_hash,
        domain::message::max_compact_filters_per_request);
    if ( ! range) {
        spdlog::debug("[compact filters] getcfilters from [{}] names no range of the connected chain",
            peer.authority());
        co_return message_result::handled;
    }
    for (auto const& block : *range) {
        auto filter = co_await build_compact_filter(blocks, block);
        if ( ! filter) {
            spdlog::debug("[compact filters] no filter for block {} asked by [{}]: {}",
                encode_hash(block), peer.authority(), filter.error().message());
            co_return message_result::handled;
        }
        domain::message::compact_filter const message{request.filter_type, block, std::move(*filter)};
        if (co_await peer.send_data(message)) {
            // The peer is gone; nothing more can be sent to it.
            co_return message_result::handled;
        }
    }
    co_return message_result::handled;
}

::asio::awaitable<message_result> handle_get_compact_filter_headers(compact_filter_channels& channels,
    network::peer_session& peer, domain::message::get_compact_filter_headers const& request) {
    auto const answer = co_await ask_compact_filter_headers(channels, request);
    if ( ! answer) {
        spdlog::debug("[compact filters] getcfheaders from [{}] not answered", peer.authority());
        co_return message_result::handled;
    }
    (void)co_await peer.send_data(*answer);
    co_return message_result::handled;
}

::asio::awaitable<message_result> handle_get_compact_filter_checkpoint(compact_filter_channels& channels,
    network::peer_session& peer, domain::message::get_compact_filter_checkpoint const& request) {
    auto const answer = co_await ask_compact_filter_checkpoint(channels, request);
    if ( ! answer) {
        spdlog::debug("[compact filters] getcfcheckpt from [{}] not answered", peer.authority());
        co_return message_result::handled;
    }
    (void)co_await peer.send_data(*answer);
    co_return message_result::handled;
}

} // namespace kth::node::detail
