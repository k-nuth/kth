// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/compact_filters.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <kth/blockchain/header_index.hpp>
#include <kth/domain/chain/compact_filter.hpp>
#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/node/detail/scope_action.hpp>
#include <kth/node/p2p_node.hpp>

#include <spdlog/spdlog.h>

namespace kth::node::detail {

namespace {

using index_t = blockchain::header_index::index_t;
constexpr auto null_index = blockchain::header_index::null_index;
constexpr size_t record_size = 2 * hash_size;

using domain::message::regular_compact_filter;
using domain::message::script_compact_filter;

// A chain as seen from its last block: an entry is on it when the last
// block's ancestor at the entry's height is the entry itself. Entries and
// their parent links never change once published, so this stays true while a
// request is answered, whatever the node connects meanwhile.
struct chain_to {
    blockchain::header_index const& index;
    index_t last;
    int32_t height;

    [[nodiscard]]
    bool contains(index_t idx) const {
        if (idx == null_index) return false;
        auto const at = index.get_height(idx);
        return at >= 0 && at <= height && index.get_ancestor(last, at) == idx;
    }

    [[nodiscard]]
    hash_digest hash_at(size_t at) const {
        return index.get_hash(index.get_ancestor(last, int32_t(at)));
    }
};

std::optional<chain_to> connected_chain(blockchain::block_chain const& chain) {
    auto const view = chain.chain_view();
    if ( ! view) return std::nullopt;
    auto const& index = chain.headers();
    auto const tip = index.find(view->tip_hash);
    if (tip == null_index) return std::nullopt;
    return chain_to{index, tip, index.get_height(tip)};
}

// The connected chain up to `stop`, when `stop` is on it.
std::optional<chain_to> connected_chain_to(blockchain::block_chain const& chain, hash_digest const& stop) {
    auto const connected = connected_chain(chain);
    if ( ! connected) return std::nullopt;
    auto const idx = connected->index.find(stop);
    if ( ! connected->contains(idx)) return std::nullopt;
    return chain_to{connected->index, idx, connected->index.get_height(idx)};
}

// A filter header: the double SHA-256 of the filter hash and the filter
// header of the block before.
hash_digest chained(hash_digest const& filter_hash, hash_digest const& previous) {
    return bitcoin_hash(splice(filter_hash, previous));
}

compact_filter_service::filter_ptr regular_filter(domain::chain::block const& block) {
    auto const filter = domain::chain::build_regular_filter(block);
    if ( ! filter) return nullptr;
    return std::make_shared<data_chunk const>(filter->to_data());
}

// As it is served: the root of the entry tree, then the set.
compact_filter_service::filter_ptr script_filter(domain::chain::block const& block,
    std::span<domain::chain::output const> spent) {
    auto const filter = domain::chain::build_script_filter(block, spent);
    if ( ! filter) return nullptr;
    return std::make_shared<data_chunk const>(domain::chain::to_data(*filter));
}

char const* name_of(uint8_t filter_type) {
    return filter_type == script_compact_filter ? "script filters" : "compact filters";
}

constexpr auto wire_version = domain::message::version::level::maximum;

} // namespace

compact_filter_service::compact_filter_service(blockchain::block_chain const& chain,
    domain::chain::block genesis, kth::path checkpoint_file, size_t interval,
    size_t filter_cache_bytes, size_t hash_cache_entries)
    : chain_(chain)
    , genesis_(std::move(genesis))
    , genesis_hash_(genesis_.hash())
    , interval_(std::max(size_t{1}, interval))
    , filter_cache_bytes_(filter_cache_bytes)
    , hash_cache_entries_(std::max(size_t{1}, hash_cache_entries))
{
    lanes_[regular_compact_filter].type = regular_compact_filter;
    lanes_[regular_compact_filter].served = true;
    lanes_[regular_compact_filter].file = std::move(checkpoint_file);
    lanes_[script_compact_filter].type = script_compact_filter;
}

void compact_filter_service::serve_script_filters(kth::path file) {
    lanes_[script_compact_filter].served = true;
    lanes_[script_compact_filter].file = std::move(file);
}

compact_filter_service::lane* compact_filter_service::lane_of(uint8_t filter_type) noexcept {
    if (filter_type >= lanes_.size() || ! lanes_[filter_type].served) return nullptr;
    return &lanes_[filter_type];
}

compact_filter_service::lane const* compact_filter_service::lane_of(uint8_t filter_type) const noexcept {
    if (filter_type >= lanes_.size() || ! lanes_[filter_type].served) return nullptr;
    return &lanes_[filter_type];
}

bool compact_filter_service::serves(uint8_t filter_type) const noexcept {
    return lane_of(filter_type) != nullptr;
}

void compact_filter_service::load() {
    for (auto& of : lanes_) {
        if (of.served) load_lane(of);
    }
}

void compact_filter_service::load_lane(lane& of) {
    std::vector<compact_filter_checkpoint_record> records;
    std::ifstream file(of.file, std::ios::binary);
    if (file) {
        data_chunk const bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bytes.size() % record_size != 0) {
            spdlog::warn("[{}] {} is not whole records; its checkpoints are computed again",
                name_of(of.type), of.file.string());
        } else {
            records.resize(bytes.size() / record_size);
            for (size_t i = 0; i < records.size(); ++i) {
                auto const at = bytes.begin() + std::ptrdiff_t(i * record_size);
                std::copy_n(at, hash_size, records[i].block.begin());
                std::copy_n(at + hash_size, hash_size, records[i].filter_header.begin());
            }
        }
    }
    std::lock_guard lock(mutex_);
    of.checkpoints = std::move(records);
}

void compact_filter_service::save_lane(lane const& of) const {
    data_chunk bytes;
    {
        std::lock_guard lock(mutex_);
        bytes.reserve(of.checkpoints.size() * record_size);
        for (auto const& record : of.checkpoints) {
            bytes.insert(bytes.end(), record.block.begin(), record.block.end());
            bytes.insert(bytes.end(), record.filter_header.begin(), record.filter_header.end());
        }
    }
    // Written beside the file and moved over it, so a stop in the middle
    // leaves the old list whole.
    auto replacement = of.file;
    replacement += ".new";
    {
        std::ofstream file(replacement, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<char const*>(bytes.data()), std::streamsize(bytes.size()));
        if ( ! file) {
            spdlog::warn("[{}] could not write {}", name_of(of.type), replacement.string());
            return;
        }
    }
    std::error_code error;
    std::filesystem::rename(replacement, of.file, error);
    if (error) {
        spdlog::warn("[{}] could not replace {}: {}", name_of(of.type), of.file.string(), error.message());
    }
}

std::vector<compact_filter_checkpoint_record> compact_filter_service::checkpoints(uint8_t filter_type) const {
    auto const* of = lane_of(filter_type);
    if (of == nullptr) return {};
    std::lock_guard lock(mutex_);
    return of->checkpoints;
}

compact_filter_service::filter_ptr compact_filter_service::cached_filter(lane& of, hash_digest const& block) {
    std::lock_guard lock(mutex_);
    auto const found = of.filter_by_block.find(block);
    if (found == of.filter_by_block.end()) return nullptr;
    of.filters.splice(of.filters.begin(), of.filters, found->second);
    return found->second->second;
}

void compact_filter_service::keep_filter(lane& of, hash_digest const& block, filter_ptr const& filter) {
    std::lock_guard lock(mutex_);
    // Two requests can build the same filter at once; the first is kept.
    if (of.filter_by_block.contains(block)) return;
    // One larger than the whole budget is served and not kept.
    if (filter->size() > filter_cache_bytes_) return;
    of.filters.emplace_front(block, filter);
    of.filter_by_block.emplace(block, of.filters.begin());
    of.filter_bytes += filter->size();
    while (of.filter_bytes > filter_cache_bytes_) {
        auto const& oldest = of.filters.back();
        of.filter_bytes -= oldest.second->size();
        of.filter_by_block.erase(oldest.first);
        of.filters.pop_back();
    }
}

std::optional<hash_digest> compact_filter_service::cached_hash(lane const& of, hash_digest const& block) const {
    std::lock_guard lock(mutex_);
    auto const found = of.hash_by_block.find(block);
    if (found == of.hash_by_block.end()) return std::nullopt;
    return found->second;
}

void compact_filter_service::keep_hash(lane& of, hash_digest const& block, hash_digest const& filter_hash) {
    std::lock_guard lock(mutex_);
    if ( ! of.hash_by_block.emplace(block, filter_hash).second) return;
    of.hash_order.push_back(block);
    while (of.hash_order.size() > hash_cache_entries_) {
        of.hash_by_block.erase(of.hash_order.front());
        of.hash_order.pop_front();
    }
}

awaitable_expected<compact_filter_service::script_source_ptr>
compact_filter_service::script_source_of(hash_digest const& block) {
    if (block == genesis_hash_) {
        // The node holds no stored record of the genesis block, and the block
        // spends nothing.
        co_return std::make_shared<script_source const>(script_source{
            std::make_shared<domain::chain::block const>(genesis_), {}});
    }
    // The block from the block files, and the outputs it spends from the undo
    // record the node wrote when it connected the block. not_found when the
    // block was connected without one and spends outputs.
    auto fetched = co_await chain_.fetch_block(block);
    if ( ! fetched) co_return std::unexpected(fetched.error());
    auto spent = co_await chain_.fetch_spent_outputs(block, fetched->first);
    if ( ! spent) co_return std::unexpected(spent.error());
    co_return std::make_shared<script_source const>(script_source{fetched->first, std::move(*spent)});
}

awaitable_expected<compact_filter_service::filter_ptr>
compact_filter_service::build(uint8_t filter_type, hash_digest const& block) {
    filter_ptr filter;
    if (filter_type == script_compact_filter) {
        auto const source = co_await script_source_of(block);
        if ( ! source) co_return std::unexpected(source.error());
        filter = script_filter(*(*source)->block, (*source)->spent.outputs);
    } else if (block == genesis_hash_) {
        // The node holds no stored record of the genesis block.
        filter = regular_filter(genesis_);
    } else {
        auto const fetched = co_await chain_.fetch_block(block);
        if ( ! fetched) co_return std::unexpected(fetched.error());
        filter = regular_filter(*fetched->first);
    }
    if ( ! filter) co_return std::unexpected(error::operation_failed);
    co_return filter;
}

awaitable_expected<compact_filter_service::filter_ptr>
compact_filter_service::filter_of(hash_digest const& block, uint8_t filter_type) {
    auto* const of = lane_of(filter_type);
    if (of == nullptr) co_return std::unexpected(error::not_found);
    if (auto cached = cached_filter(*of, block)) co_return cached;
    auto const filter = co_await build(filter_type, block);
    if ( ! filter) co_return std::unexpected(filter.error());
    keep_filter(*of, block, *filter);
    keep_hash(*of, block, bitcoin_hash(**filter));
    co_return *filter;
}

awaitable_expected<hash_digest> compact_filter_service::filter_hash_of(lane& of, hash_digest const& block) {
    if (auto const cached = cached_hash(of, block)) co_return *cached;
    auto const filter = co_await filter_of(block, of.type);
    if ( ! filter) co_return std::unexpected(filter.error());
    co_return bitcoin_hash(**filter);
}

awaitable_expected<compact_filter_service::script_tree_ptr>
compact_filter_service::script_tree_of(hash_digest const& block) {
    {
        std::lock_guard lock(mutex_);
        for (auto it = script_trees_.begin(); it != script_trees_.end(); ++it) {
            if (it->first != block) continue;
            script_trees_.splice(script_trees_.begin(), script_trees_, it);
            co_return script_trees_.front().second;
        }
    }
    auto const source = co_await script_source_of(block);
    if ( ! source) co_return std::unexpected(source.error());
    auto leaves = domain::chain::script_leaves(*(*source)->block, (*source)->spent.outputs);
    if ( ! leaves) co_return std::unexpected(error::operation_failed);

    auto tree = std::make_shared<script_tree>();
    tree->source = *source;
    tree->leaves = std::move(*leaves);
    auto const& transactions = tree->source->block->transactions();
    tree->transaction_hashes.reserve(transactions.size());
    for (auto const& tx : transactions) {
        tree->transaction_hashes.push_back(tx.hash());
    }

    std::lock_guard lock(mutex_);
    script_trees_.emplace_front(block, tree);
    while (script_trees_.size() > script_tree_cache_entries) {
        script_trees_.pop_back();
    }
    co_return tree;
}

::asio::awaitable<void> compact_filter_service::extend(std::function<bool()> stopped) {
    if (extending_.exchange(true)) co_return;
    scope_action const done([this]() noexcept { extending_ = false; }, true);
    for (auto& of : lanes_) {
        if (of.served) co_await extend_lane(of, stopped);
    }
}

::asio::awaitable<void> compact_filter_service::extend_lane(lane& of, std::function<bool()> const& stopped) {
    for (;;) {
        if (stopped && stopped()) co_return;
        auto const chain = connected_chain(chain_);
        if ( ! chain) co_return;

        // The list is one chain's: when its last record is on the connected
        // chain, every record before it is. What a reorganization left behind
        // is dropped from the end.
        bool dropped = false;
        size_t held = 0;
        hash_digest header = null_hash;
        {
            std::lock_guard lock(mutex_);
            // A block no filter can be built for stays so: nothing to do
            // while it is on the connected chain.
            if (of.stalled) {
                if (of.stalled->first <= size_t(chain->height)
                    && chain->hash_at(of.stalled->first) == of.stalled->second) {
                    co_return;
                }
                of.stalled.reset();
            }
            while ( ! of.checkpoints.empty()) {
                auto const height = of.checkpoints.size() * interval_;
                if (height <= size_t(chain->height) && chain->hash_at(height) == of.checkpoints.back().block) break;
                of.checkpoints.pop_back();
                dropped = true;
            }
            held = of.checkpoints.size();
            if (held != 0) header = of.checkpoints.back().filter_header;
        }
        if (dropped) save_lane(of);

        auto const target = (held + 1) * interval_;
        if (target > size_t(chain->height)) co_return;

        // From the block after the last record, or from the first block: the
        // filter header before it is all zeros.
        for (auto height = held == 0 ? size_t{0} : held * interval_ + 1; height <= target; ++height) {
            if (stopped && stopped()) co_return;
            auto const block = chain->hash_at(height);
            auto const filter_hash = co_await filter_hash_of(of, block);
            if ( ! filter_hash) {
                if (of.type == script_compact_filter && filter_hash.error() == error::not_found) {
                    // The block was connected without a record of what it
                    // spent: at or below the checkpoint of a node that does
                    // not validate from the first block.
                    spdlog::info("[script filters] the block at height {} was connected without a record of the "
                        "outputs it spends, so script filter headers stop at {}; a node synced with "
                        "chain.full_validation has them from the first block", height, held * interval_);
                } else {
                    spdlog::warn("[{}] no filter for the block at height {}: {}; checkpoints stop at {}",
                        name_of(of.type), height, filter_hash.error().message(), held * interval_);
                }
                std::lock_guard lock(mutex_);
                of.stalled.emplace(height, block);
                co_return;
            }
            header = chained(*filter_hash, header);
        }
        {
            std::lock_guard lock(mutex_);
            of.checkpoints.push_back({chain->hash_at(target), header});
        }
        save_lane(of);
        if (target % (interval_ * 50) == 0) {
            spdlog::info("[{}] filter headers computed up to height {}", name_of(of.type), target);
        }
    }
}

std::optional<hash_list> compact_filter_service::blocks_of(uint32_t start_height,
    hash_digest const& stop, size_t limit) const {
    auto const chain = connected_chain_to(chain_, stop);
    if ( ! chain || uint64_t(start_height) > uint64_t(chain->height)) return std::nullopt;
    auto const count = size_t(chain->height) - start_height + 1;
    if (count > limit) return std::nullopt;
    // Walked down from the stop block through parent links, then in order.
    hash_list blocks(count);
    auto idx = chain->last;
    for (auto at = count; at > 0; --at) {
        blocks[at - 1] = chain->index.get_hash(idx);
        idx = chain->index.get_parent_index(idx);
    }
    return blocks;
}

::asio::awaitable<std::optional<domain::message::compact_filter_headers>>
compact_filter_service::headers(domain::message::get_compact_filter_headers const& request) {
    auto* const of = lane_of(request.filter_type);
    if (of == nullptr) co_return std::nullopt;
    auto const blocks = blocks_of(request.start_height, request.stop_hash,
        domain::message::max_compact_filter_headers);
    if ( ! blocks) co_return std::nullopt;
    auto const chain = connected_chain_to(chain_, request.stop_hash);
    if ( ! chain) co_return std::nullopt;

    domain::message::compact_filter_headers answer;
    answer.filter_type = request.filter_type;
    answer.stop_hash = request.stop_hash;

    // The filter header of the block before the first one asked for: chained
    // from the record at or below it, or from the first block.
    if (request.start_height != 0) {
        auto const before = size_t(request.start_height) - 1;
        auto const records = before / interval_;
        size_t next = 0;
        hash_digest header = null_hash;
        if (records != 0) {
            std::lock_guard lock(mutex_);
            if (of->checkpoints.size() < records
                || of->checkpoints[records - 1].block != chain->hash_at(records * interval_)) {
                co_return std::nullopt;
            }
            header = of->checkpoints[records - 1].filter_header;
            next = records * interval_ + 1;
        }
        for (auto height = next; height <= before; ++height) {
            auto const filter_hash = co_await filter_hash_of(*of, chain->hash_at(height));
            if ( ! filter_hash) co_return std::nullopt;
            header = chained(*filter_hash, header);
        }
        answer.previous_filter_header = header;
    }

    answer.filter_hashes.reserve(blocks->size());
    for (auto const& block : *blocks) {
        auto const filter_hash = co_await filter_hash_of(*of, block);
        if ( ! filter_hash) co_return std::nullopt;
        answer.filter_hashes.push_back(*filter_hash);
    }
    co_return answer;
}

std::optional<domain::message::compact_filter_checkpoint>
compact_filter_service::checkpoint(domain::message::get_compact_filter_checkpoint const& request) const {
    auto const* of = lane_of(request.filter_type);
    if (of == nullptr) return std::nullopt;
    auto const chain = connected_chain_to(chain_, request.stop_hash);
    if ( ! chain) return std::nullopt;
    auto const count = size_t(chain->height) / interval_;
    if (count > domain::message::max_compact_filter_checkpoints) return std::nullopt;

    domain::message::compact_filter_checkpoint answer;
    answer.filter_type = request.filter_type;
    answer.stop_hash = request.stop_hash;
    if (count == 0) return answer;

    std::lock_guard lock(mutex_);
    // Not computed that far yet, or computed on a branch the stop block is not on.
    if (of->checkpoints.size() < count || of->checkpoints[count - 1].block != chain->hash_at(count * interval_)) {
        return std::nullopt;
    }
    answer.filter_headers.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        answer.filter_headers.push_back(of->checkpoints[i].filter_header);
    }
    return answer;
}

::asio::awaitable<std::optional<domain::message::compact_filter_range>>
compact_filter_service::range(domain::message::get_compact_filter_range const& request) {
    // Only the script filter has an entry tree.
    if (request.filter_type != script_compact_filter || ! serves(script_compact_filter)) co_return std::nullopt;
    if ( ! connected_chain_to(chain_, request.block_hash)) co_return std::nullopt;
    auto const tree = co_await script_tree_of(request.block_hash);
    if ( ! tree) co_return std::nullopt;
    auto const& leaves = (*tree)->leaves;
    // A prefix longer than a key, or with a bit set past its length.
    auto const run = domain::chain::script_leaves_with_prefix(leaves, request.prefix, request.prefix_bits);
    if ( ! run) co_return std::nullopt;

    domain::message::compact_filter_range answer;
    answer.filter_type = request.filter_type;
    answer.block_hash = request.block_hash;
    answer.leaf_count = uint32_t(leaves.size());
    answer.first_index = uint32_t(run->first);
    answer.leaves.assign(leaves.begin() + std::ptrdiff_t(run->first), leaves.begin() + std::ptrdiff_t(run->end));

    // The run that rebuilds the root reaches one leaf past each end, so the
    // wallet sees that the leaves next to the answer do not have the prefix.
    auto lo = run->first;
    auto hi = run->end;
    if (run->first > 0) {
        answer.before = leaves[run->first - 1];
        --lo;
    }
    if (run->end < leaves.size()) {
        answer.after = leaves[run->end];
        ++hi;
    }
    if (lo < hi) {
        auto hashes = domain::chain::script_range_hashes(leaves, lo, hi);
        if ( ! hashes) co_return std::nullopt;
        answer.hashes = std::move(*hashes);
    }
    // More leaves than fit one message: a longer prefix splits them.
    if (answer.serialized_size(wire_version) > max_payload_size) co_return std::nullopt;
    co_return answer;
}

::asio::awaitable<std::optional<domain::message::compact_filter_proof>>
compact_filter_service::proof(domain::message::get_compact_filter_proof const& request) {
    if (request.filter_type != script_compact_filter || ! serves(script_compact_filter)) co_return std::nullopt;
    auto const chain = connected_chain_to(chain_, request.block_hash);
    if ( ! chain) co_return std::nullopt;
    auto const found = co_await script_tree_of(request.block_hash);
    if ( ! found) co_return std::nullopt;
    auto const& tree = **found;
    auto const& leaves = tree.leaves;
    if (size_t(request.leaf_index) >= leaves.size()) co_return std::nullopt;
    auto const& leaf = leaves[request.leaf_index];
    auto const& block = *tree.source->block;
    auto const& transactions = block.transactions();
    if (size_t(leaf.tx_index) >= transactions.size()) co_return std::nullopt;

    domain::message::compact_filter_proof answer;
    answer.filter_type = request.filter_type;
    answer.block_hash = request.block_hash;
    answer.leaf_count = uint32_t(leaves.size());
    answer.leaf_index = request.leaf_index;
    answer.leaf = leaf;
    auto branch = domain::chain::script_range_hashes(leaves, request.leaf_index, size_t(request.leaf_index) + 1);
    if ( ! branch) co_return std::nullopt;
    answer.leaf_branch = std::move(*branch);
    answer.transaction = transactions[leaf.tx_index];
    answer.transaction_branch = block.transaction_merkle_branch(leaf.tx_index, tree.transaction_hashes);

    if ( ! domain::message::detail::is_spent_leaf(leaf)) co_return answer;

    // The leaf names an input. The outputs the block spends are listed in the
    // order of its inputs, the coinbase left out.
    auto const& spending = transactions[leaf.tx_index];
    if (leaf.tx_index == 0 || size_t(leaf.index) >= spending.inputs().size()) co_return std::nullopt;
    auto position = size_t(leaf.index);
    for (size_t t = 1; t < leaf.tx_index; ++t) {
        position += transactions[t].inputs().size();
    }
    auto const& heights = tree.source->spent.heights;
    if (position >= heights.size()) co_return std::nullopt;
    auto const created_at = size_t(heights[position]);
    if (created_at > size_t(chain->height)) co_return std::nullopt;

    // The block that output was created in: this one, or one below it on the
    // same chain.
    domain::message::compact_filter_proof_origin origin;
    std::shared_ptr<domain::chain::block const> creating;
    hash_list created_hashes;
    if (created_at == size_t(chain->height)) {
        origin.block_hash = request.block_hash;
        creating = tree.source->block;
        created_hashes = tree.transaction_hashes;
    } else {
        origin.block_hash = chain->hash_at(created_at);
        auto const fetched = co_await chain_.fetch_block(origin.block_hash);
        if ( ! fetched) co_return std::nullopt;
        creating = fetched->first;
        created_hashes.reserve(creating->transactions().size());
        for (auto const& tx : creating->transactions()) {
            created_hashes.push_back(tx.hash());
        }
    }
    auto const& wanted = spending.inputs()[leaf.index].previous_output().hash();
    auto const at = std::find(created_hashes.begin(), created_hashes.end(), wanted);
    if (at == created_hashes.end()) {
        spdlog::error("[script filters] no transaction {} in block {} at height {}",
            encode_hash(wanted), encode_hash(origin.block_hash), created_at);
        co_return std::nullopt;
    }
    auto const created_index = size_t(at - created_hashes.begin());
    origin.transaction_index = uint32_t(created_index);
    origin.transaction = creating->transactions()[created_index];
    origin.transaction_branch = creating->transaction_merkle_branch(created_index, created_hashes);
    answer.origin = std::move(origin);
    co_return answer;
}

::asio::awaitable<message_result> handle_get_compact_filters(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filters const& request) {
    if ( ! service.serves(request.filter_type)) {
        spdlog::debug("[compact filters] getcfilters for filter type {} from [{}]; not answered",
            request.filter_type, peer.authority());
        co_return message_result::handled;
    }
    auto const blocks = service.blocks_of(request.start_height, request.stop_hash,
        domain::message::max_compact_filters_per_request);
    if ( ! blocks) {
        spdlog::debug("[compact filters] getcfilters from [{}] names no range of the connected chain",
            peer.authority());
        co_return message_result::handled;
    }
    for (auto const& block : *blocks) {
        auto const filter = co_await service.filter_of(block, request.filter_type);
        if ( ! filter) {
            spdlog::debug("[compact filters] no filter of type {} for block {} asked by [{}]: {}",
                request.filter_type, encode_hash(block), peer.authority(), filter.error().message());
            co_return message_result::handled;
        }
        domain::message::compact_filter const message{request.filter_type, block, **filter};
        if (co_await peer.send_data(message)) {
            // The peer is gone; nothing more can be sent to it.
            co_return message_result::handled;
        }
    }
    co_return message_result::handled;
}

::asio::awaitable<message_result> handle_get_compact_filter_headers(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filter_headers const& request) {
    auto const answer = co_await service.headers(request);
    if ( ! answer) {
        spdlog::debug("[compact filters] getcfheaders from [{}] not answered", peer.authority());
        co_return message_result::handled;
    }
    (void)co_await peer.send_data(*answer);
    co_return message_result::handled;
}

::asio::awaitable<message_result> handle_get_compact_filter_checkpoint(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filter_checkpoint const& request) {
    auto const answer = service.checkpoint(request);
    if ( ! answer) {
        spdlog::debug("[compact filters] getcfcheckpt from [{}] not answered", peer.authority());
        co_return message_result::handled;
    }
    (void)co_await peer.send_data(*answer);
    co_return message_result::handled;
}

::asio::awaitable<message_result> handle_get_compact_filter_range(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filter_range const& request) {
    auto const answer = co_await service.range(request);
    if ( ! answer) {
        spdlog::debug("[script filters] getcfrange from [{}] not answered", peer.authority());
        co_return message_result::handled;
    }
    (void)co_await peer.send_data(*answer);
    co_return message_result::handled;
}

::asio::awaitable<message_result> handle_get_compact_filter_proof(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filter_proof const& request) {
    auto const answer = co_await service.proof(request);
    if ( ! answer) {
        spdlog::debug("[script filters] getcfproof from [{}] not answered", peer.authority());
        co_return message_result::handled;
    }
    (void)co_await peer.send_data(*answer);
    co_return message_result::handled;
}

} // namespace kth::node::detail
