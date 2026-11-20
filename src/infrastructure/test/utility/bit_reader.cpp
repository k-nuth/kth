// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <vector>

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure;

namespace {

// The definition, one bit at a time: bit `index` of the stream is bit
// 7 - index % 8 of byte index / 8.
bool bit_at(data_chunk const& bytes, size_t index) {
    return ((bytes[index / 8] >> (7 - index % 8)) & 1u) != 0;
}

uint64_t bits_at(data_chunk const& bytes, size_t index, size_t count) {
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        value = (value << 1) | (bit_at(bytes, index + i) ? 1u : 0u);
    }
    return value;
}

// Bytes with no pattern a reader could get right by accident.
data_chunk scrambled(size_t size) {
    data_chunk out(size);
    uint32_t state = 0x9e3779b9u;
    for (auto& b : out) {
        state = state * 1664525u + 1013904223u;
        b = uint8_t(state >> 24);
    }
    return out;
}

} // namespace

// Start Test Suite: bit_reader tests

TEST_CASE("bit_reader - an empty buffer has no bit to read", "[bit_reader tests]") {
    data_chunk const buffer{};
    bit_reader reader(buffer);
    REQUIRE(reader.is_exhausted());
    REQUIRE(reader.remaining_bits() == 0u);

    auto const bit = reader.read_bit();
    REQUIRE( ! bit);
    REQUIRE(bit.error() == error::read_past_end_of_buffer);
    REQUIRE( ! reader.read_bits(1));
    REQUIRE( ! reader.read_unary());

    // Zero bits are there to read even so.
    auto const none = reader.read_bits(0);
    REQUIRE(none);
    REQUIRE(*none == 0u);
}

TEST_CASE("bit_reader - the first bit of a byte is its most significant", "[bit_reader tests]") {
    data_chunk const buffer{0b1000'0001, 0b0100'0000};
    bit_reader reader(buffer);
    REQUIRE(reader.remaining_bits() == 16u);

    std::vector<bool> read;
    for (size_t i = 0; i < 16; ++i) {
        auto const bit = reader.read_bit();
        REQUIRE(bit);
        read.push_back(*bit);
    }
    REQUIRE(read == std::vector<bool>{
        true, false, false, false, false, false, false, true,
        false, true, false, false, false, false, false, false});
    REQUIRE(reader.is_exhausted());
    REQUIRE( ! reader.read_bit());
}

TEST_CASE("bit_reader - read_bits gives a number whose top bit was read first", "[bit_reader tests]") {
    data_chunk const buffer{0xa5, 0x3c, 0xff, 0x00, 0x81};
    bit_reader reader(buffer);

    auto const nibble = reader.read_bits(4);
    REQUIRE(nibble);
    REQUIRE(*nibble == 0xau);
    // Across the end of the first byte.
    auto const across = reader.read_bits(8);
    REQUIRE(across);
    REQUIRE(*across == 0x53u);
    REQUIRE(reader.remaining_bits() == 28u);
    auto const rest = reader.read_bits(28);
    REQUIRE(rest);
    REQUIRE(*rest == 0xcff0081u);
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("bit_reader - every width from 0 to 64 at every bit offset", "[bit_reader tests]") {
    // Long enough for the eight-byte loads and for the bytes read one by one
    // at the end of the buffer.
    auto const buffer = scrambled(24);
    for (size_t offset = 0; offset < 17; ++offset) {
        for (size_t width = 0; width <= bit_reader::max_bits_per_read; ++width) {
            CAPTURE(offset, width);
            bit_reader reader(buffer);
            REQUIRE(reader.read_bits(offset));
            auto const value = reader.read_bits(width);
            REQUIRE(value);
            REQUIRE(*value == bits_at(buffer, offset, width));
            REQUIRE(reader.remaining_bits() == buffer.size() * 8 - offset - width);
        }
    }
}

TEST_CASE("bit_reader - a sequence of reads of mixed widths, for every buffer length", "[bit_reader tests]") {
    for (size_t size = 1; size <= 40; ++size) {
        CAPTURE(size);
        auto const buffer = scrambled(size);
        bit_reader reader(buffer);
        size_t index = 0;
        size_t width = 1;
        while (index + width <= size * 8) {
            auto const value = reader.read_bits(width);
            REQUIRE(value);
            REQUIRE(*value == bits_at(buffer, index, width));
            index += width;
            width = width % 64 + 1 + (index % 3);
            if (width > 64) width = 64;
        }
        REQUIRE(reader.remaining_bits() == size * 8 - index);
    }
}

TEST_CASE("bit_reader - a read past the end fails and consumes nothing", "[bit_reader tests]") {
    data_chunk const buffer{0xde, 0xad};
    bit_reader reader(buffer);
    REQUIRE(reader.read_bits(5));

    auto const too_many = reader.read_bits(12);
    REQUIRE( ! too_many);
    REQUIRE(too_many.error() == error::read_past_end_of_buffer);
    REQUIRE(reader.remaining_bits() == 11u);

    auto const rest = reader.read_bits(11);
    REQUIRE(rest);
    REQUIRE(*rest == 0x6adu);
}

TEST_CASE("bit_reader - more bits than one read hands out is refused", "[bit_reader tests]") {
    auto const buffer = scrambled(32);
    bit_reader reader(buffer);
    REQUIRE( ! reader.read_bits(bit_reader::max_bits_per_read + 1));
    REQUIRE(reader.remaining_bits() == 256u);
}

TEST_CASE("bit_reader - read_unary counts the 1 bits up to a 0 bit and consumes it", "[bit_reader tests]") {
    // 0 | 10 | 1110 | 11111111 0 | then 1 and padding.
    data_chunk const buffer{0b0101'1101, 0b1111'1110, 0b1000'0000};
    bit_reader reader(buffer);
    for (auto const expected : {0u, 1u, 3u, 8u}) {
        auto const run = reader.read_unary();
        REQUIRE(run);
        REQUIRE(*run == expected);
    }
    REQUIRE(reader.remaining_bits() == 8u);
    auto const next = reader.read_bit();
    REQUIRE(next);
    REQUIRE(*next);
}

TEST_CASE("bit_reader - read_unary follows a run across any number of bytes", "[bit_reader tests]") {
    // Runs shorter than, as long as and longer than the reader's window, each
    // starting at every offset inside a byte.
    for (size_t offset = 0; offset < 8; ++offset) {
        for (size_t ones : {0u, 1u, 7u, 8u, 55u, 56u, 57u, 63u, 64u, 65u, 127u, 128u, 300u}) {
            CAPTURE(offset, ones);
            auto const bits = offset + ones + 1 + 5;
            data_chunk buffer(bytes_for_bits(bits), 0);
            for (size_t i = offset; i < offset + ones; ++i) {
                buffer[i / 8] = uint8_t(buffer[i / 8] | (1u << (7 - i % 8)));
            }
            // A 1 bit right after the closing 0, which must not be counted.
            auto const after = offset + ones + 1;
            buffer[after / 8] = uint8_t(buffer[after / 8] | (1u << (7 - after % 8)));

            bit_reader reader(buffer);
            REQUIRE(reader.read_bits(offset));
            auto const run = reader.read_unary();
            REQUIRE(run);
            REQUIRE(*run == ones);
            auto const next = reader.read_bit();
            REQUIRE(next);
            REQUIRE(*next);
        }
    }
}

TEST_CASE("bit_reader - read_unary fails when the stream ends inside the run", "[bit_reader tests]") {
    for (size_t size : {1u, 7u, 8u, 9u, 16u, 17u}) {
        CAPTURE(size);
        data_chunk const buffer(size, 0xff);
        bit_reader reader(buffer);
        auto const run = reader.read_unary();
        REQUIRE( ! run);
        REQUIRE(run.error() == error::read_past_end_of_buffer);
    }
}

// End Test Suite
