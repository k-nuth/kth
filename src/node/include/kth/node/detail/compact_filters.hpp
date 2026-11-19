// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_COMPACT_FILTERS_HPP
#define KTH_NODE_DETAIL_COMPACT_FILTERS_HPP

#include <array>
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
#include <kth/blockchain/spent_outputs.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/script_filter.hpp>
#include <kth/domain/message/compact_filter.hpp>
#include <kth/domain/message/compact_filter_checkpoint.hpp>
#include <kth/domain/message/compact_filter_headers.hpp>
#include <kth/domain/message/compact_filter_proof.hpp>
#include <kth/domain/message/compact_filter_range.hpp>
#include <kth/domain/message/compact_filter_tree.hpp>
#include <kth/domain/message/get_compact_filter_checkpoint.hpp>
#include <kth/domain/message/get_compact_filter_headers.hpp>
#include <kth/domain/message/get_compact_filter_proof.hpp>
#include <kth/domain/message/get_compact_filter_range.hpp>
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
//
// Three filter types are served, each with its own memory and its own file.
// The regular filter (type 0) is built from the block alone. The script filter
// (type 1) also needs the outputs the block spends, which the node keeps in
// the undo record of every block it validated: it is served once
// serve_script_filters() names its file, and for the blocks that have that
// record. The input prefix filter (type 2) is built from the block alone too:
// it is served once serve_input_prefix_filters() names its file, and for
// every block the node holds.
class KND_API compact_filter_service {
public:
    using filter_ptr = std::shared_ptr<data_chunk const>;

    // What the script filter of a block, and the leaves of its entry tree, are
    // built from: the block, and the outputs it spends in the order of its
    // inputs, each with the height it was created at. Both are read from what
    // the node stores already, the block and its undo record.
    struct script_source {
        std::shared_ptr<domain::chain::block const> block;
        blockchain::spent_output_list spent;
    };
    using script_source_ptr = std::shared_ptr<script_source const>;

    // The entry tree of a block's script filter, with what it was built from:
    // what a `cfrange` and a `cfproof` are answered from.
    struct script_tree {
        script_source_ptr source;
        std::vector<domain::chain::script_leaf> leaves;
        hash_list transaction_hashes;
    };
    using script_tree_ptr = std::shared_ptr<script_tree const>;

    static constexpr size_t default_filter_cache_bytes = 32 * 1024 * 1024;
    static constexpr size_t default_hash_cache_entries = 1 << 18;
    // A wallet that settles a disagreement asks many times about one block,
    // or about the two blocks it compares.
    static constexpr size_t script_tree_cache_entries = 2;

    compact_filter_service(blockchain::block_chain const& chain, domain::chain::block genesis,
        kth::path checkpoint_file,
        size_t interval = domain::message::compact_filter_checkpoint_interval,
        size_t filter_cache_bytes = default_filter_cache_bytes,
        size_t hash_cache_entries = default_hash_cache_entries);

    compact_filter_service(compact_filter_service const&) = delete;
    compact_filter_service& operator=(compact_filter_service const&) = delete;

    // Serves the script filter too, with its filter header records in `file`.
    // Called before load(), and before any request is answered.
    void serve_script_filters(kth::path file);

    // Serves the input prefix filter too, with its filter header records in
    // `file`. Called before load(), and before any request is answered.
    void serve_input_prefix_filters(kth::path file);

    [[nodiscard]] bool serves(uint8_t filter_type) const noexcept;

    // Reads the checkpoint files. A file that is not whole records is
    // ignored; records of another chain are dropped by the next extend().
    void load();

    // Brings the checkpoints up to the connected tip: a filter hash for every
    // block since the last one. One pass runs at a time, and a second caller
    // returns at once. `stopped` is asked between blocks.
    [[nodiscard]] ::asio::awaitable<void> extend(std::function<bool()> stopped = {});

    // The filter of a block the node holds, as it goes on the wire.
    [[nodiscard]] awaitable_expected<filter_ptr> filter_of(hash_digest const& block,
        uint8_t filter_type = domain::message::regular_compact_filter);

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

    // The answer to a `getcfrange`, or nothing when it is not answered: a
    // filter type without an entry tree, a block off the connected chain or
    // without the outputs it spends, a prefix that is not well formed, or more
    // leaves than fit one message, which a longer prefix splits.
    [[nodiscard]] ::asio::awaitable<std::optional<domain::message::compact_filter_range>>
    range(domain::message::get_compact_filter_range const& request);

    // The answer to a `getcfproof`, or nothing when it is not answered: as for
    // a range, or a leaf index past the last leaf.
    [[nodiscard]] ::asio::awaitable<std::optional<domain::message::compact_filter_proof>>
    proof(domain::message::get_compact_filter_proof const& request);

    // The records held for a filter type, lowest first: entry i is the block
    // at height (i + 1) * interval.
    [[nodiscard]] std::vector<compact_filter_checkpoint_record>
    checkpoints(uint8_t filter_type = domain::message::regular_compact_filter) const;

    [[nodiscard]] size_t interval() const noexcept { return interval_; }

private:
    using filter_list = std::list<std::pair<hash_digest, filter_ptr>>;

    // What is kept for one filter type. Guarded by mutex_, but for `type`,
    // `served` and `file`, which are set before the service is used.
    struct lane {
        uint8_t type{0};
        bool served{false};
        kth::path file;
        // Newest first.
        filter_list filters;
        std::map<hash_digest, filter_list::iterator> filter_by_block;
        size_t filter_bytes = 0;
        // Oldest first.
        std::deque<hash_digest> hash_order;
        std::map<hash_digest, hash_digest> hash_by_block;
        std::vector<compact_filter_checkpoint_record> checkpoints;
        // The block a pass could not build a filter for, and its height. While
        // it is on the connected chain the pass has nothing to do.
        std::optional<std::pair<size_t, hash_digest>> stalled;
    };

    [[nodiscard]] lane* lane_of(uint8_t filter_type) noexcept;
    [[nodiscard]] lane const* lane_of(uint8_t filter_type) const noexcept;
    [[nodiscard]] filter_ptr cached_filter(lane& of, hash_digest const& block);
    void keep_filter(lane& of, hash_digest const& block, filter_ptr const& filter);
    [[nodiscard]] std::optional<hash_digest> cached_hash(lane const& of, hash_digest const& block) const;
    void keep_hash(lane& of, hash_digest const& block, hash_digest const& filter_hash);
    [[nodiscard]] awaitable_expected<filter_ptr> build(uint8_t filter_type, hash_digest const& block);
    [[nodiscard]] awaitable_expected<hash_digest> filter_hash_of(lane& of, hash_digest const& block);
    [[nodiscard]] awaitable_expected<script_source_ptr> script_source_of(hash_digest const& block);
    [[nodiscard]] awaitable_expected<script_tree_ptr> script_tree_of(hash_digest const& block);
    [[nodiscard]] ::asio::awaitable<void> extend_lane(lane& of, std::function<bool()> const& stopped);
    void load_lane(lane& of);
    void save_lane(lane const& of) const;

    blockchain::block_chain const& chain_;
    domain::chain::block const genesis_;
    hash_digest const genesis_hash_;
    size_t const interval_;
    size_t const filter_cache_bytes_;
    size_t const hash_cache_entries_;

    mutable std::mutex mutex_;
    // By filter type.
    std::array<lane, 3> lanes_;
    // Newest first.
    std::list<std::pair<hash_digest, script_tree_ptr>> script_trees_;
    std::atomic<bool> extending_{false};
};

// The requests, served in the peer's service, off its read loop. A request
// that cannot be answered is ignored, as other Bitcoin Cash nodes do: the
// wallet asks another peer.

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

/// getcfrange -> cfrange.
[[nodiscard]] KND_API
::asio::awaitable<message_result> handle_get_compact_filter_range(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filter_range const& request);

/// getcfproof -> cfproof.
[[nodiscard]] KND_API
::asio::awaitable<message_result> handle_get_compact_filter_proof(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filter_proof const& request);

} // namespace detail
} // namespace kth::node

#endif // KTH_NODE_DETAIL_COMPACT_FILTERS_HPP
