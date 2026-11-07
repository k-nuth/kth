// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/header_index.hpp>

#include <cstring>
#include <bit>
#include <kth/domain/chain/block_ticks.hpp>
#include <kth/domain/chain/faster_blocks.hpp>
#include <algorithm>
#include <functional>

#include <spdlog/spdlog.h>

#include <kth/domain/chain/header.hpp>
#include <kth/infrastructure/utility/stats.hpp>

namespace kth::database {

// =============================================================================
// Construction
// =============================================================================

header_index::header_index(size_t capacity)
    : capacity_(capacity)
    , warn_threshold_((capacity * 95) / 100)
{
    // Pre-allocate all vectors to capacity
    hashes_.resize(capacity_);
    prev_block_hashes_.resize(capacity_);
    parent_indices_.resize(capacity_);
    skip_indices_.resize(capacity_);
    heights_.resize(capacity_);
    chain_works_.resize(capacity_);
    statuses_.resize(capacity_);
    versions_.resize(capacity_);
    merkle_roots_.resize(capacity_);
    timestamps_.resize(capacity_);
    bits_.resize(capacity_);
    nonces_.resize(capacity_);

    // Block file location (initialized to "no data")
    block_positions_ = std::make_unique<std::atomic<uint64_t>[]>(capacity_);
    undo_positions_.resize(capacity_, 0);

    // Reception time (0 = loaded from store / not yet set)
    header_received_times_.resize(capacity_, 0);

    // Active chain (height -> index), empty until headers are linked in
    active_.resize(capacity_, null_index);

    // Reserve hash map capacity
    hash_to_idx_.reserve(capacity_);
}

// =============================================================================
// Core Operations
// =============================================================================

header_index_result header_index::add(hash_digest const& hash, domain::chain::header const& header) {
    header_index_result result;

    // IMPORTANT: Find parent BEFORE insert_and_visit to avoid nested visits
    // (nested visits on same bucket = deadlock!)
    index_t parent_idx = null_index;
    int32_t height = 0;
    uint256_t chain_work{};

    KTH_STATS_TIME_START(find_parent);
    hash_to_idx_.visit(header.previous_block_hash(), [&](auto const& parent_pair) {
        parent_idx = parent_pair.second;
    });
    KTH_STATS_TIME_END(global_sync_stats(), find_parent, find_parent_time_ns, find_parent_calls);

    if (parent_idx != null_index) {
        height = heights_[parent_idx] + 1;
        // chainwork = sum of per-block work (2^256 / (target+1)) along the chain
        chain_work = chain_works_[parent_idx] + domain::chain::header::proof(header.bits());
    }

    // Build skip pointer BEFORE acquiring insert lock (O(log n) operation)
    KTH_STATS_TIME_START(build_skip);
    index_t const skip_idx = build_skip(parent_idx, height);
    KTH_STATS_TIME_END(global_sync_stats(), build_skip, build_skip_time_ns, build_skip_calls);

    // Now do the insert with all info already computed
    KTH_STATS_TIME_START(insert);
    hash_to_idx_.insert_and_visit(
        {hash, null_index},
        [&](auto& pair) {
            // Newly inserted - initialize everything
            index_t const idx = next_idx_.fetch_add(1, std::memory_order_relaxed);

            // Bounds check
            if (idx >= capacity_) {
                result.index = null_index;
                return;
            }

            // Store in vectors (all writes happen under bucket lock)
            hashes_[idx] = hash;
            prev_block_hashes_[idx] = header.previous_block_hash();
            parent_indices_[idx] = parent_idx;
            skip_indices_[idx] = skip_idx;
            heights_[idx] = height;
            chain_works_[idx] = chain_work;
            statuses_[idx] = 0;
            versions_[idx] = header.version();
            merkle_roots_[idx] = header.merkle();
            timestamps_[idx] = header.timestamp();
            bits_[idx] = header.bits();
            nonces_[idx] = header.nonce();

            // Update the map value with actual index
            pair.second = idx;
            result.index = idx;
            result.inserted = true;
        },
        [&](auto const& pair) {
            // Already existed - just get the index
            result.index = pair.second;
        }
    );
    KTH_STATS_TIME_END(global_sync_stats(), insert, insert_time_ns, insert_calls);

    result.capacity_warning = (result.index != null_index && result.index >= warn_threshold_);

    return result;
}

header_index::index_t header_index::find(hash_digest const& hash) const {
    index_t result = null_index;
    hash_to_idx_.visit(hash, [&](auto const& pair) {
        result = pair.second;
    });
    return result;
}

bool header_index::contains(hash_digest const& hash) const {
    return find(hash) != null_index;
}

// =============================================================================
// Field Accessors
// =============================================================================

hash_digest header_index::get_prev_block_hash(index_t idx) const {
    return prev_block_hashes_[idx];
}

header_index::index_t header_index::get_parent_index(index_t idx) const {
    return parent_indices_[idx];
}

header_index::index_t header_index::get_skip_index(index_t idx) const {
    return skip_indices_[idx];
}

int32_t header_index::get_height(index_t idx) const {
    return heights_[idx];
}

uint256_t header_index::get_chain_work(index_t idx) const {
    return chain_works_[idx];
}

std::optional<uint256_t> header_index::total_chain_work(hash_digest const& tip) const {
    auto const idx = find(tip);
    if (idx == null_index) {
        return std::nullopt;
    }
    auto const root = get_ancestor(idx, 0);
    if (root == null_index) {
        return std::nullopt;
    }
    return get_chain_work(idx) + domain::chain::header::proof(get_bits(root));
}

std::optional<std::vector<header_index::chain_tip>>
header_index::get_chain_tips(hash_digest const& connected_tip) const {
    return get_chain_tips(connected_tip, std::numeric_limits<uint64_t>::max());
}

std::optional<std::vector<header_index::chain_tip>>
header_index::get_chain_tips(hash_digest const& connected_tip, uint64_t activation_time) const {
    auto const active = find(connected_tip);
    if (active == null_index) return std::nullopt;

    // Index entries are append-only and a parent is published before its
    // children. Compute each branch's fork and data availability once, instead
    // of walking the same shared ancestry again for every leaf.
    auto const count = size();
    if (active >= count) return std::nullopt;
    // Eleven threshold bits reproduce the upper median without sorting or
    // walking shared ancestors again. Bit 11 records that this branch already
    // crossed activation; the containing block still adds its parent's interval.
    static_assert(domain::chain::faster_blocks_mtp_span < 16);
    constexpr uint16_t window_mask = (1u << domain::chain::faster_blocks_mtp_span) - 1;
    constexpr uint16_t activated_mask = 1u << domain::chain::faster_blocks_mtp_span;
    struct branch_state {
        uint64_t ticks = 0;
        int32_t fork_height = -1; // -1: not connected to the published chain
        uint16_t activation_window = 0;
        bool all_data = false;
        bool has_child = false;
    };
    std::vector<branch_state> branches(count);
    for (auto idx = active; idx != header_index::null_index;
         idx = get_parent_index(idx)) {
        branches[idx].fork_height = get_height(idx);
        branches[idx].all_data = true;
    }

    for (size_t i = 0; i < count; ++i) {
        auto const idx = header_index::index_t(i);
        auto const parent = get_parent_index(idx);
        if (parent != header_index::null_index && parent < count) {
            branches[parent].has_child = true;
        }
        auto& branch = branches[i];
        auto const parent_window = parent != null_index && parent < i
            ? branches[parent].activation_window : uint16_t{0};
        auto const parent_activated = (parent_window & activated_mask) != 0;
        if (parent != null_index && parent < i) {
            branch.ticks = branches[parent].ticks + (parent_activated
                ? domain::chain::faster_block_interval : domain::chain::base_block_interval);
        }
        auto const window = uint16_t(((parent_window << 1) |
            (uint64_t(get_timestamp(idx)) >= activation_time ? 1u : 0u)) & window_mask);
        auto const samples = std::min(size_t(get_height(idx)) + 1, domain::chain::faster_blocks_mtp_span);
        auto const activated = parent_activated || size_t(std::popcount(window)) >= (samples + 1) / 2;
        branch.activation_window = window | (activated ? activated_mask : 0);
        if (branch.fork_height >= 0) continue; // already on the connected chain
        if (parent == header_index::null_index || parent >= i) continue;
        auto const& previous = branches[parent];
        if (previous.fork_height < 0) continue; // a disconnected root
        branch.fork_height = previous.fork_height;
        branch.all_data = previous.all_data &&
            has_status(idx, header_status::have_data);
    }

    std::vector<chain_tip> tips;
    for (size_t i = 0; i < count; ++i) {
        auto const idx = header_index::index_t(i);
        if (idx == active) {
            tips.push_back({size_t(get_height(active)), connected_tip, 0, tip_status::active, branches[i].ticks});
            continue;
        }
        auto const& branch = branches[i];
        if (branch.has_child || branch.fork_height < 0) continue;
        auto const height = get_height(idx);
        tips.push_back({size_t(height), get_hash(idx),
            size_t(height - branch.fork_height),
            branch.all_data ? tip_status::valid_headers : tip_status::headers_only, branch.ticks});
    }
    // O(N + T log T) time, O(N + T) storage for N headers and T tips.
    std::ranges::stable_sort(tips, std::ranges::greater{}, &chain_tip::height);
    return tips;
}

header_status header_index::get_status(index_t idx) const {
    return header_status(statuses_[idx]);
}

uint32_t header_index::get_version(index_t idx) const {
    return versions_[idx];
}

hash_digest header_index::get_merkle(index_t idx) const {
    return merkle_roots_[idx];
}

uint32_t header_index::get_timestamp(index_t idx) const {
    return timestamps_[idx];
}

uint32_t header_index::get_bits(index_t idx) const {
    return bits_[idx];
}

uint32_t header_index::get_nonce(index_t idx) const {
    return nonces_[idx];
}

header_entry header_index::get_entry(index_t idx) const {
    auto const block_pos = get_block_pos(idx);
    return {
        get_header(idx),
        hashes_[idx],
        parent_indices_[idx],
        skip_indices_[idx],
        heights_[idx],
        chain_works_[idx],
        header_status(statuses_[idx]),
        int16_t(block_pos.file),
        block_pos.pos,
        undo_positions_[idx]
    };
}

hash_digest header_index::get_hash(index_t idx) const {
    return hashes_[idx];
}

domain::chain::header header_index::get_header(index_t idx) const {
    return {
        versions_[idx],
        prev_block_hashes_[idx],
        merkle_roots_[idx],
        timestamps_[idx],
        bits_[idx],
        nonces_[idx]
    };
}

// =============================================================================
// Status Management
// =============================================================================

void header_index::restore_failed_blocks(std::vector<hash_digest> hashes) {
    std::ranges::sort(hashes);
    hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
    restored_failures_ = std::move(hashes);
    if (restored_failures_.empty()) {
        return;
    }
    for (auto const& hash : restored_failures_) {
        auto const idx = find(hash);
        if (idx != null_index) {
            add_status(idx, header_status::failed_valid);
        }
    }
    // Parents are inserted before children. One forward pass marks even deep
    // descendants that were persisted before their ancestor's verdict committed.
    auto const count = index_t(size());
    for (index_t idx = 0; idx < count; ++idx) {
        auto const parent = get_parent_index(idx);
        if (parent != null_index &&
            (has_status(parent, header_status::failed_valid) ||
             has_status(parent, header_status::failed_child))) {
            add_status(idx, header_status::failed_child);
        }
    }
}

bool header_index::is_restored_failure(hash_digest const& hash) const {
    return std::binary_search(restored_failures_.begin(), restored_failures_.end(), hash);
}

void header_index::set_status(index_t idx, header_status status) {
    statuses_[idx] = uint32_t(status);
}

void header_index::add_status(index_t idx, header_status flags) {
    statuses_[idx] |= uint32_t(flags);
}

bool header_index::has_status(index_t idx, header_status flags) const {
    return (statuses_[idx] & uint32_t(flags)) == uint32_t(flags);
}

// =============================================================================
// Chain Work Management
// =============================================================================

void header_index::set_chain_work(index_t idx, uint256_t const& work) {
    chain_works_[idx] = work;
}

// =============================================================================
// Block File Location
// =============================================================================

namespace {

constexpr uint64_t pack_block_pos(int16_t file, uint32_t pos) {
    return file < 0 ? 0 : (uint64_t(file) + 1) << 32 | pos;
}

constexpr flat_file_pos unpack_block_pos(uint64_t packed) {
    return packed == 0 ? flat_file_pos{} : flat_file_pos{int32_t(packed >> 32) - 1, uint32_t(packed)};
}

} // namespace

void header_index::set_block_pos(index_t idx, int16_t file, uint32_t pos) {
    block_positions_[idx].store(pack_block_pos(file, pos), std::memory_order_release);
}

flat_file_pos header_index::get_block_pos(index_t idx) const {
    return unpack_block_pos(block_positions_[idx].load(std::memory_order_acquire));
}

void header_index::set_undo_pos(index_t idx, uint32_t pos) {
    undo_positions_[idx] = pos;
}

// =============================================================================
// Active chain
// =============================================================================

void header_index::active_push(index_t idx) {
    auto const size = active_size_.load(std::memory_order_relaxed);
    if (size_t(size) >= capacity_) {
        // Dropping silently would freeze the active chain while headers kept
        // being indexed, so every later height would resolve to nothing.
        spdlog::error("[header_index] Active chain is at capacity ({}); cannot extend to height {}",
            capacity_, size);
        return;
    }
    // Write the entry, then publish it: a reader that sees the new size is
    // guaranteed to see the entry too.
    active_[size] = idx;
    active_size_.store(size + 1, std::memory_order_release);
}

void header_index::active_truncate(int32_t height) {
    if (height < 0) {
        // Fork below genesis: the whole active chain goes.
        active_size_.store(0, std::memory_order_release);
        return;
    }
    if (height == std::numeric_limits<int32_t>::max()) {
        return;   // height + 1 would overflow; nothing to truncate anyway
    }
    auto const new_size = height + 1;
    if (new_size < active_size_.load(std::memory_order_relaxed)) {
        // Shrinking only: a concurrent reader can see a shorter chain, never a
        // stale entry.
        active_size_.store(new_size, std::memory_order_release);
    }
}

header_index::index_t header_index::active_at(int32_t height) const {
    if (height < 0) {
        return null_index;
    }
    if (height >= active_size_.load(std::memory_order_acquire)) {
        return null_index;
    }
    return active_[height];
}

int32_t header_index::active_tip_height() const {
    return active_size_.load(std::memory_order_acquire) - 1;
}

void header_index::active_set_tip(index_t tip_idx) {
    // A forward extension re-points at a descendant of the current tip and does
    // not invalidate in-flight work; only a move onto a different branch does.
    auto const previous_tip = active_at(active_tip_height());
    bool const same_branch = (previous_tip == null_index) ||
        (tip_idx != null_index && get_ancestor(tip_idx, heights_[previous_tip]) == previous_tip);

    // The generation is bumped at the END of a branch change, never here: a
    // reader that saw the new generation while this function was still rebuilding
    // would pair it with heights that still resolve to the OLD branch, and stamp
    // stale work as current — the exact confusion the counter exists to prevent.

    if (tip_idx == null_index) {
        active_size_.store(0, std::memory_order_release);
        if ( ! same_branch) {
            generation_.fetch_add(1, std::memory_order_release);
        }
        return;
    }

    // Walk the new branch back to the first entry already on the active chain —
    // that is the fork point. Collect the branch on the way so it can be linked
    // in ascending order afterwards.
    std::vector<index_t> branch;
    index_t idx = tip_idx;
    while (idx != null_index) {
        auto const h = heights_[idx];
        if (active_at(h) == idx) {
            break;   // fork point: this entry is already active
        }
        branch.push_back(idx);
        idx = parent_indices_[idx];
    }

    auto const fork_height = (idx == null_index) ? -1 : heights_[idx];
    active_truncate(fork_height);

    // Link the branch in, lowest height first.
    for (auto it = branch.rbegin(); it != branch.rend(); ++it) {
        active_push(*it);
    }

    // Now that the new active chain is fully published, mark the branch change.
    // Ordered after the pushes so any reader observing the new generation is
    // guaranteed to resolve heights against the new branch.
    if ( ! same_branch) {
        generation_.fetch_add(1, std::memory_order_release);
    }
}

void header_index::set_received_time(index_t idx, uint32_t unix_seconds) {
    header_received_times_[idx] = unix_seconds;
}

uint32_t header_index::get_received_time(index_t idx) const {
    return header_received_times_[idx];
}

int16_t header_index::get_file_number(index_t idx) const {
    return int16_t(get_block_pos(idx).file);
}

uint32_t header_index::get_data_pos(index_t idx) const {
    return get_block_pos(idx).pos;
}

uint32_t header_index::get_undo_pos(index_t idx) const {
    return undo_positions_[idx];
}

bool header_index::has_block_data(index_t idx) const {
    return ! get_block_pos(idx).is_null();
}

bool header_index::has_undo_data(index_t idx) const {
    // Undo data exists if file_number is valid and undo_pos is non-zero
    // (position 0 is valid for block data but not for undo, since undo
    // is written after the block is connected)
    return has_block_data(idx) && undo_positions_[idx] > 0;
}

// =============================================================================
// Traversal Primitives
// =============================================================================

header_index::index_t header_index::get_ancestor_linear(index_t start_idx, int32_t target_height) const {
    if (start_idx == null_index || target_height < 0) {
        return null_index;
    }

    int32_t current_height = heights_[start_idx];
    if (target_height > current_height) {
        return null_index;
    }

    int32_t remaining = current_height - target_height;
    index_t idx = start_idx;

    while (remaining > 0 && idx != null_index) {
        idx = parent_indices_[idx];
        --remaining;
    }
    return idx;
}

header_index::index_t header_index::get_ancestor(index_t start_idx, int32_t target_height) const {
    if (start_idx == null_index || target_height < 0) {
        return null_index;
    }

    if (target_height > heights_[start_idx]) {
        return null_index;
    }

    index_t idx = start_idx;
    int32_t height_walk = heights_[start_idx];

    while (height_walk > target_height) {
        int32_t height_skip = get_skip_height(height_walk);
        int32_t height_skip_prev = get_skip_height(height_walk - 1);

        if (skip_indices_[idx] != null_index &&
            (height_skip == target_height ||
             (height_skip > target_height &&
              !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
            idx = skip_indices_[idx];
            height_walk = height_skip;
        } else {
            idx = parent_indices_[idx];
            --height_walk;
        }

        if (height_walk <= target_height) {
            break;
        }
    }
    return idx;
}

header_index::index_t header_index::find_fork(index_t idx_a, index_t idx_b) const {
    if (idx_a == null_index || idx_b == null_index) {
        return null_index;
    }

    int32_t height_a = heights_[idx_a];
    int32_t height_b = heights_[idx_b];

    // Equalize heights using skip pointers for efficiency
    if (height_a > height_b) {
        idx_a = get_ancestor(idx_a, height_b);
        height_a = height_b;
    } else if (height_b > height_a) {
        idx_b = get_ancestor(idx_b, height_a);
        height_b = height_a;
    }

    // Walk back in parallel until they meet
    while (idx_a != idx_b && idx_a != null_index && idx_b != null_index) {
        idx_a = parent_indices_[idx_a];
        idx_b = parent_indices_[idx_b];
    }

    return (idx_a == idx_b) ? idx_a : null_index;
}

// =============================================================================
// State Queries
// =============================================================================

size_t header_index::size() const {
    return hash_to_idx_.size();
}

size_t header_index::capacity() const {
    return capacity_;
}

bool header_index::at_capacity_warning() const {
    return next_idx_.load(std::memory_order_relaxed) > warn_threshold_;
}

size_t header_index::memory_usage() const {
    // Calculate approximate memory usage
    size_t const count = size();

    size_t total = 0;
    total += capacity_ * sizeof(hash_digest);    // hashes_
    total += capacity_ * sizeof(hash_digest);    // prev_block_hashes_
    total += capacity_ * sizeof(index_t);        // parent_indices_
    total += capacity_ * sizeof(index_t);        // skip_indices_
    total += capacity_ * sizeof(int32_t);        // heights_
    total += capacity_ * sizeof(uint256_t);      // chain_works_
    total += capacity_ * sizeof(uint32_t);       // statuses_
    total += capacity_ * sizeof(uint32_t);       // versions_
    total += capacity_ * sizeof(hash_digest);    // merkle_roots_
    total += capacity_ * sizeof(uint32_t);       // timestamps_
    total += capacity_ * sizeof(uint32_t);       // bits_
    total += capacity_ * sizeof(uint32_t);       // nonces_
    total += capacity_ * sizeof(uint64_t);       // block_positions_
    total += capacity_ * sizeof(uint32_t);       // undo_positions_

    // Estimate hash map overhead (~1.5x for load factor + bucket overhead)
    total += count * (sizeof(hash_digest) + sizeof(index_t)) * 3 / 2;

    return total;
}

// =============================================================================
// Skip List Helpers
// =============================================================================

int32_t header_index::invert_lowest_one(int32_t n) {
    return n & (n - 1);
}

int32_t header_index::get_skip_height(int32_t height) {
    if (height < 2) {
        return 0;
    }
    // For odd heights: more aggressive skip
    // For even heights: just invert lowest bit
    return (height & 1)
        ? invert_lowest_one(invert_lowest_one(height - 1)) + 1
        : invert_lowest_one(height);
}

header_index::index_t header_index::build_skip(index_t parent_idx, int32_t height) const {
    if (parent_idx == null_index) {
        return null_index;
    }
    return get_ancestor_internal(parent_idx, get_skip_height(height));
}

header_index::index_t header_index::get_ancestor_internal(index_t start_idx, int32_t target_height) const {
    // Same as get_ancestor but can be called during add() under bucket lock
    if (start_idx == null_index || target_height < 0) {
        return null_index;
    }

    if (target_height > heights_[start_idx]) {
        return null_index;
    }

    index_t idx = start_idx;
    int32_t height_walk = heights_[start_idx];

    while (height_walk > target_height) {
        int32_t height_skip = get_skip_height(height_walk);
        int32_t height_skip_prev = get_skip_height(height_walk - 1);

        if (skip_indices_[idx] != null_index &&
            (height_skip == target_height ||
             (height_skip > target_height &&
              !(height_skip_prev < height_skip - 2 && height_skip_prev >= target_height)))) {
            idx = skip_indices_[idx];
            height_walk = height_skip;
        } else {
            idx = parent_indices_[idx];
            --height_walk;
        }

        if (height_walk <= target_height) {
            break;
        }
    }
    return idx;
}

} // namespace kth::database
