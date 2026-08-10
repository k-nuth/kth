// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/database/block_undo.hpp>

using namespace kth;
using namespace kth::database;

namespace {

utxoz::raw_outpoint make_key(uint8_t seed) {
    utxoz::raw_outpoint key{};
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = uint8_t(seed + i);
    }
    return key;
}

// A reference-mode payload: {file_number(4 LE), tx_offset(4 LE)}.
std::vector<uint8_t> make_compact_value(uint32_t file_number, uint32_t offset) {
    std::vector<uint8_t> value(8);
    std::memcpy(value.data(), &file_number, 4);
    std::memcpy(value.data() + 4, &offset, 4);
    return value;
}

} // namespace

// =============================================================================
// block_undo serialization — the on-disk reorg undo record
// =============================================================================

TEST_CASE("block_undo round-trips through serialization", "[block_undo]") {
    block_undo original;
    original.spent.push_back({make_key(1), make_compact_value(0, 4096), 700000});
    original.spent.push_back({make_key(50), make_compact_value(3, 123456789), 12345});
    original.spent.push_back({make_key(200), make_compact_value(32767, 0), 1});

    auto const data = original.to_data();
    REQUIRE(data.size() == original.serialized_size());

    byte_reader reader(data);
    auto parsed = block_undo::from_data(reader);
    REQUIRE(parsed);
    REQUIRE(parsed->spent.size() == 3);

    for (size_t i = 0; i < 3; ++i) {
        REQUIRE(parsed->spent[i].key == original.spent[i].key);
        REQUIRE(parsed->spent[i].value == original.spent[i].value);
        // The ORIGINAL creation height must survive exactly: restoring a spent
        // output with the wrong height would corrupt both its maturity and the
        // median-time-past window used to resolve it.
        REQUIRE(parsed->spent[i].height == original.spent[i].height);
    }
}

TEST_CASE("block_undo round-trips when empty", "[block_undo]") {
    // A block whose only transaction is the coinbase spends nothing.
    block_undo original;

    auto const data = original.to_data();
    REQUIRE(data.size() == original.serialized_size());

    byte_reader reader(data);
    auto parsed = block_undo::from_data(reader);
    REQUIRE(parsed);
    REQUIRE(parsed->spent.empty());
}

TEST_CASE("block_undo round-trips a variable-length payload", "[block_undo]") {
    // Full mode stores the serialized entry rather than an 8-byte reference, so
    // the record must not assume a fixed payload width.
    block_undo original;
    original.spent.push_back({make_key(7), std::vector<uint8_t>(137, 0xAB), 500000});

    auto const data = original.to_data();
    byte_reader reader(data);
    auto parsed = block_undo::from_data(reader);

    REQUIRE(parsed);
    REQUIRE(parsed->spent.size() == 1);
    REQUIRE(parsed->spent[0].value.size() == 137);
    REQUIRE(parsed->spent[0].value == original.spent[0].value);
    REQUIRE(parsed->spent[0].height == 500000);
}

TEST_CASE("block_undo rejects truncated data", "[block_undo]") {
    block_undo original;
    original.spent.push_back({make_key(9), make_compact_value(1, 64), 42});

    auto data = original.to_data();
    data.resize(data.size() - 4);   // clip the height off the last record

    byte_reader reader(data);
    auto parsed = block_undo::from_data(reader);
    REQUIRE( ! parsed);
}
