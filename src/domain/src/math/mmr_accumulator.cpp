// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/math/mmr.hpp>

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace kth::domain {

std::optional<mmr_accumulator> mmr_accumulator::from_peaks(uint64_t leaf_count, hash_list peaks) {
    if (peaks.size() != static_cast<size_t>(std::popcount(leaf_count))) {
        return std::nullopt;
    }

    mmr_accumulator result;
    result.leaf_count_ = leaf_count;
    result.peaks_ = std::move(peaks);
    return result;
}

bool mmr_accumulator::extend(hash_digest const& leaf) {
    if (leaf_count_ == std::numeric_limits<uint64_t>::max()) {
        return false;
    }

    auto const merge_count = std::countr_one(leaf_count_);
    auto current = leaf;
    for (int i = 0; i < merge_count; ++i) {
        current = mmr_node_hash(peaks_[peaks_.size() - 1 - i], current);
    }

    // Hash first so an allocation failure cannot leave partially merged peaks.
    peaks_.resize(peaks_.size() - merge_count);
    peaks_.push_back(current);
    ++leaf_count_;
    return true;
}

hash_digest mmr_accumulator::root() const {
    if (leaf_count_ == 0) {
        return null_hash;
    }

    auto current = peaks_.back();
    auto remaining = leaf_count_;
    auto height = std::countr_zero(remaining);
    for (auto i = peaks_.size() - 1; i > 0; --i) {
        remaining &= remaining - 1;
        auto const next_height = std::countr_zero(remaining);
        while (height < next_height) {
            current = mmr_node_hash(current, current);
            ++height;
        }
        current = mmr_node_hash(peaks_[i - 1], current);
        ++height;
    }
    return current;
}

bool mmr_accumulator::verify_proof_to_peak(uint64_t index, hash_digest const& leaf, hash_list const& siblings) const {
    if (index >= leaf_count_) {
        return false;
    }

    auto remaining = leaf_count_;
    uint64_t mountain_start = 0;
    size_t peak_index = 0;
    auto height = std::bit_width(remaining) - 1;
    auto mountain_size = uint64_t{1} << height;
    while (index >= mountain_start + mountain_size) {
        mountain_start += mountain_size;
        remaining -= mountain_size;
        ++peak_index;
        height = std::bit_width(remaining) - 1;
        mountain_size = uint64_t{1} << height;
    }

    if (siblings.size() != height) {
        return false;
    }

    // Trusted peaks fix the tree shape, so duplicate leaves are unambiguous here.
    auto current = leaf;
    for (auto const& sibling : siblings) {
        current = (index & 1) ? mmr_node_hash(sibling, current) : mmr_node_hash(current, sibling);
        index >>= 1;
    }
    return current == peaks_[peak_index];
}

bool verify_mmr_proof(uint64_t index, uint64_t leaf_count, hash_digest const& leaf,
                      hash_list const& siblings, hash_digest const& expected_root) {
    if (index >= leaf_count) {
        return false;
    }
    if (siblings.size() != std::bit_width(leaf_count - 1)) {
        return false;
    }

    auto current = leaf;
    auto width = leaf_count;
    for (auto const& sibling : siblings) {
        if (index & 1) {
            // Padding only creates right siblings; a duplicate on the left
            // would admit phantom leaves through CVE-2012-2459.
            if (sibling == current) {
                return false;
            }
            current = mmr_node_hash(sibling, current);
        } else {
            if (index == width - 1 && sibling != current) {
                return false;
            }
            current = mmr_node_hash(current, sibling);
        }
        index >>= 1;
        width = (width >> 1) + (width & 1);
    }
    return current == expected_root;
}

bool mmr_accumulator::verify_proof_to_root(uint64_t index, hash_digest const& leaf, hash_list const& siblings) const {
    return verify_mmr_proof(index, leaf_count_, leaf, siblings, root());
}

std::optional<mmr_accumulator> mmr_accumulator::bootstrap(uint64_t leaf_count, hash_digest const& last_leaf,
                                                        hash_list const& siblings, hash_digest const& expected_root) {
    if (leaf_count == 0) {
        if ( ! siblings.empty() || expected_root != null_hash) {
            return std::nullopt;
        }
        return mmr_accumulator{};
    }
    if (siblings.size() != std::bit_width(leaf_count - 1)) {
        return std::nullopt;
    }

    hash_list peaks;
    peaks.reserve(std::popcount(leaf_count));
    auto current = last_leaf;
    auto index = leaf_count - 1;
    auto const smallest_height = std::countr_zero(leaf_count);

    // The last leaf first climbs through its own complete mountain. Every
    // sibling on this segment is on the left and must pass the duplicate guard.
    for (int height = 0; height < smallest_height; ++height) {
        auto const& sibling = siblings[height];
        if (sibling == current) {
            return std::nullopt;
        }
        current = mmr_node_hash(sibling, current);
        index >>= 1;
    }
    peaks.push_back(current);

    for (size_t height = smallest_height; height < siblings.size(); ++height) {
        auto const& sibling = siblings[height];
        if (index & 1) {
            if (sibling == current) {
                return std::nullopt;
            }
            peaks.push_back(sibling);
            current = mmr_node_hash(sibling, current);
        } else {
            // A last-leaf path can only have right siblings made by padding.
            if (sibling != current) {
                return std::nullopt;
            }
            current = mmr_node_hash(current, sibling);
        }
        index >>= 1;
    }

    if (current != expected_root) {
        return std::nullopt;
    }
    std::reverse(peaks.begin(), peaks.end());
    return from_peaks(leaf_count, std::move(peaks));
}

} // namespace kth::domain
