// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/header_index.hpp>

#include <cstring>

#include <kth/infrastructure/utility/stats.hpp>

namespace kth::blockchain {

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
    file_numbers_.resize(capacity_, -1);
    data_positions_.resize(capacity_, 0);
    undo_positions_.resize(capacity_, 0);

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
    uint64_t chain_work = 0;

    KTH_STATS_TIME_START(find_parent);
    hash_to_idx_.visit(header.previous_block_hash(), [&](auto const& parent_pair) {
        parent_idx = parent_pair.second;
    });
    KTH_STATS_TIME_END(global_sync_stats(), find_parent, find_parent_time_ns, find_parent_calls);

    if (parent_idx != null_index) {
        height = heights_[parent_idx] + 1;
        chain_work = chain_works_[parent_idx] + 1;  // TODO: Calculate actual work from bits
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

uint64_t header_index::get_chain_work(index_t idx) const {
    return chain_works_[idx];
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
    return {
        get_header(idx),
        hashes_[idx],
        parent_indices_[idx],
        skip_indices_[idx],
        heights_[idx],
        chain_works_[idx],
        header_status(statuses_[idx]),
        file_numbers_[idx],
        data_positions_[idx],
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

void header_index::set_chain_work(index_t idx, uint64_t work) {
    chain_works_[idx] = work;
}

// =============================================================================
// Block File Location
// =============================================================================

void header_index::set_block_pos(index_t idx, int16_t file, uint32_t pos) {
    file_numbers_[idx] = file;
    data_positions_[idx] = pos;
}

void header_index::set_undo_pos(index_t idx, uint32_t pos) {
    undo_positions_[idx] = pos;
}

int16_t header_index::get_file_number(index_t idx) const {
    return file_numbers_[idx];
}

uint32_t header_index::get_data_pos(index_t idx) const {
    return data_positions_[idx];
}

uint32_t header_index::get_undo_pos(index_t idx) const {
    return undo_positions_[idx];
}

bool header_index::has_block_data(index_t idx) const {
    return file_numbers_[idx] >= 0;
}

bool header_index::has_undo_data(index_t idx) const {
    // Undo data exists if file_number is valid and undo_pos is non-zero
    // (position 0 is valid for block data but not for undo, since undo
    // is written after the block is connected)
    return file_numbers_[idx] >= 0 && undo_positions_[idx] > 0;
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
    total += capacity_ * sizeof(uint64_t);       // chain_works_
    total += capacity_ * sizeof(uint32_t);       // statuses_
    total += capacity_ * sizeof(uint32_t);       // versions_
    total += capacity_ * sizeof(hash_digest);    // merkle_roots_
    total += capacity_ * sizeof(uint32_t);       // timestamps_
    total += capacity_ * sizeof(uint32_t);       // bits_
    total += capacity_ * sizeof(uint32_t);       // nonces_
    total += capacity_ * sizeof(int16_t);        // file_numbers_
    total += capacity_ * sizeof(uint32_t);       // data_positions_
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

} // namespace kth::blockchain
