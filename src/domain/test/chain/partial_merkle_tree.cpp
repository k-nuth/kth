// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace kth;
using namespace kd;
using chain::partial_merkle_tree;

namespace {

// Transaction hashes that are easy to tell apart: the SHA-256 of one byte.
hash_list leaves(size_t count) {
    hash_list out;
    for (size_t i = 0; i < count; ++i) {
        out.push_back(sha256_hash(data_chunk{uint8_t(i)}));
    }
    return out;
}

std::vector<bool> marks(size_t count, std::set<size_t> const& matched) {
    std::vector<bool> out(count, false);
    for (auto const at : matched) out[at] = true;
    return out;
}

hash_digest root_of(hash_list const& txids) {
    return chain::block{}.generate_merkle_root(txids);
}

std::string prefix(hash_digest const& hash) {
    return encode_base16(hash).substr(0, 8);
}

} // namespace

TEST_CASE("partial merkle tree of seven transactions is laid out as other nodes lay it", "[partial merkle tree]") {
    auto const txids = leaves(7);
    REQUIRE(encode_base16(root_of(txids)) == "df8cda881072fe6533296d6370b24ed0ffcfbb5037b88b069ae003bf200ac698");

    struct layout {
        std::set<size_t> matched;
        char const* flags;
        std::vector<std::string> hashes;
    };
    for (auto const& [matched, flags, hashes] : {
            layout{{1, 5}, "d702", {"6e340b9c", "4bf5122f", "67b338aa", "e52d9c50", "e77b9a9a", "195cc51f"}},
            // No match: the root alone, and one flag bit.
            layout{{}, "00", {"df8cda88"}},
            layout{{0, 1, 2, 3, 4, 5, 6}, "ff3f",
                {"6e340b9c", "4bf5122f", "dbc1b4c9", "084fed08", "e52d9c50", "e77b9a9a", "67586e98"}},
            // The last one, which is paired with itself.
            layout{{6}, "35", {"ff16b8c2", "8ade3dbd", "67586e98"}}}) {
        CAPTURE(flags);
        auto const tree = partial_merkle_tree::build(txids, marks(7, matched));
        CHECK(tree.total_transactions == 7);
        CHECK(encode_base16(tree.flags) == flags);
        REQUIRE(tree.hashes.size() == hashes.size());
        for (size_t i = 0; i < hashes.size(); ++i) CHECK(prefix(tree.hashes[i]) == hashes[i]);

        auto const proved = tree.extract();
        REQUIRE(proved);
        CHECK(proved->root == root_of(txids));
        REQUIRE(proved->matches.size() == matched.size());
        size_t at = 0;
        for (auto const position : matched) {
            CHECK(proved->matches[at].first == position);
            CHECK(proved->matches[at].second == txids[position]);
            ++at;
        }
    }
}

TEST_CASE("partial merkle tree proves the block's own root for every size and match", "[partial merkle tree]") {
    for (size_t count = 1; count <= 17; ++count) {
        auto const txids = leaves(count);
        auto const root = root_of(txids);
        // Every single transaction, then every other one, then all of them.
        std::vector<std::set<size_t>> choices;
        for (size_t i = 0; i < count; ++i) choices.push_back({i});
        std::set<size_t> alternate;
        std::set<size_t> all;
        for (size_t i = 0; i < count; ++i) {
            if (i % 2 == 0) alternate.insert(i);
            all.insert(i);
        }
        choices.push_back(alternate);
        choices.push_back(all);
        choices.push_back({});
        for (auto const& matched : choices) {
            CAPTURE(count, matched.size());
            auto const tree = partial_merkle_tree::build(txids, marks(count, matched));
            // Never more hashes than transactions.
            CHECK(tree.hashes.size() <= count);
            auto const proved = tree.extract();
            REQUIRE(proved);
            CHECK(proved->root == root);
            REQUIRE(proved->matches.size() == matched.size());
            size_t at = 0;
            for (auto const position : matched) {
                CHECK(proved->matches[at].first == position);
                CHECK(proved->matches[at].second == txids[position]);
                ++at;
            }
        }
    }
    // One transaction: its hash is the root.
    auto const one = partial_merkle_tree::build(leaves(1), {true});
    CHECK(encode_base16(one.flags) == "01");
    REQUIRE(one.hashes.size() == 1);
    CHECK(one.extract()->root == leaves(1).front());
}

TEST_CASE("partial merkle tree refuses what is not a tree", "[partial merkle tree]") {
    auto const txids = leaves(7);
    auto const good = partial_merkle_tree::build(txids, marks(7, {1, 5}));
    REQUIRE(good.extract());

    // Nothing to build from, or lists of different lengths.
    CHECK(partial_merkle_tree::build({}, {}).total_transactions == 0);
    CHECK_FALSE(partial_merkle_tree::build({}, {}).extract());
    CHECK(partial_merkle_tree::build(txids, marks(6, {})).total_transactions == 0);

    auto no_transactions = good;
    no_transactions.total_transactions = 0;
    CHECK_FALSE(no_transactions.extract());

    // A hash too many, or too few.
    auto extra_hash = good;
    extra_hash.hashes.push_back(txids[0]);
    CHECK_FALSE(extra_hash.extract());
    auto missing_hash = good;
    missing_hash.hashes.pop_back();
    CHECK_FALSE(missing_hash.extract());
    // More hashes than transactions.
    auto crowded = good;
    crowded.total_transactions = 2;
    CHECK_FALSE(crowded.extract());

    // A flag byte too many, or too few.
    auto extra_flags = good;
    extra_flags.flags.push_back(0);
    CHECK_FALSE(extra_flags.extract());
    auto missing_flags = good;
    missing_flags.flags.pop_back();
    CHECK_FALSE(missing_flags.extract());
    auto no_flags = good;
    no_flags.flags.clear();
    CHECK_FALSE(no_flags.extract());

    // A changed hash still parses, and leads to another root.
    auto changed = good;
    changed.hashes[0][0] ^= 1;
    auto const other = changed.extract();
    REQUIRE(other);
    CHECK(other->root != root_of(txids));

    // Two equal children: a list of four whose last two are the same
    // transaction has the root of the list of three.
    hash_list repeated{txids[0], txids[1], txids[2], txids[2]};
    CHECK(root_of(repeated) == root_of(hash_list{txids[0], txids[1], txids[2]}));
    auto const forged = partial_merkle_tree::build(repeated, {false, false, true, true});
    CHECK_FALSE(forged.extract());
}
