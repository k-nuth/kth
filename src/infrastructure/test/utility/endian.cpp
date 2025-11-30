// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: endian tests

TEST_CASE("endian from little endian unsafe one byte expected", "[endian tests]") {
    static uint8_t const expected = 0xff;
    static auto const bytes = data_chunk{ expected };
    REQUIRE(from_little_endian_unsafe<uint8_t>(bytes.begin()) == expected);
}

TEST_CASE("endian from big endian unsafe one byte expected", "[endian tests]") {
    static uint8_t const expected = 0xff;
    static auto const bytes = data_chunk{ expected };
    REQUIRE(from_big_endian_unsafe<uint8_t>(bytes.begin()) == expected);
}

TEST_CASE("endian round trip32 little to big expected", "[endian tests]") {
    static const uint32_t expected = 123456789u;
    auto little_endian = to_little_endian<uint32_t>(expected);
    REQUIRE(from_little_endian_unsafe<uint32_t>(little_endian.begin()) == expected);

    std::reverse(little_endian.begin(), little_endian.end());
    REQUIRE(from_big_endian_unsafe<uint32_t>(little_endian.begin()) == expected);
}

TEST_CASE("endian round trip32 big to little expected", "[endian tests]") {
    static const uint32_t expected = 123456789u;
    auto big_endian = to_big_endian<uint32_t>(expected);
    REQUIRE(from_big_endian_unsafe<uint32_t>(big_endian.begin()) == expected);

    std::reverse(big_endian.begin(), big_endian.end());
    REQUIRE(from_little_endian_unsafe<uint32_t>(big_endian.begin()) == expected);
}

TEST_CASE("endian round trip32 big to big expected", "[endian tests]") {
    static const uint32_t expected = 123456789u;
    auto const big_endian = to_big_endian<uint32_t>(expected);
    REQUIRE(from_big_endian_unsafe<uint32_t>(big_endian.begin()) == expected);
}

TEST_CASE("endian round trip32 little to little expected", "[endian tests]") {
    static const uint32_t expected = 123456789u;
    auto const little_endian = to_little_endian<uint32_t>(expected);
    REQUIRE(from_little_endian_unsafe<uint32_t>(little_endian.begin()) == expected);
}

TEST_CASE("endian round trip64 little to little expected", "[endian tests]") {
    static uint64_t const expected = 0x1122334455667788;
    auto const little_endian = to_little_endian<uint64_t>(expected);
    REQUIRE(from_little_endian_unsafe<uint64_t>(little_endian.begin()) == expected);
}

TEST_CASE("endian round trip64 big to big expected", "[endian tests]") {
    static uint64_t const expected = 0x1122334455667788;
    auto const big_endian = to_big_endian<uint64_t>(expected);
    REQUIRE(from_big_endian_unsafe<uint64_t>(big_endian.begin()) == expected);
}

// End Test Suite
