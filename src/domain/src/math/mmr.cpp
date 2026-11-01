// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/math/mmr.hpp>

#include <array>
#include <bit>
#include <limits>
#include <utility>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain {
namespace {

constexpr uint64_t internal_node_count(uint64_t leaves) noexcept {
    return leaves - std::popcount(leaves);
}

struct mountain {
    uint64_t start;
    unsigned height;
    size_t peak_index;
};

// The caller checks index < leaves, so a containing mountain always exists.
mountain containing_mountain(uint64_t index, uint64_t leaves) {
    uint64_t start = 0;
    size_t peak_index = 0;
    while (true) {
        auto const height = static_cast<unsigned>(std::bit_width(leaves) - 1);
        auto const size = uint64_t{1} << height;
        if (index - start < size) {
            return {start, height, peak_index};
        }
        start += size;
        leaves -= size;
        ++peak_index;
    }
}

} // namespace

hash_digest mmr_node_hash(hash_digest const& left, hash_digest const& right) {
    return bitcoin_hash(splice(left, right));
}

void mmr::append(hash_digest const& leaf, hash_digest const& previous_leaf) {
    KTH_ASSERT(leaf_count_ < std::numeric_limits<uint64_t>::max());
    auto const merges = static_cast<unsigned>(std::countr_one(leaf_count_));

    // Compute before inserting so allocation failure leaves the old state intact.
    std::array<hash_digest, 64> added;
    if (merges != 0) {
        added[0] = mmr_node_hash(previous_leaf, leaf);
        for (unsigned level = 1; level < merges; ++level) {
            auto const end = leaf_count_ + 2 - (uint64_t{1} << level);
            auto const peak_index = internal_node_count(end) - 1;
            added[level] = mmr_node_hash(nodes_[peak_index], added[level - 1]);
        }
    }
    nodes_.insert(nodes_.end(), added.begin(), added.begin() + merges);
    ++leaf_count_;
}

bool mmr::remove(uint64_t count) noexcept {
    if (count > leaf_count_) {
        return false;
    }
    leaf_count_ -= count;
    nodes_.resize(static_cast<size_t>(internal_node_count(leaf_count_)));
    return true;
}

std::optional<hash_list> mmr::peaks(uint64_t leaf_count, hash_digest const& last_leaf) const {
    if (leaf_count > leaf_count_) {
        return std::nullopt;
    }

    hash_list result;
    result.reserve(std::popcount(leaf_count));
    uint64_t end = 0;
    auto remaining = leaf_count;
    while (remaining > 1) {
        auto const size = uint64_t{1} << (std::bit_width(remaining) - 1);
        end += size;
        result.push_back(nodes_[internal_node_count(end) - 1]);
        remaining -= size;
    }
    if (remaining != 0) {
        result.push_back(last_leaf);
    }
    return result;
}

std::optional<hash_digest> mmr::root(uint64_t leaf_count, hash_digest const& last_leaf) const {
    auto values = peaks(leaf_count, last_leaf);
    if ( ! values) {
        return std::nullopt;
    }
    return mmr_accumulator::from_peaks(leaf_count, std::move(*values))->root();
}

std::optional<hash_list> mmr::proof_to_peak(uint64_t index, uint64_t leaf_count,
    hash_digest const& leaf_sibling) const {
    if (index >= leaf_count || leaf_count > leaf_count_) {
        return std::nullopt;
    }

    auto const mountain = containing_mountain(index, leaf_count);
    hash_list proof;
    proof.reserve(mountain.height);
    if (mountain.height != 0) {
        proof.push_back(leaf_sibling);
        auto local = (index - mountain.start) >> 1;
        for (unsigned level = 1; level < mountain.height; ++level) {
            auto const end = mountain.start + (((local ^ 1) + 1) << level);
            // Internal nodes are appended bottom-up when their last leaf arrives.
            auto const node = internal_node_count(end) - std::countr_one(end - 1) + level - 1;
            proof.push_back(nodes_[node]);
            local >>= 1;
        }
    }
    return proof;
}

std::optional<hash_list> mmr::proof_to_root(uint64_t index, uint64_t leaf_count,
    hash_digest const& leaf_sibling, hash_digest const& last_leaf) const {
    auto proof = proof_to_peak(index, leaf_count, leaf_sibling);
    if ( ! proof) {
        return std::nullopt;
    }

    auto const mountain = containing_mountain(index, leaf_count);
    auto const values = *peaks(leaf_count, last_leaf);
    auto hash = values.back();
    auto remaining = leaf_count;
    auto height = static_cast<unsigned>(std::countr_zero(remaining));
    bool on_path = mountain.peak_index == values.size() - 1;

    for (size_t i = values.size() - 1; i > 0; --i) {
        remaining &= remaining - 1;
        auto const next_height = static_cast<unsigned>(std::countr_zero(remaining));
        while (height < next_height) {
            if (on_path) {
                proof->push_back(hash);
            }
            hash = mmr_node_hash(hash, hash);
            ++height;
        }
        if (i - 1 == mountain.peak_index) {
            proof->push_back(hash);
            on_path = true;
        } else if (on_path) {
            proof->push_back(values[i - 1]);
        }
        hash = mmr_node_hash(values[i - 1], hash);
        ++height;
    }
    return proof;
}

} // namespace kth::domain
