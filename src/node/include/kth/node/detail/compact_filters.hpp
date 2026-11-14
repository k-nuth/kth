// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_COMPACT_FILTERS_HPP
#define KTH_NODE_DETAIL_COMPACT_FILTERS_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

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
#include <kth/infrastructure/utility/data.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/node/define.hpp>

namespace kth::node {

enum class message_result;

namespace detail {

// The filter header of one block of the connected chain, with the block it
// belongs to, so a list of them is recognized after a reorganization.
struct compact_filter_checkpoint_record {
    hash_digest block{null_hash};
    hash_digest filter_header{null_hash};

    [[nodiscard]]
    friend bool operator==(compact_filter_checkpoint_record const&,
        compact_filter_checkpoint_record const&) = default;
};

// What the node needs to answer the compact filter messages, and nothing kept
// per block.
//
// A filter is built from its block when a peer asks and stays in memory while
// it is recent. A filter hash is the hash of that filter, kept in memory the
// same way. Filter headers chain back to the first block, so one is kept for
// every `interval` blocks (a thousand: the ones a `cfcheckpt` carries), in
// memory and in one small file, and any other is chained from the one below
// it. That file is the only thing stored: 64 bytes for every thousand blocks.
class KND_API compact_filter_service {
public:
    using filter_ptr = std::shared_ptr<data_chunk const>;

    static constexpr size_t default_filter_cache_bytes = 32 * 1024 * 1024;
    static constexpr size_t default_hash_cache_entries = 1 << 18;

    compact_filter_service(blockchain::block_chain const& chain, domain::chain::block genesis,
        kth::path checkpoint_file,
        size_t interval = domain::message::compact_filter_checkpoint_interval,
        size_t filter_cache_bytes = default_filter_cache_bytes,
        size_t hash_cache_entries = default_hash_cache_entries);

    compact_filter_service(compact_filter_service const&) = delete;
    compact_filter_service& operator=(compact_filter_service const&) = delete;

    // Reads the checkpoint file. A file that is not whole records is ignored;
    // records of another chain are dropped by the next extend().
    void load();

    // Brings the checkpoints up to the connected tip: a filter hash for every
    // block since the last one. One pass runs at a time, and a second caller
    // returns at once. `stopped` is asked between blocks.
    [[nodiscard]] ::asio::awaitable<void> extend(std::function<bool()> stopped = {});

    // The filter of a block the node holds, as it goes on the wire.
    [[nodiscard]] awaitable_expected<filter_ptr> filter_of(hash_digest const& block);

    // The blocks from `start_height` up to `stop` on the connected chain, when
    // `stop` is on it and they are at most `limit`. Otherwise nothing: such a
    // request is not answered.
    [[nodiscard]] std::optional<hash_list> blocks_of(uint32_t start_height, hash_digest const& stop,
        size_t limit) const;

    // The answer to a `getcfheaders`, or nothing when it is not answered: an
    // unknown filter type, a stop block off the connected chain, a range
    // beyond the limit, or a filter header below it that is not computed yet.
    [[nodiscard]] ::asio::awaitable<std::optional<domain::message::compact_filter_headers>>
    headers(domain::message::get_compact_filter_headers const& request);

    // The answer to a `getcfcheckpt`, or nothing when it is not answered.
    [[nodiscard]] std::optional<domain::message::compact_filter_checkpoint>
    checkpoint(domain::message::get_compact_filter_checkpoint const& request) const;

    // The records held, lowest first: entry i is the block at height
    // (i + 1) * interval.
    [[nodiscard]] std::vector<compact_filter_checkpoint_record> checkpoints() const;

    [[nodiscard]] size_t interval() const noexcept { return interval_; }

private:
    [[nodiscard]] filter_ptr cached_filter(hash_digest const& block);
    void keep_filter(hash_digest const& block, filter_ptr const& filter);
    [[nodiscard]] std::optional<hash_digest> cached_hash(hash_digest const& block) const;
    void keep_hash(hash_digest const& block, hash_digest const& filter_hash);
    [[nodiscard]] awaitable_expected<hash_digest> filter_hash_of(hash_digest const& block);
    void save() const;

    blockchain::block_chain const& chain_;
    domain::chain::block const genesis_;
    hash_digest const genesis_hash_;
    kth::path const checkpoint_file_;
    size_t const interval_;
    size_t const filter_cache_bytes_;
    size_t const hash_cache_entries_;

    mutable std::mutex mutex_;
    // Newest first.
    std::list<std::pair<hash_digest, filter_ptr>> filters_;
    std::map<hash_digest, std::list<std::pair<hash_digest, filter_ptr>>::iterator> filter_by_block_;
    size_t filter_bytes_ = 0;
    // Oldest first.
    std::deque<hash_digest> hash_order_;
    std::map<hash_digest, hash_digest> hash_by_block_;
    std::vector<compact_filter_checkpoint_record> checkpoints_;
    std::atomic<bool> extending_{false};
};

// The three requests, served in the peer's service, off its read loop. A
// request that cannot be answered is ignored, as other Bitcoin Cash nodes do:
// the wallet asks another peer.

/// getcfilters -> one cfilter for each block, lowest first.
[[nodiscard]] KND_API
::asio::awaitable<message_result> handle_get_compact_filters(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filters const& request);

/// getcfheaders -> cfheaders.
[[nodiscard]] KND_API
::asio::awaitable<message_result> handle_get_compact_filter_headers(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filter_headers const& request);

/// getcfcheckpt -> cfcheckpt.
[[nodiscard]] KND_API
::asio::awaitable<message_result> handle_get_compact_filter_checkpoint(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filter_checkpoint const& request);

} // namespace detail
} // namespace kth::node

#endif // KTH_NODE_DETAIL_COMPACT_FILTERS_HPP
