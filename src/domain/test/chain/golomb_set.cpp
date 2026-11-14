// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include <kth/domain/chain/golomb_set.hpp>

using namespace kth;
using namespace kd;

namespace {

constexpr auto filter_key = "000102030405060708090a0b0c0d0e0f"_base16;

constexpr std::string_view hex_n0 = "00";
constexpr std::string_view hex_n1 = "01225890";
constexpr std::string_view hex_n2 = "0244b133d083";
constexpr std::string_view hex_n50 =
    "32e7574c9d0c9ef936578c6fcdaac05b354cae73ff57e78545e5056589a45f61"
    "059df5a220c32555fe1f4ca54cfdae5ed4d408c18c2a309eadfe262e91eba428"
    "26d1dd4792e7b862f5c49b764be082733ebd44dc8b106a26e6b2676425229315"
    "20784d0a4e3dd1583505fa765b190fd01d0e9088f91d88ca0a723bd8f1e0a0ec"
    "9cbf477b90";
constexpr std::string_view hex_block = "0499ff80bec18387496fab00";

data_chunk absent_hash() {
    constexpr uint8_t text[] = {'a', 'b', 's', 'e', 'n', 't'};
    return to_chunk(sha256_hash(text));
}

std::vector<data_chunk> sha256_bytes(uint32_t count) {
    std::vector<data_chunk> items;
    items.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t const byte = static_cast<uint8_t>(i);
        items.push_back(to_chunk(sha256_hash(byte_span(&byte, 1))));
    }
    return items;
}

void require_hex(data_chunk const& bytes, std::string_view hex) {
    auto const expected = decode_base16(hex);
    REQUIRE(expected);
    REQUIRE(bytes == *expected);
}

void check_indexed(uint32_t count, std::string_view hex) {
    auto const items = sha256_bytes(count);
    auto const built = chain::golomb_set::build(filter_key, items);
    REQUIRE(built);
    REQUIRE(built->size() == count);

    auto const wire = built->to_data();
    require_hex(wire, hex);
    for (auto const& item : items) {
        REQUIRE(built->match(filter_key, item));
    }
    REQUIRE( ! built->match(filter_key, absent_hash()));

    auto const parsed = chain::golomb_set::from_data(wire);
    REQUIRE(parsed);
    REQUIRE(*parsed == *built);

    REQUIRE(wire.size() > 0u);
    auto const rejected = chain::golomb_set::from_data(byte_span(wire.data(), wire.size() - 1));
    REQUIRE( ! rejected);
    REQUIRE(rejected.error() == chain::golomb_set_error::truncated);
}

// First 32 bytes of each of the ten script-filter leaves, in leaf order. Four keys are distinct.
std::vector<data_chunk> leaf_keys() {
    byte_array<32> const keys[] = {
        "11633077d74aa1a6958c6f1162c9e935c628c6b004d167165f0a4cccd020eecb"_base16,
        "11633077d74aa1a6958c6f1162c9e935c628c6b004d167165f0a4cccd020eecb"_base16,
        "46402f61b3239cd80b93294e10b552d66a31b79c8fe7fdd92a832d00f832e82e"_base16,
        "46402f61b3239cd80b93294e10b552d66a31b79c8fe7fdd92a832d00f832e82e"_base16,
        "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24"_base16,
        "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24"_base16,
        "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24"_base16,
        "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e"_base16,
        "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e"_base16,
        "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e"_base16,
    };
    std::vector<data_chunk> items;
    items.reserve(10);
    for (auto const& key : keys) {
        items.push_back(to_chunk(key));
    }
    return items;
}

} // namespace

// Start Test Suite: golomb set tests

TEST_CASE("sha256 byte items serialize to the known filters and match their members", "[golomb set]") {
    check_indexed(0, hex_n0);
    check_indexed(1, hex_n1);
    check_indexed(2, hex_n2);
    check_indexed(50, hex_n50);
}

TEST_CASE("script filter leaf keys serialize to the known block filter", "[golomb set]") {
    constexpr auto block_key = "aba60f051d9f2e17a4b5035d5e130336"_base16;
    auto const repeated = leaf_keys();
    std::vector<data_chunk> const distinct{repeated[0], repeated[2], repeated[4], repeated[7]};

    auto const from_distinct = chain::golomb_set::build(block_key, distinct);
    auto const from_repeated = chain::golomb_set::build(block_key, repeated);
    REQUIRE(from_distinct);
    REQUIRE(from_repeated);
    REQUIRE(from_distinct->size() == 4u);
    REQUIRE(from_repeated->size() == 4u);
    REQUIRE(*from_distinct == *from_repeated);
    require_hex(from_repeated->to_data(), hex_block);
    for (auto const& key : repeated) {
        REQUIRE(from_repeated->match(block_key, key));
    }
}

TEST_CASE("from_data accepts a complete encoding and refuses a short one", "[golomb set]") {
    auto const empty_span = chain::golomb_set::from_data({});
    REQUIRE( ! empty_span);
    REQUIRE(empty_span.error() == chain::golomb_set_error::truncated);

    uint8_t const zero = 0x00;
    auto const empty_set = chain::golomb_set::from_data(byte_span(&zero, 1));
    REQUIRE(empty_set);
    REQUIRE(*empty_set == chain::golomb_set{});
    REQUIRE(empty_set->size() == 0u);
    require_hex(empty_set->to_data(), hex_n0);
    REQUIRE( ! empty_set->match(filter_key, absent_hash()));

    // CompactSize for 2^32, which does not fit in the uint32 width of N.
    auto const too_big = decode_base16("ff0000000001000000");
    REQUIRE(too_big);
    auto const overflow = chain::golomb_set::from_data(*too_big);
    REQUIRE( ! overflow);
    REQUIRE(overflow.error() == chain::golomb_set_error::too_many_items);

    // A longer-than-needed width for zero. The reader rejects it.
    auto const wide = decode_base16("fd0000");
    REQUIRE(wide);
    auto const non_canonical = chain::golomb_set::from_data(*wide);
    REQUIRE( ! non_canonical);
    REQUIRE(non_canonical.error() == chain::golomb_set_error::truncated);
}

TEST_CASE("match any is true when one item is a member and false otherwise", "[golomb set]") {
    auto const items = sha256_bytes(2);
    auto const built = chain::golomb_set::build(filter_key, items);
    REQUIRE(built);

    std::vector<data_chunk> const hit{absent_hash(), items[0]};
    REQUIRE(built->match_any(filter_key, hit));

    uint8_t const outsider = 9;
    std::vector<data_chunk> const miss{
        absent_hash(),
        to_chunk(sha256_hash(byte_span(&outsider, 1)))};
    REQUIRE( ! built->match_any(filter_key, miss));

    std::vector<data_chunk> const none;
    REQUIRE( ! built->match_any(filter_key, none));

    chain::golomb_set const empty;
    REQUIRE( ! empty.match(filter_key, items[0]));
    REQUIRE( ! empty.match_any(filter_key, hit));
}

TEST_CASE("reversing the items does not change the serialized set", "[golomb set]") {
    auto const forward = sha256_bytes(50);
    auto reversed = forward;
    std::reverse(reversed.begin(), reversed.end());

    auto const built_forward = chain::golomb_set::build(filter_key, forward);
    auto const built_reversed = chain::golomb_set::build(filter_key, reversed);
    REQUIRE(built_forward);
    REQUIRE(built_reversed);
    REQUIRE(built_forward->to_data() == built_reversed->to_data());
    require_hex(built_reversed->to_data(), hex_n50);
}
