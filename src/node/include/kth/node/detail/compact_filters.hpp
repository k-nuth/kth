// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed, not exported, no ABI.

#ifndef KTH_NODE_DETAIL_COMPACT_FILTERS_HPP
#define KTH_NODE_DETAIL_COMPACT_FILTERS_HPP

#if ! defined(KTH_WITH_NEUTRINO)
#error "Compact filters are an optional feature: build with the Conan option neutrino=True."
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

#include <asio/awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/message/compact_filter.hpp>
#include <kth/domain/message/compact_filter_checkpoint.hpp>
#include <kth/domain/message/compact_filter_headers.hpp>
#include <kth/domain/message/get_compact_filter_checkpoint.hpp>
#include <kth/domain/message/get_compact_filter_headers.hpp>
#include <kth/domain/message/get_compact_filters.hpp>
#include <kth/infrastructure/handlers.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/path.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/node/define.hpp>

namespace kth::node {

enum class message_result;

namespace detail {

// =============================================================================
// Serving compact block filters (the regular filter, BIP157/158)
// =============================================================================
//
// A filter is built from its block when a peer asks for it, by the peer's own
// service: nothing of it is shared.
//
// A filter header chains back to the first block, so the node keeps the filter
// hash and the filter header of every block of the connected chain: 64 bytes a
// block (compact_filter_store.hpp). One task owns those records, the index. It
// follows the connected chain, reading each new block once, and it answers
// what is asked about the records. The peers' services reach it through
// `compact_filter_channels` and wait for its answer; nothing else touches the
// records.

template <typename Request, typename Answer>
struct compact_filter_query {
    using reply_channel = select_channel<std::optional<Answer>>;

    Request request;
    // Answered once by the index, and closed behind the answer: nothing when
    // the request is not answered. Closed without an answer when the index
    // ends first.
    std::shared_ptr<reply_channel> reply;
};

using compact_filter_headers_query = compact_filter_query<
    domain::message::get_compact_filter_headers, domain::message::compact_filter_headers>;
using compact_filter_checkpoint_query = compact_filter_query<
    domain::message::get_compact_filter_checkpoint, domain::message::compact_filter_checkpoint>;

using compact_filter_index_query = std::variant<compact_filter_headers_query, compact_filter_checkpoint_query>;

// What the peers' services and the sync coordinator share with the index.
// Created and closed by the node.
struct KND_API compact_filter_channels {
    // Queries between two turns of the index, which answers each from memory.
    // One past it is refused, and its request goes unanswered: the wallet asks
    // again, or asks another peer.
    static constexpr size_t query_capacity = 64;

    compact_filter_channels();

    compact_filter_channels(compact_filter_channels const&) = delete;
    compact_filter_channels& operator=(compact_filter_channels const&) = delete;

    // The connected tip moved. Never waits: rings coalesce, and the index
    // reads the chain from where it got to.
    void tip_moved();

    // End it: the index sees the channels closed.
    void close();

    select_channel<compact_filter_index_query> queries;
    select_channel<std::monostate> tip_events;
};

// The block a filter is built from, by its hash. The node stores no record of
// the first block, so that one is the configured genesis block.
struct compact_filter_blocks {
    compact_filter_blocks(blockchain::block_chain const& chain, domain::chain::block genesis);

    blockchain::block_chain const& chain;
    domain::chain::block const genesis;
    hash_digest const genesis_hash;
};

// The regular filter of a block the node holds, as it goes on the wire.
[[nodiscard]] KND_API
awaitable_expected<data_chunk> build_compact_filter(compact_filter_blocks const& blocks, hash_digest const& block);

// The blocks from `start_height` up to `stop` on the connected chain, lowest
// first, when `stop` is on it and they are at most `limit`. Otherwise nothing:
// such a request is not answered.
[[nodiscard]] KND_API
std::optional<hash_list> compact_filter_range(blockchain::block_chain const& chain, uint32_t start_height,
    hash_digest const& stop, size_t limit);

// How the index paces itself.
struct compact_filter_index_pace {
    // Blocks read between two looks at the queries.
    size_t slice = 32;
    // Records between two saves while it catches up. It saves when it reaches
    // the connected tip, too.
    size_t save_every = 1000;
    // A `cfcheckpt` carries the filter header of every block at a multiple of
    // this height.
    size_t interval = domain::message::compact_filter_checkpoint_interval;
};

// The index: until `channels` is closed. It loads `file`, drops the records of
// blocks that left the connected chain, reads the blocks it has no record of,
// and answers the queries between slices of that. It looks at the chain when
// it starts and at each `tip_moved`.
[[nodiscard]] KND_API
::asio::awaitable<void> run_compact_filter_index(compact_filter_channels& channels,
    compact_filter_blocks blocks, kth::path file, compact_filter_index_pace pace = {});

// The answer to a `getcfheaders`, asked of the index. Nothing when it is not
// answered: another filter type, a stop block off the connected chain, a range
// beyond the limit or past the records, or an index that is busy or ended.
[[nodiscard]] KND_API
::asio::awaitable<std::optional<domain::message::compact_filter_headers>>
ask_compact_filter_headers(compact_filter_channels& channels,
    domain::message::get_compact_filter_headers const& request);

// The answer to a `getcfcheckpt`, asked of the index. Nothing when it is not
// answered.
[[nodiscard]] KND_API
::asio::awaitable<std::optional<domain::message::compact_filter_checkpoint>>
ask_compact_filter_checkpoint(compact_filter_channels& channels,
    domain::message::get_compact_filter_checkpoint const& request);

// The three requests, served in the peer's service, off its read loop. A
// request that cannot be answered is ignored, as other Bitcoin Cash nodes do:
// the wallet asks another peer.

/// getcfilters -> one cfilter for each block, lowest first.
[[nodiscard]] KND_API
::asio::awaitable<message_result> handle_get_compact_filters(compact_filter_blocks const& blocks,
    network::peer_session& peer, domain::message::get_compact_filters const& request);

/// getcfheaders -> cfheaders.
[[nodiscard]] KND_API
::asio::awaitable<message_result> handle_get_compact_filter_headers(compact_filter_channels& channels,
    network::peer_session& peer, domain::message::get_compact_filter_headers const& request);

/// getcfcheckpt -> cfcheckpt.
[[nodiscard]] KND_API
::asio::awaitable<message_result> handle_get_compact_filter_checkpoint(compact_filter_channels& channels,
    network::peer_session& peer, domain::message::get_compact_filter_checkpoint const& request);

} // namespace detail
} // namespace kth::node

#endif // KTH_NODE_DETAIL_COMPACT_FILTERS_HPP
