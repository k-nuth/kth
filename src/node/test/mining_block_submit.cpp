// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <memory>
#include <vector>

#include <kth/domain.hpp>
#include <kth/node/mining/block_submit.hpp>

using namespace kth;
using namespace kth::node::mining;

namespace {

transaction_const_ptr make_tx(uint32_t locktime) {
    return std::make_shared<domain::message::transaction>(
        domain::chain::transaction{1u, locktime, {}, {}});
}

} // namespace

// Start Test Suite: mining block_submit tests

TEST_CASE("assemble_block puts the coinbase first then the job selection", "[mining block_submit]") {
    domain::chain::transaction coinbase{1u, 0u, {}, {}};
    std::vector<transaction_const_ptr> job{make_tx(11), make_tx(22)};

    auto const block = assemble_block(domain::chain::header{}, coinbase, job);

    REQUIRE(block.transactions().size() == 3u);
    REQUIRE(block.transactions()[0].hash() == coinbase.hash());
    REQUIRE(block.transactions()[1].hash() == job[0]->hash());
    REQUIRE(block.transactions()[2].hash() == job[1]->hash());
}

TEST_CASE("assemble_block with an empty selection is coinbase-only", "[mining block_submit]") {
    domain::chain::transaction coinbase{1u, 7u, {}, {}};
    auto const block = assemble_block(domain::chain::header{}, coinbase, {});
    REQUIRE(block.transactions().size() == 1u);
    REQUIRE(block.transactions()[0].hash() == coinbase.hash());
}

TEST_CASE("coinbase_merkle_branch folds the coinbase to the block's merkle root", "[mining block_submit]") {
    domain::chain::transaction coinbase{1u, 0u, {}, {}};
    auto const fold = [&](hash_list const& branch) {
        auto root = coinbase.hash();
        for (auto const& sibling : branch) {
            data_chunk pair;
            extend_data(pair, root);
            extend_data(pair, sibling);
            root = bitcoin_hash(pair);
        }
        return root;
    };

    // Odd and even selections at every level, up to three levels deep.
    std::vector<transaction_const_ptr> job;
    for (uint32_t size = 0; size <= 8; ++size) {
        CAPTURE(size);
        auto const branch = coinbase_merkle_branch(job);
        auto const block = assemble_block(domain::chain::header{}, coinbase, job);
        REQUIRE(fold(branch) == block.generate_merkle_root());
        if ( ! job.empty()) {
            REQUIRE(branch.front() == job.front()->hash());
        }
        job.push_back(make_tx(100u + size));
    }
}

TEST_CASE("coinbase_merkle_branch is empty for a coinbase-only block", "[mining block_submit]") {
    REQUIRE(coinbase_merkle_branch({}).empty());
}
