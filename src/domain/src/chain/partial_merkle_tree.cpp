// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/partial_merkle_tree.hpp>

#include <algorithm>
#include <cstddef>

namespace kth::domain::chain {

namespace {

// The number of nodes at a height: leaves are height zero.
uint64_t width_at(uint64_t transactions, uint32_t height) {
    return (transactions + (uint64_t{1} << height) - 1) >> height;
}

uint32_t height_of(uint64_t transactions) {
    uint32_t height = 0;
    while (width_at(transactions, height) > 1) ++height;
    return height;
}

hash_digest parent_of(hash_digest const& left, hash_digest const& right) {
    return bitcoin_hash(splice(left, right));
}

struct builder {
    hash_list const& txids;
    std::vector<bool> const& matches;
    std::vector<bool> bits;
    hash_list hashes;

    hash_digest hash_at(uint32_t height, uint64_t position) const {
        if (height == 0) return txids[position];
        auto const left = hash_at(height - 1, position * 2);
        // An odd node out is paired with itself.
        auto const right = position * 2 + 1 < width_at(txids.size(), height - 1)
            ? hash_at(height - 1, position * 2 + 1) : left;
        return parent_of(left, right);
    }

    void walk(uint32_t height, uint64_t position) {
        auto const first = position << height;
        auto const last = std::min((position + 1) << height, uint64_t(txids.size()));
        bool parent_of_match = false;
        for (auto at = first; at < last && ! parent_of_match; ++at) parent_of_match = matches[at];
        bits.push_back(parent_of_match);
        if (height == 0 || ! parent_of_match) {
            hashes.push_back(hash_at(height, position));
            return;
        }
        walk(height - 1, position * 2);
        if (position * 2 + 1 < width_at(txids.size(), height - 1)) walk(height - 1, position * 2 + 1);
    }
};

struct extractor {
    partial_merkle_tree const& tree;
    size_t bits_used = 0;
    size_t hashes_used = 0;
    bool bad = false;
    std::vector<std::pair<uint32_t, hash_digest>> matches;

    bool bit(size_t at) const {
        return (tree.flags[at / 8] >> (at % 8)) & 1;
    }

    hash_digest walk(uint32_t height, uint64_t position) {
        if (bits_used >= tree.flags.size() * 8) {
            bad = true;
            return null_hash;
        }
        auto const parent_of_match = bit(bits_used++);
        if (height == 0 || ! parent_of_match) {
            if (hashes_used >= tree.hashes.size()) {
                bad = true;
                return null_hash;
            }
            auto const& hash = tree.hashes[hashes_used++];
            if (height == 0 && parent_of_match) matches.emplace_back(uint32_t(position), hash);
            return hash;
        }
        auto const left = walk(height - 1, position * 2);
        auto right = left;
        if (position * 2 + 1 < width_at(tree.total_transactions, height - 1)) {
            right = walk(height - 1, position * 2 + 1);
            // Two equal children would let a list with a repeated transaction
            // pass for the list without it.
            if (right == left) bad = true;
        }
        return parent_of(left, right);
    }
};

} // namespace

partial_merkle_tree partial_merkle_tree::build(hash_list const& txids, std::vector<bool> const& matches) {
    partial_merkle_tree tree;
    if (txids.empty() || txids.size() != matches.size()) return tree;
    tree.total_transactions = uint32_t(txids.size());
    builder work{txids, matches, {}, {}};
    work.walk(height_of(txids.size()), 0);
    tree.hashes = std::move(work.hashes);
    tree.flags.assign((work.bits.size() + 7) / 8, 0);
    for (size_t at = 0; at < work.bits.size(); ++at) {
        if (work.bits[at]) tree.flags[at / 8] |= uint8_t(1u << (at % 8));
    }
    return tree;
}

std::optional<partial_merkle_tree::extraction> partial_merkle_tree::extract() const {
    if (total_transactions == 0 || hashes.size() > total_transactions
        || flags.size() * 8 < hashes.size()) {
        return std::nullopt;
    }
    extractor work{*this};
    auto const root = work.walk(height_of(total_transactions), 0);
    if (work.bad) return std::nullopt;
    // Every flag byte and every hash is part of the tree.
    if ((work.bits_used + 7) / 8 != flags.size() || work.hashes_used != hashes.size()) {
        return std::nullopt;
    }
    return extraction{root, std::move(work.matches)};
}

} // namespace kth::domain::chain
