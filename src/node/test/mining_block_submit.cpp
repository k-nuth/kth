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
