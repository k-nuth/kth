// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: binary tests

// Start Test Suite: binary  encoded

TEST_CASE("infrastructure binary encoded 32 bits from data chunk", "[infrastructure][binary]") {
    data_chunk const blocks{ { 0xba, 0xad, 0xf0, 0x0d } };
    binary const prefix(32, blocks);
    REQUIRE(prefix.encoded() == "10111010101011011111000000001101");
}

TEST_CASE("infrastructure binary encoded 32 bits from unsigned integer", "[infrastructure][binary]") {
    binary const prefix(32, uint32_t(0x0df0adba));
    REQUIRE(prefix.encoded() == "10111010101011011111000000001101");
}
TEST_CASE("infrastructure binary encoded 8 bits from unsigned integer", "[infrastructure][binary]") {
    binary const prefix(8, uint32_t(0x0df0adba));
    REQUIRE(prefix.encoded() == "10111010");
}

// End Test Suite

// Start Test Suite: binary  to string

TEST_CASE("infrastructure binary to string stream output", "[infrastructure][binary]") {
    data_chunk const blocks{ { 0xba, 0xad, 0xf0, 0x0d } };
    binary const prefix(32, blocks);
    std::stringstream stream;
    stream << prefix;
    REQUIRE(stream.str() == "10111010101011011111000000001101");
}

// End Test Suite

// Start Test Suite: binary  shift left

TEST_CASE("infrastructure binary shift left by zero", "[infrastructure][binary]") {
    binary::size_type distance = 0;
    binary instance(16, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(16, data_chunk{ 0xAA, 0xAA });
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift left alternate initial", "[binary  shift left]") {
    binary::size_type distance = 16;
    binary instance(24, data_chunk{ 0xAB, 0xCD, 0xEF });
    binary expected(8, data_chunk{ 0xEF });
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift left by less than byte", "[binary  shift left]") {
    binary::size_type distance = 5;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(19, data_chunk{ 0x55, 0x55, 0x40});
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift left by less than byte to byte align", "[binary  shift left]") {
    binary::size_type distance = 5;
    binary instance(21, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(16, data_chunk{ 0x55, 0x55 });
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift left by byte single", "[binary  shift left]") {
    binary::size_type distance = 8;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(16, data_chunk{ 0xAA, 0xAA });
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift left by greater than byte", "[binary  shift left]") {
    binary::size_type distance = 13;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(11, data_chunk{ 0x55, 0x40 });
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift left by greater than byte not initially aligned", "[binary  shift left]") {
    binary::size_type distance = 13;
    binary instance(18, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(5, data_chunk{ 0x50 });
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift left by byte multiple", "[binary  shift left]") {
    binary::size_type distance = 16;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(8, data_chunk{ 0xAA });
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift left by size", "[binary  shift left]") {
    binary::size_type distance = 24;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(0, data_chunk{});
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift left by greater than size", "[binary  shift left]") {
    binary::size_type distance = 30;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(0, data_chunk{});
    instance.shift_left(distance);
    REQUIRE(expected == instance);
}

// End Test Suite

// Start Test Suite: binary  shift right

TEST_CASE("shift right by zero", "[binary  shift right]") {
    binary::size_type distance = 0;
    binary instance(16, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(16, data_chunk{ 0xAA, 0xAA });
    instance.shift_right(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift right by less than byte", "[binary  shift right]") {
    binary::size_type distance = 5;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(29, data_chunk{ 0x05, 0x55, 0x55, 0x50});
    instance.shift_right(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift right by less than byte to byte align", "[binary  shift right]") {
    binary::size_type distance = 3;
    binary instance(21, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(24, data_chunk{ 0x15, 0x55, 0x55 });
    instance.shift_right(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift right by byte single", "[binary  shift right]") {
    binary::size_type distance = 8;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(32, data_chunk{ 0x00, 0xAA, 0xAA, 0xAA });
    instance.shift_right(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift right by greater than byte", "[binary  shift right]") {
    binary::size_type distance = 13;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(37, data_chunk{ 0x00, 0x05, 0x55, 0x55, 0x50 });
    instance.shift_right(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift right by greater than byte not initially aligned", "[binary  shift right]") {
    binary::size_type distance = 13;
    binary instance(18, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(31, data_chunk{ 0x00, 0x05, 0x55, 0x54 });
    instance.shift_right(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift right by byte multiple", "[binary  shift right]") {
    binary::size_type distance = 16;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(40, data_chunk{ 0x00, 0x00, 0xAA, 0xAA, 0xAA });
    instance.shift_right(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift right by size", "[binary  shift right]") {
    binary::size_type distance = 24;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(48, data_chunk{ 0x00, 0x00, 0x00, 0xAA, 0xAA, 0xAA });
    instance.shift_right(distance);
    REQUIRE(expected == instance);
}

TEST_CASE("shift right by greater than size", "[binary  shift right]") {
    binary::size_type distance = 30;
    binary instance(24, data_chunk{ 0xAA, 0xAA, 0xAA });
    binary expected(54, data_chunk{ 0x00, 0x00, 0x00, 0x02, 0xAA, 0xAA, 0xA8 });
    instance.shift_right(distance);
    REQUIRE(expected == instance);
}

// End Test Suite

// Start Test Suite: binary  append

TEST_CASE("append to zero length", "[binary  append]") {
    binary instance(0, data_chunk{ 0x00 });
    binary augment(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary expected(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    instance.append(augment);
    REQUIRE(expected == instance);
}

TEST_CASE("append zero length to content", "[binary  append]") {
    binary instance(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary augment(0, data_chunk{});
    binary expected(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    instance.append(augment);
    REQUIRE(expected == instance);
}

TEST_CASE("append byte aligned instances", "[binary  append]") {
    binary instance(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary augment(24, data_chunk{ 0xDD, 0xEE, 0xFF });
    binary expected(48, data_chunk{ 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF });
    instance.append(augment);
    REQUIRE(expected == instance);
}

TEST_CASE("append byte nonaligned instances", "[binary  append]") {
    binary instance(20, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary augment(13, data_chunk{ 0xDD, 0xEE });
    binary expected(33, data_chunk{ 0xAA, 0xBB, 0xCD, 0xDE, 0x80 });
    instance.append(augment);
    REQUIRE(expected == instance);
}

//
// prepend tests
//
TEST_CASE("prepend to zero length", "[binary  append]") {
    binary instance(0, data_chunk{ 0x00 });
    binary augment(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary expected(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    instance.prepend(augment);
    REQUIRE(expected == instance);
}

TEST_CASE("prepend zero length to content", "[binary  append]") {
    binary instance(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary augment(0, data_chunk{});
    binary expected(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    instance.prepend(augment);
    REQUIRE(expected == instance);
}

TEST_CASE("prepend byte aligned instances", "[binary  append]") {
    binary instance(24, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary augment(24, data_chunk{ 0xDD, 0xEE, 0xFF });
    binary expected(48, data_chunk{ 0xDD, 0xEE, 0xFF, 0xAA, 0xBB, 0xCC });
    instance.prepend(augment);
    REQUIRE(expected == instance);
}

TEST_CASE("prepend byte nonaligned instances", "[binary  append]") {
    binary instance(20, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary augment(13, data_chunk{ 0xDD, 0xEE });
    binary expected(33, data_chunk{ 0xDD, 0xED, 0x55, 0xDE, 0x00 });
    instance.prepend(augment);
    REQUIRE(expected == instance);
}

// End Test Suite

// Start Test Suite: binary  substring

TEST_CASE("substring start after end", "[binary  substring]") {
    binary instance(20, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary::size_type start = 21;
    binary::size_type length = 10;
    binary expected(0, data_chunk{});
    binary result = instance.substring(start, length);
    REQUIRE(expected == result);
}

TEST_CASE("substring length zero", "[binary  substring]") {
    binary instance(20, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary::size_type start = 5;
    binary::size_type length = 0;
    binary expected(0, data_chunk{});
    binary result = instance.substring(start, length);
    REQUIRE(expected == result);
}

TEST_CASE("substring byte aligned start", "[binary  substring]") {
    binary instance(20, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary::size_type start = 8;
    binary::size_type length = 10;
    binary expected(10, data_chunk{ 0xBB, 0xC0 });
    binary result = instance.substring(start, length);
    REQUIRE(expected == result);
}

TEST_CASE("substring byte nonaligned start", "[binary  substring]") {
    binary instance(20, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary::size_type start = 10;
    binary::size_type length = 10;
    binary expected(10, data_chunk{ 0xEF, 0x00 });
    binary result = instance.substring(start, length);
    REQUIRE(expected == result);
}

TEST_CASE("substring request exceeds string", "[binary  substring]") {
    binary instance(20, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary::size_type start = 10;
    binary::size_type length = 15;
    binary expected(10, data_chunk{ 0xEF, 0x00 });
    binary result = instance.substring(start, length);
    REQUIRE(expected == result);
}

TEST_CASE("substring implicit length", "[binary  substring]") {
    binary instance(20, data_chunk{ 0xAA, 0xBB, 0xCC });
    binary::size_type start = 10;
    binary expected(10, data_chunk{ 0xEF, 0x00 });
    binary result = instance.substring(start);
    REQUIRE(expected == result);
}

// End Test Suite

// Start Test Suite: binary  blocks

TEST_CASE("string to prefix  32 bits  expected value", "[binary  blocks]") {
    data_chunk const blocks{ { 0xba, 0xad, 0xf0, 0x0d } };
    binary const prefix("10111010101011011111000000001101");
    REQUIRE(prefix.blocks() == blocks);
}

TEST_CASE("prefix to bytes  32 bits  expected value", "[binary  blocks]") {
    data_chunk const blocks{ { 0xba, 0xad, 0xf0, 0x0d } };
    binary const prefix(32, blocks);
    REQUIRE(prefix.blocks() == blocks);
}

TEST_CASE("bytes to prefix  zero bits  round trips", "[binary  blocks]") {
    data_chunk const bytes;
    binary const prefix(0, bytes);
    std::stringstream stream;
    stream << prefix;
    REQUIRE(prefix.size() == 0u);
    REQUIRE(prefix.blocks().size() == 0u);
    REQUIRE(stream.str().empty());
}

TEST_CASE("prefix to bytes  zero bits  round trips", "[binary  blocks]") {
    data_chunk const blocks{ { 0x00, 0x00, 0x00, 0x00 } };
    binary const prefix(0, blocks);
    auto const bytes = prefix.blocks();
    std::stringstream stream;
    stream << prefix;
    REQUIRE(prefix.size() == 0u);
    REQUIRE(prefix.blocks().size() == 0u);
    REQUIRE(bytes.size() == 0u);
    REQUIRE(stream.str().empty());
}

TEST_CASE("bytes to prefix  one bit  round trips", "[binary  blocks]") {
    data_chunk bytes{ { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } };
    auto prefix = binary(1, bytes);
    std::stringstream stream;
    stream << prefix;
    REQUIRE(prefix.size() == 1u);
    REQUIRE(prefix.blocks().size() == 1u);
    REQUIRE(stream.str() == "1");
}

TEST_CASE("prefix to bytes  one bit  round trips", "[binary  blocks]") {
    data_chunk const blocks{ { 0xff, 0xff, 0xff, 0xff } };
    binary const prefix(1, blocks);
    auto const bytes = prefix.blocks();
    std::stringstream stream;
    stream << prefix;
    REQUIRE(prefix.size() == 1u);
    REQUIRE(prefix.blocks().size() == 1u);
    REQUIRE(bytes.size() == 1u);
    REQUIRE(stream.str() == "1");
}

TEST_CASE("bytes to prefix  two bits leading zero  round trips", "[binary  blocks]") {
    data_chunk const bytes{ { 0x01, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42 } };
    auto const prefix = binary(2, bytes);
    std::stringstream stream;
    stream << prefix;
    REQUIRE(prefix.size() == 2u);
    REQUIRE(prefix.blocks().size() == 1u);
    REQUIRE(stream.str() == "00");
}

TEST_CASE("prefix to bytes  two bits leading zero  round trips", "[binary  blocks]") {
    data_chunk const blocks{ { 0x42, 0x42, 0x42, 0x01 } };
    binary const prefix(2, blocks);
    auto bytes = prefix.blocks();
    std::stringstream stream;
    stream << prefix;
    REQUIRE(prefix.size() == 2u);
    REQUIRE(prefix.blocks().size() == 1u);
    REQUIRE(bytes.size() == 1u);
    REQUIRE(stream.str() == "01");
}

TEST_CASE("bytes to prefix  two bytes leading null byte  round trips", "[binary  blocks]") {
    data_chunk const bytes{ { 0xFF, 0x00 } };
    auto const prefix = binary(16, bytes);
    std::stringstream stream;
    stream << prefix;
    REQUIRE(prefix.size() == 16u);
    REQUIRE(prefix.blocks().size() == 2u);
    REQUIRE(stream.str() == "1111111100000000");
}

TEST_CASE("prefix to bytes  two bytes leading null byte  round trips", "[binary  blocks]") {
    data_chunk const blocks{ { 0x00, 0x00 } };
    binary const prefix(16, blocks);
    auto bytes = prefix.blocks();
    std::stringstream stream;
    stream << prefix;
    REQUIRE(prefix.size() == 16u);
    REQUIRE(prefix.blocks().size() == 2u);
    REQUIRE(bytes.size() == 2u);
    REQUIRE(stream.str() == "0000000000000000");
}

// End Test Suite

// End Test Suite
