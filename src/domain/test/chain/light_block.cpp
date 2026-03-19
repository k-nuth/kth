// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/domain/chain/light_block.hpp>

using namespace kth;
using namespace kd;

// Valid block with 3 transactions (from block.cpp tests)
auto const raw_block_hex =
    "010000007f110631052deeee06f0754a3629ad7663e56359fd5f3aa7b3e30a0000000"
    "0005f55996827d9712147a8eb6d7bae44175fe0bcfa967e424a25bfe9f4dc118244d6"
    "7fb74c9d8e2f1bea5ee82a03010000000100000000000000000000000000000000000"
    "00000000000000000000000000000ffffffff07049d8e2f1b0114ffffffff0100f205"
    "2a0100000043410437b36a7221bc977dce712728a954e3b5d88643ed5aef46660ddcf"
    "eeec132724cd950c1fdd008ad4a2dfd354d6af0ff155fc17c1ee9ef802062feb07ef1"
    "d065f0ac000000000100000001260fd102fab456d6b169f6af4595965c03c2296ecf2"
    "5bfd8790e7aa29b404eff010000008c493046022100c56ad717e07229eb93ecef2a32"
    "a42ad041832ffe66bd2e1485dc6758073e40af022100e4ba0559a4cebbc7ccb5d14d1"
    "312634664bac46f36ddd35761edaae20cefb16f01410417e418ba79380f462a60d8dd"
    "12dcef8ebfd7ab1741c5c907525a69a8743465f063c1d9182eea27746aeb9f1f52583"
    "040b1bc341b31ca0388139f2f323fd59f8effffffff0200ffb2081d0000001976a914"
    "fc7b44566256621affb1541cc9d59f08336d276b88ac80f0fa02000000001976a9146"
    "17f0609c9fabb545105f7898f36b84ec583350d88ac00000000010000000122cd6da2"
    "6eef232381b1a670aa08f4513e9f91a9fd129d912081a3dd138cb013010000008c493"
    "0460221009339c11b83f234b6c03ebbc4729c2633cbc8cbd0d15774594bfedc45c4f9"
    "9e2f022100ae0135094a7d651801539df110a028d65459d24bc752d7512bc8a9f78b4"
    "ab368014104a2e06c38dc72c4414564f190478e3b0d01260f09b8520b196c2f6ec3d0"
    "6239861e49507f09b7568189efe8d327c3384a4e488f8c534484835f8020b3669e5ae"
    "bffffffff0200ac23fc060000001976a914b9a2c9700ff9519516b21af338d28d53dd"
    "f5349388ac00743ba40b0000001976a914eb675c349c474bec8dea2d79d12cff6f330"
    "ab48788ac00000000";

// Helper to decode hex and get raw bytes
data_chunk get_raw_block() {
    auto result = decode_base16(raw_block_hex);
    REQUIRE(result.has_value());
    return *result;
}

// Start Test Suite: light_block tests

TEST_CASE("light_block from_data valid block returns success", "[chain light_block]") {
    auto const raw_block = get_raw_block();
    byte_reader reader(raw_block);
    auto const result = chain::light_block::from_data(reader, true);
    REQUIRE(result.has_value());
}

TEST_CASE("light_block from_data stores raw bytes", "[chain light_block]") {
    auto const raw_block = get_raw_block();
    byte_reader reader(raw_block);
    auto const result = chain::light_block::from_data(reader, true);
    REQUIRE(result.has_value());
    REQUIRE(result->raw_data() == raw_block);
}

TEST_CASE("light_block serialized_size equals raw_data size", "[chain light_block]") {
    auto const raw_block = get_raw_block();
    byte_reader reader(raw_block);
    auto const result = chain::light_block::from_data(reader, true);
    REQUIRE(result.has_value());
    REQUIRE(result->serialized_size() == raw_block.size());
}

TEST_CASE("light_block tx_count returns correct count", "[chain light_block]") {
    auto const raw_block = get_raw_block();
    byte_reader reader(raw_block);
    auto const result = chain::light_block::from_data(reader, true);
    REQUIRE(result.has_value());
    REQUIRE(result->tx_count() == 3);
}

TEST_CASE("light_block header is valid", "[chain light_block]") {
    auto const raw_block = get_raw_block();
    byte_reader reader(raw_block);
    auto const result = chain::light_block::from_data(reader, true);
    REQUIRE(result.has_value());
    REQUIRE(result->header().is_valid());
}

TEST_CASE("light_block header hash matches full block", "[chain light_block]") {
    auto const raw_block = get_raw_block();

    // Parse as light_block
    byte_reader light_reader(raw_block);
    auto const light_result = chain::light_block::from_data(light_reader, true);
    REQUIRE(light_result.has_value());

    // Parse as full block
    byte_reader full_reader(raw_block);
    auto const full_result = chain::block::from_data(full_reader, true);
    REQUIRE(full_result.has_value());

    // Hashes must match
    REQUIRE(light_result->header().hash() == full_result->header().hash());
}

TEST_CASE("light_block is_valid_merkle_root returns true for valid block", "[chain light_block]") {
    auto const raw_block = get_raw_block();
    byte_reader reader(raw_block);
    auto const result = chain::light_block::from_data(reader, true);
    REQUIRE(result.has_value());
    REQUIRE(result->is_valid_merkle_root());
}

TEST_CASE("light_block tx_bytes returns correct data", "[chain light_block]") {
    auto const raw_block = get_raw_block();
    byte_reader reader(raw_block);
    auto const result = chain::light_block::from_data(reader, true);
    REQUIRE(result.has_value());

    // Get first transaction bytes
    auto const tx0_bytes = result->tx_bytes(0);
    REQUIRE(tx0_bytes.size() > 0);

    // Parse it as a transaction to verify it's valid
    byte_reader tx_reader(tx0_bytes);
    auto const tx_result = chain::transaction::from_data(tx_reader, true);
    REQUIRE(tx_result.has_value());
    REQUIRE(tx_result->is_valid());
}

TEST_CASE("light_block from_data insufficient bytes returns error", "[chain light_block]") {
    data_chunk const data(10);
    byte_reader reader(data);
    auto const result = chain::light_block::from_data(reader, true);
    REQUIRE(!result.has_value());
}

TEST_CASE("light_block tx_length returns correct lengths", "[chain light_block]") {
    auto const raw_block = get_raw_block();
    byte_reader reader(raw_block);
    auto const result = chain::light_block::from_data(reader, true);
    REQUIRE(result.has_value());

    // Sum of all tx lengths should equal total block size minus header (80 bytes) minus varint (1 byte for 3 txs)
    size_t total_tx_bytes = 0;
    for (size_t i = 0; i < result->tx_count(); ++i) {
        total_tx_bytes += result->tx_length(i);
    }

    // Block = header (80) + tx_count varint (1) + transactions
    REQUIRE(total_tx_bytes == raw_block.size() - 80 - 1);
}
