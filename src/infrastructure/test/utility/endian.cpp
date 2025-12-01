// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: endian tests

// ============================================================================
// to_little_endian / to_big_endian tests
// ============================================================================

TEST_CASE("endian to little endian 32bit", "[endian tests]") {
    constexpr uint32_t value = 0x01020304;
    auto const bytes = to_little_endian(value);
    REQUIRE(bytes[0] == 0x04);
    REQUIRE(bytes[1] == 0x03);
    REQUIRE(bytes[2] == 0x02);
    REQUIRE(bytes[3] == 0x01);
}

TEST_CASE("endian to big endian 32bit", "[endian tests]") {
    constexpr uint32_t value = 0x01020304;
    auto const bytes = to_big_endian(value);
    REQUIRE(bytes[0] == 0x01);
    REQUIRE(bytes[1] == 0x02);
    REQUIRE(bytes[2] == 0x03);
    REQUIRE(bytes[3] == 0x04);
}

TEST_CASE("endian to little endian 64bit", "[endian tests]") {
    constexpr uint64_t value = 0x0102030405060708;
    auto const bytes = to_little_endian(value);
    REQUIRE(bytes[0] == 0x08);
    REQUIRE(bytes[7] == 0x01);
}

TEST_CASE("endian to big endian 64bit", "[endian tests]") {
    constexpr uint64_t value = 0x0102030405060708;
    auto const bytes = to_big_endian(value);
    REQUIRE(bytes[0] == 0x01);
    REQUIRE(bytes[7] == 0x08);
}

// ============================================================================
// from_little_endian / from_big_endian (fixed-size span) tests
// ============================================================================

TEST_CASE("endian from little endian span 32bit", "[endian tests]") {
    std::array<uint8_t, 4> const data = {0x04, 0x03, 0x02, 0x01};
    auto const value = from_little_endian<uint32_t>(std::span{data});
    REQUIRE(value == 0x01020304);
}

TEST_CASE("endian from big endian span 32bit", "[endian tests]") {
    std::array<uint8_t, 4> const data = {0x01, 0x02, 0x03, 0x04};
    auto const value = from_big_endian<uint32_t>(std::span{data});
    REQUIRE(value == 0x01020304);
}

TEST_CASE("endian from little endian span 64bit", "[endian tests]") {
    std::array<uint8_t, 8> const data = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    auto const value = from_little_endian<uint64_t>(std::span{data});
    REQUIRE(value == 0x0102030405060708);
}

TEST_CASE("endian from big endian span 64bit", "[endian tests]") {
    std::array<uint8_t, 8> const data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto const value = from_big_endian<uint64_t>(std::span{data});
    REQUIRE(value == 0x0102030405060708);
}

// ============================================================================
// from_*_unsafe (dynamic span) tests
// ============================================================================

TEST_CASE("endian from little endian unsafe one byte", "[endian tests]") {
    constexpr uint8_t expected = 0xff;
    auto const bytes = data_chunk{expected};
    REQUIRE(from_little_endian_unsafe<uint8_t>(bytes) == expected);
}

TEST_CASE("endian from big endian unsafe one byte", "[endian tests]") {
    constexpr uint8_t expected = 0xff;
    auto const bytes = data_chunk{expected};
    REQUIRE(from_big_endian_unsafe<uint8_t>(bytes) == expected);
}

TEST_CASE("endian from little endian unsafe 32bit", "[endian tests]") {
    data_chunk const data = {0x04, 0x03, 0x02, 0x01};
    auto const value = from_little_endian_unsafe<uint32_t>(data);
    REQUIRE(value == 0x01020304);
}

TEST_CASE("endian from big endian unsafe 32bit", "[endian tests]") {
    data_chunk const data = {0x01, 0x02, 0x03, 0x04};
    auto const value = from_big_endian_unsafe<uint32_t>(data);
    REQUIRE(value == 0x01020304);
}

// ============================================================================
// Round-trip tests
// ============================================================================

TEST_CASE("endian round trip32 little to big", "[endian tests]") {
    constexpr uint32_t expected = 123456789u;
    auto little_endian = to_little_endian(expected);
    REQUIRE(from_little_endian_unsafe<uint32_t>(little_endian) == expected);

    std::reverse(little_endian.begin(), little_endian.end());
    REQUIRE(from_big_endian_unsafe<uint32_t>(little_endian) == expected);
}

TEST_CASE("endian round trip32 big to little", "[endian tests]") {
    constexpr uint32_t expected = 123456789u;
    auto big_endian = to_big_endian(expected);
    REQUIRE(from_big_endian_unsafe<uint32_t>(big_endian) == expected);

    std::reverse(big_endian.begin(), big_endian.end());
    REQUIRE(from_little_endian_unsafe<uint32_t>(big_endian) == expected);
}

TEST_CASE("endian round trip32 big to big", "[endian tests]") {
    constexpr uint32_t expected = 123456789u;
    auto const big_endian = to_big_endian(expected);
    REQUIRE(from_big_endian_unsafe<uint32_t>(big_endian) == expected);
}

TEST_CASE("endian round trip32 little to little", "[endian tests]") {
    constexpr uint32_t expected = 123456789u;
    auto const little_endian = to_little_endian(expected);
    REQUIRE(from_little_endian_unsafe<uint32_t>(little_endian) == expected);
}

TEST_CASE("endian round trip64 little to little", "[endian tests]") {
    constexpr uint64_t expected = 0x1122334455667788;
    auto const little_endian = to_little_endian(expected);
    REQUIRE(from_little_endian_unsafe<uint64_t>(little_endian) == expected);
}

TEST_CASE("endian round trip64 big to big", "[endian tests]") {
    constexpr uint64_t expected = 0x1122334455667788;
    auto const big_endian = to_big_endian(expected);
    REQUIRE(from_big_endian_unsafe<uint64_t>(big_endian) == expected);
}

// ============================================================================
// constexpr tests (compile-time evaluation)
// ============================================================================

TEST_CASE("endian constexpr to little endian", "[endian tests]") {
    constexpr auto bytes = to_little_endian<uint32_t>(0x01020304);
    static_assert(bytes[0] == 0x04);
    static_assert(bytes[3] == 0x01);
    REQUIRE(bytes[0] == 0x04);
}

TEST_CASE("endian constexpr to big endian", "[endian tests]") {
    constexpr auto bytes = to_big_endian<uint32_t>(0x01020304);
    static_assert(bytes[0] == 0x01);
    static_assert(bytes[3] == 0x04);
    REQUIRE(bytes[0] == 0x01);
}

// End Test Suite
