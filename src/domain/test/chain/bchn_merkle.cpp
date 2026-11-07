// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of Bitcoin Cash Node src/test/merkle_tests.cpp, merkle_test (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b).
// Original: Copyright (c) 2015-2016 The Bitcoin Core developers,
//           Copyright (c) 2017-2022 The Bitcoin developers, MIT license.
//
// BCHN computes the root together with a `mutated` flag (CVE-2012-2459: duplicating the
// last transactions of a block with an odd number at some level gives the same root).
// Knuth's generate_merkle_root has no such flag, so here the old reference algorithm of
// the original test provides it, and the entry point that must refuse a mutated block
// is block::check_body.

#include <test_helpers.hpp>

#include <algorithm>
#include <random>
#include <vector>

using namespace kth;
namespace chain = kth::domain::chain;

namespace {

hash_digest hash_pair(hash_digest const& a, hash_digest const& b) {
    data_chunk both(a.begin(), a.end());
    both.insert(both.end(), b.begin(), b.end());
    return bitcoin_hash(both);
}

// BlockBuildMerkleTree of the original test: the reference root and the mutated flag.
hash_digest reference_root(std::vector<hash_digest> const& txids, bool& mutated) {
    std::vector<hash_digest> tree(txids);
    mutated = false;
    size_t j = 0;
    for (size_t size = txids.size(); size > 1; size = (size + 1) / 2) {
        for (size_t i = 0; i < size; i += 2) {
            size_t const i2 = std::min(i + 1, size - 1);
            if (i2 == i + 1 && i2 + 1 == size && tree[j + i] == tree[j + i2]) {
                mutated = true;   // two identical hashes at the end of the list at a particular level
            }
            tree.push_back(hash_pair(tree[j + i], tree[j + i2]));
        }
        j += size;
    }
    return tree.empty() ? null_hash : tree.back();
}

int ctz(uint32_t i) {
    if (i == 0) return 0;
    int j = 0;
    while ( ! (i & 1)) { j++; i >>= 1; }
    return j;
}

// A transaction with a distinct non-null input (so duplicates are double spends) and a distinct locktime.
chain::transaction tx_number(uint32_t n, bool coinbase) {
    hash_digest prev{};
    prev.fill(0x44);
    prev[0] = uint8_t(n);
    prev[1] = uint8_t(n >> 8);
    chain::input::list inputs;
    inputs.emplace_back(coinbase ? chain::output_point{null_hash, chain::point::null_index} : chain::output_point{prev, n},
                        chain::script(data_chunk(4, 0x51), false), 0xffffffff);
    chain::output::list outputs;
    outputs.emplace_back(1000 + n, chain::script(data_chunk(3, 0x51), false), chain::token_data_opt{});
    return chain::transaction(1, n, std::move(inputs), std::move(outputs));
}

std::vector<hash_digest> txids_of(chain::transaction::list const& txs) {
    std::vector<hash_digest> ids;
    for (auto const& tx : txs) ids.push_back(tx.hash());
    return ids;
}

} // namespace

TEST_CASE("BCHN merkle_tests - merkle_test", "[bchn_non_vm][merkle]") {
    std::mt19937 rng(12345);   // the original draws 15 random sizes; fixed here to keep the run deterministic
    for (int i = 0; i < 32; i++) {
        // Try 32 block sizes: all sizes from 0 to 16 inclusive, and then 15 random sizes.
        int const ntx = (i <= 16) ? i : 17 + int(rng() % 4000);
        // Try up to 3 mutations.
        for (int mutate = 0; mutate <= 3; mutate++) {
            // The last how many transactions to duplicate first.
            int const duplicate1 = mutate >= 1 ? 1 << ctz(ntx) : 0;
            if (duplicate1 >= ntx) break;   // duplicating the entire tree adds a level: a different root
            int const ntx1 = ntx + duplicate1;
            int const duplicate2 = mutate >= 2 ? 1 << ctz(ntx1) : 0;
            if (duplicate2 >= ntx1) break;
            int const ntx2 = ntx1 + duplicate2;
            int const duplicate3 = mutate >= 3 ? 1 << ctz(ntx2) : 0;
            if (duplicate3 >= ntx2) break;
            int const ntx3 = ntx2 + duplicate3;

            DYNAMIC_SECTION("ntx " << ntx << " mutate " << mutate) {
                // Build a block with ntx different transactions.
                chain::transaction::list txs;
                for (int j = 0; j < ntx; j++) txs.push_back(tx_number(uint32_t(j), j == 0));
                chain::block block(chain::header{1, null_hash, null_hash, 0, 0, 0}, chain::transaction::list(txs));

                // The root of the block before mutating it, and its mutated flag
                bool unmutated_mutated = false;
                auto const unmutated_root = reference_root(txids_of(txs), unmutated_mutated);
                CHECK( ! unmutated_mutated);
                CHECK(block.generate_merkle_root() == unmutated_root);

                // Optionally mutate by duplicating the last transactions: same merkle root.
                txs.resize(size_t(ntx3));
                for (int j = 0; j < duplicate1; j++) txs[size_t(ntx + j)] = txs[size_t(ntx + j - duplicate1)];
                for (int j = 0; j < duplicate2; j++) txs[size_t(ntx1 + j)] = txs[size_t(ntx1 + j - duplicate2)];
                for (int j = 0; j < duplicate3; j++) txs[size_t(ntx2 + j)] = txs[size_t(ntx2 + j - duplicate3)];

                bool mutated_mutated = false;
                auto const mutated_root = reference_root(txids_of(txs), mutated_mutated);
                CHECK(mutated_mutated == (mutate != 0));
                CHECK(mutated_root == unmutated_root);

                chain::block mutated_block(chain::header{1, null_hash, null_hash, 0, 0, 0}, chain::transaction::list(txs));
                CHECK(mutated_block.generate_merkle_root() == unmutated_root);

                // The entry point that has to refuse a mutated block: with the right merkle root in the
                // header, only the duplicated transactions (double spends of their own inputs) give it away.
                if (ntx >= 2 && mutate != 0) {
                    chain::block with_root(chain::header{1, null_hash, unmutated_root, 0, 0, 0}, chain::transaction::list(txs));
                    CHECK(with_root.check_body() != error::success);
                }
            }
        }
    }
}
