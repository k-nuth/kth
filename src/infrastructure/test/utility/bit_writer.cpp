// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <limits>
#include <vector>

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure;

// Start Test Suite: bit_writer tests

TEST_CASE("bit_writer - bytes_for_bits completes the last byte", "[bit_writer tests]") {
    REQUIRE(bytes_for_bits(0) == 0u);
    REQUIRE(bytes_for_bits(1) == 1u);
    REQUIRE(bytes_for_bits(8) == 1u);
    REQUIRE(bytes_for_bits(9) == 2u);
    REQUIRE(bytes_for_bits(64) == 8u);
    REQUIRE(bytes_for_bits(65) == 9u);
}

TEST_CASE("bit_writer - an empty buffer takes no bit", "[bit_writer tests]") {
    data_chunk buffer{};
    bit_writer writer(buffer);
    REQUIRE(writer.is_exhausted());
    REQUIRE(writer.remaining_bits() == 0u);
    REQUIRE(writer.bits_written() == 0u);

    auto const bit = writer.write_bit(true);
    REQUIRE( ! bit);
    REQUIRE(bit.error() == error::write_past_end_of_buffer);
    REQUIRE( ! writer.write_bits(1, 1));
    REQUIRE( ! writer.write_unary(0));
    // Zero bits always fit.
    REQUIRE(writer.write_bits(0xff, 0));
    writer.flush();
    REQUIRE(writer.bits_written() == 0u);
}

TEST_CASE("bit_writer - the first bit written is the most significant of its byte", "[bit_writer tests]") {
    data_chunk buffer(2, 0xff);
    bit_writer writer(buffer);
    for (bool const bit : {true, false, false, false, false, false, false, true, false, true}) {
        REQUIRE(writer.write_bit(bit));
    }
    REQUIRE(writer.bits_written() == 10u);
    REQUIRE(writer.remaining_bits() == 6u);
    // The second byte is stored by flush, its unwritten bits as zeros whatever
    // the buffer held.
    REQUIRE(buffer[0] == 0b1000'0001);
    writer.flush();
    REQUIRE(buffer[1] == 0b0100'0000);
}

TEST_CASE("bit_writer - write_bits stores the low bits of the value, the top one first", "[bit_writer tests]") {
    data_chunk buffer(5, 0);
    bit_writer writer(buffer);
    REQUIRE(writer.write_bits(0xa, 4));
    REQUIRE(writer.write_bits(0x53, 8));
    // The bits above the 28 asked for are not part of the stream.
    REQUIRE(writer.write_bits(0xfffffffffcff0081u, 28));
    REQUIRE(writer.is_exhausted());
    writer.flush();
    REQUIRE(buffer == data_chunk{0xa5, 0x3c, 0xff, 0x00, 0x81});
}

TEST_CASE("bit_writer - a write that does not fit fails and writes nothing", "[bit_writer tests]") {
    data_chunk buffer(2, 0);
    bit_writer writer(buffer);
    REQUIRE(writer.write_bits(0x1b, 5));

    auto const too_many = writer.write_bits(0xfff, 12);
    REQUIRE( ! too_many);
    REQUIRE(too_many.error() == error::write_past_end_of_buffer);
    REQUIRE(writer.bits_written() == 5u);
    REQUIRE( ! writer.write_unary(11));
    REQUIRE(writer.bits_written() == 5u);

    REQUIRE(writer.write_bits(0x6ad, 11));
    writer.flush();
    REQUIRE(buffer == data_chunk{0xde, 0xad});
}

TEST_CASE("bit_writer - more bits than one write takes is refused", "[bit_writer tests]") {
    data_chunk buffer(32, 0);
    bit_writer writer(buffer);
    REQUIRE( ! writer.write_bits(0, bit_writer::max_bits_per_write + 1));
    REQUIRE(writer.bits_written() == 0u);
}

TEST_CASE("bit_writer - a count of ones that would wrap is refused", "[bit_writer tests]") {
    data_chunk buffer(4, 0);
    bit_writer writer(buffer);
    REQUIRE( ! writer.write_unary(std::numeric_limits<uint64_t>::max()));
    REQUIRE(writer.bits_written() == 0u);
}

TEST_CASE("bit_writer - write_unary stores the 1 bits and the 0 bit that ends them", "[bit_writer tests]") {
    data_chunk buffer(3, 0);
    bit_writer writer(buffer);
    for (auto const ones : {0u, 1u, 3u, 8u}) {
        REQUIRE(writer.write_unary(ones));
    }
    REQUIRE(writer.write_bit(true));
    writer.flush();
    REQUIRE(buffer == data_chunk{0b0101'1101, 0b1111'1110, 0b1000'0000});
}

TEST_CASE("bit_writer - flush goes on at the next byte and leaves the rest alone", "[bit_writer tests]") {
    data_chunk buffer(4, 0xee);
    bit_writer writer(buffer);
    REQUIRE(writer.write_bits(0b101, 3));
    writer.flush();
    REQUIRE(writer.bits_written() == 8u);
    // Nothing pending: a second flush stores nothing.
    writer.flush();
    REQUIRE(writer.bits_written() == 8u);
    REQUIRE(writer.write_bits(0xab, 8));
    writer.flush();
    REQUIRE(buffer == data_chunk{0b1010'0000, 0xab, 0xee, 0xee});
}

TEST_CASE("bit_writer - what it writes is what bit_reader reads, for every width and run", "[bit_writer tests]") {
    struct field {
        bool unary;
        uint64_t value;
        size_t width;
    };
    // Widths 0 to 64 with values that use every bit, and runs around the sizes
    // of a byte, of half the window and of the window.
    std::vector<field> fields;
    uint64_t state = 0x243f6a8885a308d3u;
    size_t bits = 0;
    for (size_t round = 0; round < 4; ++round) {
        for (size_t width = 0; width <= 64; ++width) {
            state = state * 6364136223846793005u + 1442695040888963407u;
            auto const value = width == 64 ? state : state & ((uint64_t{1} << width) - 1);
            fields.push_back({false, value, width});
            bits += width;
            if (width % 5 == round) {
                auto const ones = (state >> 40) % 3 == 0 ? width * 3 : width / 2;
                fields.push_back({true, ones, 0});
                bits += ones + 1;
            }
        }
    }

    data_chunk buffer(bytes_for_bits(bits), 0);
    bit_writer writer(buffer);
    for (auto const& f : fields) {
        if (f.unary) {
            REQUIRE(writer.write_unary(f.value));
        } else {
            REQUIRE(writer.write_bits(f.value, f.width));
        }
    }
    REQUIRE(writer.bits_written() == bits);
    writer.flush();

    bit_reader reader(buffer);
    for (auto const& f : fields) {
        CAPTURE(f.unary, f.value, f.width);
        auto const read = f.unary ? reader.read_unary() : reader.read_bits(f.width);
        REQUIRE(read);
        REQUIRE(*read == f.value);
    }
    // Only the padding of the last byte is left, and it is zeros.
    REQUIRE(reader.remaining_bits() < 8u);
    auto const padding = reader.read_bits(reader.remaining_bits());
    REQUIRE(padding);
    REQUIRE(*padding == 0u);
}

// End Test Suite
