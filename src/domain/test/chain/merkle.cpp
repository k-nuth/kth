// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <vector>

#include <kth/domain/chain/merkle.hpp>

using namespace kth;
using namespace kd;

namespace {

// Hashes with no pattern, different for each seed.
hash_list scrambled(size_t count, uint32_t seed = 0x9e3779b9u) {
    hash_list out(count);
    auto state = seed;
    for (auto& hash : out) {
        for (auto& b : hash) {
            state = state * 1664525u + 1013904223u;
            b = uint8_t(state >> 24);
        }
    }
    return out;
}

hash_digest hash_pair(hash_digest const& left, hash_digest const& right) {
    return bitcoin_hash(splice(left, right));
}

// The definition, one pair at a time with nothing shared: the last hash of an
// odd level is paired with itself.
hash_digest reference_root(hash_list level) {
    if (level.empty()) {
        return null_hash;
    }
    while (level.size() > 1) {
        hash_list above;
        for (size_t i = 0; i < level.size(); i += 2) {
            above.push_back(hash_pair(level[i], level[i + 1 < level.size() ? i + 1 : i]));
        }
        level = above;
    }
    return level.front();
}

hash_digest fold(hash_digest hash, size_t index, hash_list const& branch) {
    for (auto const& sibling : branch) {
        hash = index % 2 == 0 ? hash_pair(hash, sibling) : hash_pair(sibling, hash);
        index /= 2;
    }
    return hash;
}

} // namespace

// Start Test Suite: merkle tests

TEST_CASE("merkle root of no hashes is null and of one hash is that hash", "[merkle]") {
    REQUIRE(chain::merkle_root(hash_list{}) == null_hash);
    auto const one = scrambled(1);
    REQUIRE(chain::merkle_root(one) == one.front());

    bool mutated = true;
    REQUIRE(chain::merkle_root(hash_list{}, mutated) == null_hash);
    REQUIRE( ! mutated);
    mutated = true;
    REQUIRE(chain::merkle_root(one, mutated) == one.front());
    REQUIRE( ! mutated);
}

TEST_CASE("merkle root hashed in place is the root of the definition, for every size", "[merkle]") {
    // Every count up to several widths of the widest batch hash, so that each
    // of its paths and each remainder is taken, and odd counts at every level.
    std::vector<size_t> sizes;
    for (size_t size = 2; size <= 70; ++size) sizes.push_back(size);
    for (size_t size : {100u, 127u, 128u, 129u, 255u, 256u, 257u, 1000u, 4099u}) sizes.push_back(size);

    for (auto const size : sizes) {
        CAPTURE(size);
        auto const hashes = scrambled(size, uint32_t(size));
        auto const expected = reference_root(hashes);
        REQUIRE(chain::merkle_root(hashes) == expected);
        bool mutated = true;
        REQUIRE(chain::merkle_root(hashes, mutated) == expected);
        REQUIRE( ! mutated);
    }
}

TEST_CASE("merkle root reports equal siblings at any level, and not a last hash paired with itself", "[merkle]") {
    auto const h = scrambled(4);
    bool mutated = false;

    // Equal siblings among the transactions.
    (void)chain::merkle_root(hash_list{h[0], h[1], h[2], h[2]}, mutated);
    REQUIRE(mutated);

    // Equal siblings one level up: two pairs that hash to the same node.
    (void)chain::merkle_root(hash_list{h[0], h[1], h[0], h[1], h[2], h[3]}, mutated);
    REQUIRE(mutated);

    // An odd count: the last hash is paired with itself, which is the rule.
    (void)chain::merkle_root(hash_list{h[0], h[1], h[2]}, mutated);
    REQUIRE( ! mutated);

    // The list that repeats that last hash has the same root, and is the one
    // that is told apart.
    auto const short_root = chain::merkle_root(hash_list{h[0], h[1], h[2]});
    REQUIRE(chain::merkle_root(hash_list{h[0], h[1], h[2], h[2]}, mutated) == short_root);
    REQUIRE(mutated);
}

TEST_CASE("merkle branch of every hash folds to the root, for every size", "[merkle]") {
    for (size_t size = 1; size <= 40; ++size) {
        auto const hashes = scrambled(size, uint32_t(1000 + size));
        auto const root = chain::merkle_root(hashes);
        for (size_t index = 0; index < size; ++index) {
            CAPTURE(size, index);
            auto const branch = chain::merkle_branch(hashes, index);
            REQUIRE(fold(hashes[index], index, branch) == root);
        }
    }
}

TEST_CASE("merkle branch is empty for one hash and for an index past the last", "[merkle]") {
    REQUIRE(chain::merkle_branch(scrambled(1), 0).empty());
    REQUIRE(chain::merkle_branch(scrambled(5), 5).empty());
    REQUIRE(chain::merkle_branch(scrambled(5), 1000).empty());
    REQUIRE(chain::merkle_branch(hash_list{}, 0).empty());
}

TEST_CASE("merkle branch of a last hash with no partner holds that hash", "[merkle]") {
    auto const hashes = scrambled(5);
    auto const branch = chain::merkle_branch(hashes, 4);
    REQUIRE(branch.size() == 3u);
    // It is alone at the first level, and so is its parent at the second.
    REQUIRE(branch[0] == hashes[4]);
    REQUIRE(branch[1] == hash_pair(hashes[4], hashes[4]));
    REQUIRE(fold(hashes[4], 4, branch) == chain::merkle_root(hashes));
}

TEST_CASE("merkle branches of several hashes are the branch of each, in the order asked", "[merkle]") {
    auto const hashes = scrambled(37);
    std::vector<size_t> const indexes{36, 0, 17, 17, 99, 5};
    auto const branches = chain::merkle_branches(hashes, indexes);
    REQUIRE(branches.size() == indexes.size());
    for (size_t i = 0; i < indexes.size(); ++i) {
        CAPTURE(indexes[i]);
        REQUIRE(branches[i] == chain::merkle_branch(hashes, indexes[i]));
    }
    REQUIRE(branches[4].empty());
    REQUIRE(chain::merkle_branches(hashes, {}).empty());
}

// End Test Suite
