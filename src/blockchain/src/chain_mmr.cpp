// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/chain_mmr.hpp>

#include <bit>
#include <mutex>
#include <tuple>
#include <utility>

#include <kth/infrastructure/utility/assert.hpp>

namespace kth::blockchain {

bool chain_mmr::set_tip(header_index::index_t tip) {
    std::unique_lock const lock(mutex_);
    if (tip == header_index::null_index || tip >= headers_.size()) {
        return false;
    }

    std::vector<header_index::index_t> added;
    auto cursor = tip;
    auto count = uint64_t(headers_.get_height(cursor)) + 1;
    while (count != 0 && (count > leaves_.size() || leaves_[count - 1] != cursor)) {
        added.push_back(cursor);
        cursor = headers_.get_parent_index(cursor);
        --count;
    }
    // A different genesis is not a reorganization of this chain.
    if (count == 0 && ! leaves_.empty() && added.back() != leaves_.front()) {
        return false;
    }

    // Allocate before truncating; a failed reserve leaves the old index intact.
    leaves_.reserve(count + added.size());
    std::ignore = mmr_.remove(leaves_.size() - count);
    leaves_.resize(count);
    for (auto it = added.rbegin(); it != added.rend(); ++it) {
        auto const previous = leaves_.empty() ? null_hash : headers_.get_hash(leaves_.back());
        mmr_.append(headers_.get_hash(*it), previous);
        leaves_.push_back(*it);
    }
    return true;
}

void chain_mmr::disconnect(uint32_t height) {
    std::unique_lock const lock(mutex_);
    KTH_ASSERT(uint64_t(height) + 1 == leaves_.size());
    static_cast<void>(height);
    std::ignore = mmr_.remove(1);
    leaves_.pop_back();
}

uint64_t chain_mmr::leaf_count() const {
    std::shared_lock const lock(mutex_);
    return leaves_.size();
}

std::optional<uint64_t> chain_mmr::height_locked(hash_digest const& hash) const {
    auto const idx = headers_.find(hash);
    if (idx == header_index::null_index) {
        return std::nullopt;
    }
    auto const height = uint64_t(headers_.get_height(idx));
    if (height >= leaves_.size() || leaves_[height] != idx) {
        return std::nullopt;
    }
    return height;
}

std::optional<uint64_t> chain_mmr::height(hash_digest const& hash) const {
    std::shared_lock const lock(mutex_);
    return height_locked(hash);
}

std::optional<hash_digest> chain_mmr::root(uint64_t height,
    std::optional<hash_digest> expected_hash) const {
    std::shared_lock const lock(mutex_);
    if (height >= leaves_.size()) {
        return std::nullopt;
    }
    auto const last = headers_.get_hash(leaves_[height]);
    if (expected_hash && *expected_hash != last) {
        return std::nullopt;
    }
    return mmr_.root(height + 1, last);
}

std::optional<shv_proof_result> chain_mmr::proof_locked(uint64_t height,
    uint64_t commitment_height, bool to_root) const {
    if (height > commitment_height || commitment_height >= leaves_.size()) {
        return std::nullopt;
    }
    auto const count = commitment_height + 1;
    auto const sibling = (height ^ 1) < count
        ? headers_.get_hash(leaves_[height ^ 1]) : null_hash;
    auto const last = headers_.get_hash(leaves_[commitment_height]);
    auto proof = to_root ? mmr_.proof_to_root(height, count, sibling, last)
                         : mmr_.proof_to_peak(height, count, sibling);
    auto target = *mmr_.root(count, last);
    if ( ! to_root) {
        auto const peaks = *mmr_.peaks(count, last);
        auto remaining = count;
        auto local = height;
        size_t peak = 0;
        auto size = uint64_t{1} << (std::bit_width(remaining) - 1);
        while (local >= size) {
            local -= size;
            remaining -= size;
            ++peak;
            size = uint64_t{1} << (std::bit_width(remaining) - 1);
        }
        target = peaks[peak];
    }
    return shv_proof_result{height, headers_.get_header(leaves_[height]), std::move(*proof), target};
}

std::optional<shv_proof_result> chain_mmr::proof(uint64_t height,
    uint64_t commitment_height, bool to_root) const {
    std::shared_lock const lock(mutex_);
    return proof_locked(height, commitment_height, to_root);
}

std::optional<shv_proof_result> chain_mmr::proof(hash_digest const& hash,
    uint64_t commitment_height, bool to_root) const {
    std::shared_lock const lock(mutex_);
    auto const height = height_locked(hash);
    return height ? proof_locked(*height, commitment_height, to_root) : std::nullopt;
}

} // namespace kth::blockchain
