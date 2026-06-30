// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// TODO(legacy): move this into the tests by source file organization.
TEST_CASE( "satoshi words mainnet", "[satoshi words]" ) {
    // Create mainnet genesis block (contains a single coinbase transaction).
    auto const block = chain::block::genesis_mainnet();
    auto const& transactions = block.transactions();
    REQUIRE(transactions.size() == 1u);

    // Coinbase tx (first in block) has a single input.
    auto const& coinbase_tx = transactions[0];
    auto const& coinbase_inputs = coinbase_tx.inputs();
    REQUIRE(coinbase_inputs.size() == 1u);

    // Convert the input script to its raw format.
    auto const& coinbase_input = coinbase_inputs[0];
    auto const raw_message = kth::to_data_chunk(coinbase_input.script(), false);
    REQUIRE(raw_message.size() > 8u);

    // Convert to a string after removing the 8 byte checksum.
    std::string message;
    message.resize(raw_message.size() - 8);
    std::copy(raw_message.begin() + 8, raw_message.end(), message.begin());

    REQUIRE(message == "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks");
}

