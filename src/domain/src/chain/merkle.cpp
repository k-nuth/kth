// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/merkle.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include <crypto/sha256.h>

#include <kth/domain/chain/detail/gpu_merkle.hpp>

namespace kth::domain::chain {

namespace {

// A hash_list is its hashes one after another, which is the layout the batch
// hash reads its pairs from and writes its results to.
static_assert(sizeof(hash_digest) == hash_size);

// Whether two real siblings of this level are equal. The last hash of an odd
// level has no sibling here.
bool has_equal_siblings(hash_list const& level) {
    for (size_t i = 0; i + 1 < level.size(); i += 2) {
        if (level[i] == level[i + 1]) {
            return true;
        }
    }
    return false;
}

// Replaces a level by the one above it, in place.
//
// SHA256D64 hashes pair i, the 64 bytes at 64 * i, into the 32 bytes at 32 * i.
// The result never lands ahead of the pair it came from, and each batch of
// pairs is read whole before its results are written, so the level can be
// hashed over itself. SHA256D64 picks the widest implementation the machine
// has: SHA-NI, AVX2, SSE4.1, ARM SHA2.
void hash_level(hash_list& level) {
    auto const size = level.size();
    auto const pairs = size / 2;
    auto* const data = level.front().data();
    if (pairs != 0) {
        SHA256D64(data, data, pairs);
    }
    if (size % 2 != 0) {
        // The last hash is paired with itself. It sits past every result
        // written above, so it is still the hash of this level.
        std::array<uint8_t, 2 * hash_size> twice;
        auto const& last = level[size - 1];
        std::copy(last.begin(), last.end(), twice.begin());
        std::copy(last.begin(), last.end(), twice.begin() + hash_size);
        SHA256D64(level[pairs].data(), twice.data(), 1);
    }
    level.resize(pairs + size % 2);
}

hash_digest root_on_the_cpu(hash_list& hashes, bool* mutated) {
    while (hashes.size() > 1) {
        if (mutated != nullptr && ! *mutated) {
            *mutated = has_equal_siblings(hashes);
        }
        hash_level(hashes);
    }
    return hashes.front();
}

} // namespace

hash_digest merkle_root(hash_list hashes) {
    if (hashes.empty()) {
        return null_hash;
    }
    bool equal_siblings = false;
    if (auto const root = detail::gpu_merkle_root(hashes, equal_siblings)) {
        return *root;
    }
    return root_on_the_cpu(hashes, nullptr);
}

hash_digest merkle_root(hash_list hashes, bool& mutated) {
    mutated = false;
    if (hashes.empty()) {
        return null_hash;
    }
    if (auto const root = detail::gpu_merkle_root(hashes, mutated)) {
        return *root;
    }
    return root_on_the_cpu(hashes, &mutated);
}

std::vector<hash_list> merkle_branches(hash_list hashes, std::span<size_t const> indexes) {
    std::vector<hash_list> branches(indexes.size());
    // Where each hash asked for stands in the current level. One past the
    // last hash has no branch and is left out of every level.
    std::vector<size_t> positions(indexes.begin(), indexes.end());
    auto const count = hashes.size();

    while (hashes.size() > 1) {
        auto const size = hashes.size();
        for (size_t i = 0; i < positions.size(); ++i) {
            if (indexes[i] >= count) {
                continue;
            }
            auto const sibling = positions[i] ^ size_t(1);
            // A last hash with no partner is paired with itself.
            branches[i].push_back(hashes[sibling < size ? sibling : positions[i]]);
            positions[i] /= 2;
        }
        hash_level(hashes);
    }
    return branches;
}

hash_list merkle_branch(hash_list hashes, size_t index) {
    return std::move(merkle_branches(std::move(hashes), std::span<size_t const>(&index, 1)).front());
}

} // namespace kth::domain::chain
