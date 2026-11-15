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
#include <kth/node/detail/scope_action.hpp>
#include <kth/node/p2p_node.hpp>

#include <spdlog/spdlog.h>

namespace kth::node::detail {

namespace {

using index_t = blockchain::header_index::index_t;
constexpr auto null_index = blockchain::header_index::null_index;
constexpr size_t record_size = 2 * hash_size;

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

compact_filter_service::filter_ptr built(domain::chain::block const& block) {
    auto const filter = domain::chain::build_regular_filter(block);
    if ( ! filter) return nullptr;
    return std::make_shared<data_chunk const>(filter->to_data());
}

} // namespace

compact_filter_service::compact_filter_service(blockchain::block_chain const& chain,
    domain::chain::block genesis, kth::path checkpoint_file, size_t interval,
    size_t filter_cache_bytes, size_t hash_cache_entries)
    : chain_(chain)
    , genesis_(std::move(genesis))
    , genesis_hash_(genesis_.hash())
    , checkpoint_file_(std::move(checkpoint_file))
    , interval_(std::max(size_t{1}, interval))
    , filter_cache_bytes_(filter_cache_bytes)
    , hash_cache_entries_(std::max(size_t{1}, hash_cache_entries))
{}

void compact_filter_service::load() {
    std::vector<compact_filter_checkpoint_record> records;
    std::ifstream file(checkpoint_file_, std::ios::binary);
    if (file) {
        data_chunk const bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bytes.size() % record_size != 0) {
            spdlog::warn("[compact filters] {} is not whole records; its checkpoints are computed again",
                checkpoint_file_.string());
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
    checkpoints_ = std::move(records);
}

void compact_filter_service::save() const {
    data_chunk bytes;
    {
        std::lock_guard lock(mutex_);
        bytes.reserve(checkpoints_.size() * record_size);
        for (auto const& record : checkpoints_) {
            bytes.insert(bytes.end(), record.block.begin(), record.block.end());
            bytes.insert(bytes.end(), record.filter_header.begin(), record.filter_header.end());
        }
    }
    // Written beside the file and moved over it, so a stop in the middle
    // leaves the old list whole.
    auto replacement = checkpoint_file_;
    replacement += ".new";
    {
        std::ofstream file(replacement, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<char const*>(bytes.data()), std::streamsize(bytes.size()));
        if ( ! file) {
            spdlog::warn("[compact filters] could not write {}", replacement.string());
            return;
        }
    }
    std::error_code error;
    std::filesystem::rename(replacement, checkpoint_file_, error);
    if (error) {
        spdlog::warn("[compact filters] could not replace {}: {}", checkpoint_file_.string(), error.message());
    }
}

std::vector<compact_filter_checkpoint_record> compact_filter_service::checkpoints() const {
    std::lock_guard lock(mutex_);
    return checkpoints_;
}

compact_filter_service::filter_ptr compact_filter_service::cached_filter(hash_digest const& block) {
    std::lock_guard lock(mutex_);
    auto const found = filter_by_block_.find(block);
    if (found == filter_by_block_.end()) return nullptr;
    filters_.splice(filters_.begin(), filters_, found->second);
    return found->second->second;
}

void compact_filter_service::keep_filter(hash_digest const& block, filter_ptr const& filter) {
    std::lock_guard lock(mutex_);
    // Two requests can build the same filter at once; the first is kept.
    if (filter_by_block_.contains(block)) return;
    // One larger than the whole budget is served and not kept.
    if (filter->size() > filter_cache_bytes_) return;
    filters_.emplace_front(block, filter);
    filter_by_block_.emplace(block, filters_.begin());
    filter_bytes_ += filter->size();
    while (filter_bytes_ > filter_cache_bytes_) {
        auto const& oldest = filters_.back();
        filter_bytes_ -= oldest.second->size();
        filter_by_block_.erase(oldest.first);
        filters_.pop_back();
    }
}

std::optional<hash_digest> compact_filter_service::cached_hash(hash_digest const& block) const {
    std::lock_guard lock(mutex_);
    auto const found = hash_by_block_.find(block);
    if (found == hash_by_block_.end()) return std::nullopt;
    return found->second;
}

void compact_filter_service::keep_hash(hash_digest const& block, hash_digest const& filter_hash) {
    std::lock_guard lock(mutex_);
    if ( ! hash_by_block_.emplace(block, filter_hash).second) return;
    hash_order_.push_back(block);
    while (hash_order_.size() > hash_cache_entries_) {
        hash_by_block_.erase(hash_order_.front());
        hash_order_.pop_front();
    }
}

awaitable_expected<compact_filter_service::filter_ptr>
compact_filter_service::filter_of(hash_digest const& block) {
    if (auto cached = cached_filter(block)) co_return cached;
    filter_ptr filter;
    if (block == genesis_hash_) {
        // The node holds no stored record of the genesis block.
        filter = built(genesis_);
    } else {
        auto const fetched = co_await chain_.fetch_block(block);
        if ( ! fetched) co_return std::unexpected(fetched.error());
        filter = built(*fetched->first);
    }
    if ( ! filter) co_return std::unexpected(error::operation_failed);
    keep_filter(block, filter);
    keep_hash(block, bitcoin_hash(*filter));
    co_return filter;
}

awaitable_expected<hash_digest> compact_filter_service::filter_hash_of(hash_digest const& block) {
    if (auto const cached = cached_hash(block)) co_return *cached;
    auto const filter = co_await filter_of(block);
    if ( ! filter) co_return std::unexpected(filter.error());
    co_return bitcoin_hash(**filter);
}

::asio::awaitable<void> compact_filter_service::extend(std::function<bool()> stopped) {
    if (extending_.exchange(true)) co_return;
    scope_action const done([this]() noexcept { extending_ = false; }, true);

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
            while ( ! checkpoints_.empty()) {
                auto const height = checkpoints_.size() * interval_;
                if (height <= size_t(chain->height) && chain->hash_at(height) == checkpoints_.back().block) break;
                checkpoints_.pop_back();
                dropped = true;
            }
            held = checkpoints_.size();
            if (held != 0) header = checkpoints_.back().filter_header;
        }
        if (dropped) save();

        auto const target = (held + 1) * interval_;
        if (target > size_t(chain->height)) co_return;

        // From the block after the last record, or from the first block: the
        // filter header before it is all zeros.
        for (auto height = held == 0 ? size_t{0} : held * interval_ + 1; height <= target; ++height) {
            if (stopped && stopped()) co_return;
            auto const filter_hash = co_await filter_hash_of(chain->hash_at(height));
            if ( ! filter_hash) {
                spdlog::warn("[compact filters] no filter for the block at height {}: {}; checkpoints stop at {}",
                    height, filter_hash.error().message(), held * interval_);
                co_return;
            }
            header = chained(*filter_hash, header);
        }
        {
            std::lock_guard lock(mutex_);
            checkpoints_.push_back({chain->hash_at(target), header});
        }
        save();
        if (target % (interval_ * 50) == 0) {
            spdlog::info("[compact filters] filter headers computed up to height {}", target);
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
    if (request.filter_type != domain::message::regular_compact_filter) co_return std::nullopt;
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
            if (checkpoints_.size() < records
                || checkpoints_[records - 1].block != chain->hash_at(records * interval_)) {
                co_return std::nullopt;
            }
            header = checkpoints_[records - 1].filter_header;
            next = records * interval_ + 1;
        }
        for (auto height = next; height <= before; ++height) {
            auto const filter_hash = co_await filter_hash_of(chain->hash_at(height));
            if ( ! filter_hash) co_return std::nullopt;
            header = chained(*filter_hash, header);
        }
        answer.previous_filter_header = header;
    }

    answer.filter_hashes.reserve(blocks->size());
    for (auto const& block : *blocks) {
        auto const filter_hash = co_await filter_hash_of(block);
        if ( ! filter_hash) co_return std::nullopt;
        answer.filter_hashes.push_back(*filter_hash);
    }
    co_return answer;
}

std::optional<domain::message::compact_filter_checkpoint>
compact_filter_service::checkpoint(domain::message::get_compact_filter_checkpoint const& request) const {
    if (request.filter_type != domain::message::regular_compact_filter) return std::nullopt;
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
    if (checkpoints_.size() < count || checkpoints_[count - 1].block != chain->hash_at(count * interval_)) {
        return std::nullopt;
    }
    answer.filter_headers.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        answer.filter_headers.push_back(checkpoints_[i].filter_header);
    }
    return answer;
}

::asio::awaitable<message_result> handle_get_compact_filters(compact_filter_service& service,
    network::peer_session& peer, domain::message::get_compact_filters const& request) {
    if (request.filter_type != domain::message::regular_compact_filter) {
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
        auto const filter = co_await service.filter_of(block);
        if ( ! filter) {
            spdlog::debug("[compact filters] no filter for block {} asked by [{}]: {}",
                encode_hash(block), peer.authority(), filter.error().message());
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

} // namespace kth::node::detail
