// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

namespace {

// A block with a non-zero header and the given transactions. Defaults to a
// single (coinbase-stand-in) transaction because a block always has at least
// one; `create` rejects an empty transaction list.
chain::block make_block(chain::transaction::list txs = {chain::transaction(1, 0, {}, {})}) {
    return chain::block(
        chain::header{1u, null_hash, null_hash, 0u, 0u, 0u}, std::move(txs));
}

// A block of `count` transactions that differ by their lock time.
chain::block make_block_of(uint32_t count) {
    chain::transaction::list txs;
    for (uint32_t i = 0; i < count; ++i) {
        txs.push_back(chain::transaction(1, i, {}, {}));
    }
    return make_block(std::move(txs));
}

data_chunk block_100k_raw() {
    return to_chunk(
        "010000007f110631052deeee06f0754a3629ad7663e56359fd5f3aa7b3e30a00"
        "000000005f55996827d9712147a8eb6d7bae44175fe0bcfa967e424a25bfe9f4"
        "dc118244d67fb74c9d8e2f1bea5ee82a03010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff07049d8e2f1b"
        "0114ffffffff0100f2052a0100000043410437b36a7221bc977dce712728a954"
        "e3b5d88643ed5aef46660ddcfeeec132724cd950c1fdd008ad4a2dfd354d6af0"
        "ff155fc17c1ee9ef802062feb07ef1d065f0ac000000000100000001260fd102"
        "fab456d6b169f6af4595965c03c2296ecf25bfd8790e7aa29b404eff01000000"
        "8c493046022100c56ad717e07229eb93ecef2a32a42ad041832ffe66bd2e1485"
        "dc6758073e40af022100e4ba0559a4cebbc7ccb5d14d1312634664bac46f36dd"
        "d35761edaae20cefb16f01410417e418ba79380f462a60d8dd12dcef8ebfd7ab"
        "1741c5c907525a69a8743465f063c1d9182eea27746aeb9f1f52583040b1bc34"
        "1b31ca0388139f2f323fd59f8effffffff0200ffb2081d0000001976a914fc7b"
        "44566256621affb1541cc9d59f08336d276b88ac80f0fa02000000001976a914"
        "617f0609c9fabb545105f7898f36b84ec583350d88ac00000000010000000122"
        "cd6da26eef232381b1a670aa08f4513e9f91a9fd129d912081a3dd138cb01301"
        "0000008c4930460221009339c11b83f234b6c03ebbc4729c2633cbc8cbd0d157"
        "74594bfedc45c4f99e2f022100ae0135094a7d651801539df110a028d65459d2"
        "4bc752d7512bc8a9f78b4ab368014104a2e06c38dc72c4414564f190478e3b0d"
        "01260f09b8520b196c2f6ec3d06239861e49507f09b7568189efe8d327c3384a"
        "4e488f8c534484835f8020b3669e5aebffffffff0200ac23fc060000001976a9"
        "14b9a2c9700ff9519516b21af338d28d53ddf5349388ac00743ba40b00000019"
        "76a914eb675c349c474bec8dea2d79d12cff6f330ab48788ac00000000"_base16);
}

} // anonymous namespace

// Start Test Suite: block merkle branch tests

TEST_CASE("block transaction merkle branch of every transaction folds to the merkle root", "[block merkle branch]") {
    // One transaction, full levels, and levels whose last node is paired with
    // itself at the bottom, in the middle and at more than one height.
    for (uint32_t const count : {1u, 2u, 3u, 4u, 5u, 7u, 8u, 13u}) {
        auto const block = make_block_of(count);
        auto const txids = block.to_hashes();
        auto const root = block.generate_merkle_root();

        size_t levels = 0;
        for (size_t width = count; width > 1; width = (width + 1) / 2) {
            ++levels;
        }

        for (size_t index = 0; index < count; ++index) {
            auto const branch = block.transaction_merkle_branch(index);
            REQUIRE(branch.size() == levels);
            REQUIRE(chain::block::merkle_branch_root(txids[index], index, branch) == root);
            REQUIRE(block.transaction_merkle_branch(index, txids) == branch);
        }
    }
}

TEST_CASE("block transaction merkle branch of historic block folds to the header merkle root", "[block merkle branch]") {
    data_chunk const raw = block_100k_raw();
    byte_reader reader(raw);
    auto const result = chain::block::from_data(reader);
    REQUIRE(result);
    auto const& block100k = *result;
    auto const txids = block100k.to_hashes();
    REQUIRE(txids.size() > 1);

    for (size_t index = 0; index < txids.size(); ++index) {
        auto const branch = block100k.transaction_merkle_branch(index);
        REQUIRE(chain::block::merkle_branch_root(txids[index], index, branch) == block100k.header().merkle());
    }
}

TEST_CASE("block transaction merkle branch last transaction of an odd level is its own sibling", "[block merkle branch]") {
    auto const block = make_block_of(3);
    auto const txids = block.to_hashes();
    auto const branch = block.transaction_merkle_branch(2);
    REQUIRE(branch.size() == 2);
    REQUIRE(branch[0] == txids[2]);
}

TEST_CASE("block transaction merkle branch claiming a position past the last transaction is refused", "[block merkle branch]") {
    // Three transactions: the last is paired with itself, so its branch folds
    // to the root from index 3 too, a transaction that is not there.
    auto const block = make_block_of(3);
    auto const txids = block.to_hashes();
    auto const root = block.generate_merkle_root();
    auto const branch = block.transaction_merkle_branch(2);
    REQUIRE(chain::block::merkle_branch_root(txids[2], 2, branch) == root);
    REQUIRE(chain::block::merkle_branch_root(txids[2], 3, branch) != std::optional<hash_digest>(root));
    // Higher up as well: in five transactions the last pair is (4, 4), and
    // its parent is paired with itself again one level up.
    auto const five = make_block_of(5);
    auto const five_txids = five.to_hashes();
    auto const five_branch = five.transaction_merkle_branch(4);
    REQUIRE(chain::block::merkle_branch_root(five_txids[4], 4, five_branch) == five.generate_merkle_root());
    for (size_t index : {5u, 6u, 7u}) {
        REQUIRE(chain::block::merkle_branch_root(five_txids[4], index, five_branch) != std::optional<hash_digest>(five.generate_merkle_root()));
    }
}

TEST_CASE("block transaction merkle branch at another index folds to another root", "[block merkle branch]") {
    auto const block = make_block_of(4);
    auto const txids = block.to_hashes();
    auto const branch = block.transaction_merkle_branch(0);
    REQUIRE(chain::block::merkle_branch_root(txids[0], 0, branch) == block.generate_merkle_root());
    REQUIRE(chain::block::merkle_branch_root(txids[0], 1, branch) != block.generate_merkle_root());
    REQUIRE(chain::block::merkle_branch_root(txids[1], 0, branch) != block.generate_merkle_root());
}

TEST_CASE("block transaction merkle branch index past the last transaction is empty", "[block merkle branch]") {
    auto const block = make_block_of(4);
    REQUIRE(block.transaction_merkle_branch(4).empty());
    REQUIRE(block.transaction_merkle_branch(1000).empty());
    REQUIRE(make_block_of(1).transaction_merkle_branch(0).empty());
}

// End Test Suite
