// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/domain.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/node/rpc/query.hpp>

using namespace kth;
using namespace kth::node::rpc;

// Start Test Suite: rpc query tests

TEST_CASE("render_blockchain_info serializes the getblockchaininfo fields", "[rpc query]") {
    REQUIRE(render_blockchain_info(
                "Mainnet", /*blocks*/ 10u, /*headers*/ 12u,
                "00ff", /*difficulty*/ 1.0) ==
        R"({"chain":"Mainnet","blocks":10,"headers":12,"bestblockhash":"00ff","difficulty":1.0})");
}

TEST_CASE("transaction_to_hex round-trips through the wire format", "[rpc query]") {
    domain::chain::transaction tx{1u, 7u, {}, {}};

    auto const hex = transaction_to_hex(tx);
    REQUIRE(hex.size() == tx.serialized_size(true) * 2u);

    auto const bytes = decode_base16(hex);
    REQUIRE(bytes.has_value());
    byte_reader reader(*bytes);
    auto const decoded = domain::chain::transaction::from_data(reader);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->hash() == tx.hash());
}
