// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure;

// Start Test Suite: byte_reader tests

TEST_CASE("byte_reader - is_exhausted on empty buffer returns true", "[byte_reader tests]") {
    data_chunk const buffer{};
    byte_reader reader(buffer);
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("byte_reader - is_exhausted on nonempty buffer returns false", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03};
    byte_reader reader(buffer);
    REQUIRE( ! reader.is_exhausted());
}

TEST_CASE("byte_reader - peek_byte does not advance position", "[byte_reader tests]") {
    uint8_t const expected = 0xAA;
    data_chunk const buffer{expected, 0xBB};
    byte_reader reader(buffer);

    auto result1 = reader.peek_byte();
    REQUIRE(result1.has_value());
    REQUIRE(*result1 == expected);

    auto result2 = reader.peek_byte();
    REQUIRE(result2.has_value());
    REQUIRE(*result2 == expected);

    auto result3 = reader.peek_byte();
    REQUIRE(result3.has_value());
    REQUIRE(*result3 == expected);

    REQUIRE(reader.position() == 0);
}

TEST_CASE("byte_reader - read_byte advances position", "[byte_reader tests]") {
    uint8_t const expected = 0xAA;
    data_chunk const buffer{expected, 0xBB};
    byte_reader reader(buffer);

    auto const result = reader.read_byte();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
    REQUIRE(reader.position() == 1);
}

TEST_CASE("byte_reader - read_byte past end returns error", "[byte_reader tests]") {
    data_chunk const buffer{0xAA};
    byte_reader reader(buffer);

    auto result1 = reader.read_byte();
    REQUIRE(result1.has_value());

    auto result2 = reader.read_byte();
    REQUIRE( ! result2.has_value());
    REQUIRE(result2.error() == error::read_past_end_of_buffer);
}

TEST_CASE("byte_reader - read 2 bytes little endian", "[byte_reader tests]") {
    uint16_t const expected = 43707;  // 0xAABB in little endian: BB AA
    data_chunk const buffer{0xBB, 0xAA};
    byte_reader reader(buffer);

    auto const result = reader.read_little_endian<uint16_t>();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read 4 bytes little endian", "[byte_reader tests]") {
    uint32_t const expected = 2898120443;  // 0xACBDCEFB
    data_chunk const buffer{0xFB, 0xCE, 0xBD, 0xAC};  // little endian
    byte_reader reader(buffer);

    auto const result = reader.read_little_endian<uint32_t>();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read 8 bytes little endian", "[byte_reader tests]") {
    uint64_t const expected = 0xd4b14be5d8f02abe;
    data_chunk const buffer{0xbe, 0x2a, 0xf0, 0xd8, 0xe5, 0x4b, 0xb1, 0xd4};
    byte_reader reader(buffer);

    auto const result = reader.read_little_endian<uint64_t>();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read 2 bytes big endian", "[byte_reader tests]") {
    uint16_t const expected = 43707;  // 0xAABB
    data_chunk const buffer{0xAA, 0xBB};
    byte_reader reader(buffer);

    auto const result = reader.read_big_endian<uint16_t>();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read 4 bytes big endian", "[byte_reader tests]") {
    uint32_t const expected = 2898120443;  // 0xACBDCEFB
    data_chunk const buffer{0xAC, 0xBD, 0xCE, 0xFB};  // big endian
    byte_reader reader(buffer);

    auto const result = reader.read_big_endian<uint32_t>();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read 8 bytes big endian", "[byte_reader tests]") {
    uint64_t const expected = 0xd4b14be5d8f02abe;
    data_chunk const buffer{0xd4, 0xb1, 0x4b, 0xe5, 0xd8, 0xf0, 0x2a, 0xbe};
    byte_reader reader(buffer);

    auto const result = reader.read_big_endian<uint64_t>();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read variable little endian 1 byte", "[byte_reader tests]") {
    uint64_t const expected = 0xAA;
    data_chunk const buffer{0xAA};
    byte_reader reader(buffer);

    auto const result = reader.read_variable_little_endian();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read variable little endian 2 bytes", "[byte_reader tests]") {
    uint64_t const expected = 43707;  // 0xAABB
    // varint_two_bytes (0xFD) followed by little endian uint16
    data_chunk const buffer{0xFD, 0xBB, 0xAA};
    byte_reader reader(buffer);

    auto const result = reader.read_variable_little_endian();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read variable little endian 4 bytes", "[byte_reader tests]") {
    uint64_t const expected = 2898120443;  // 0xACBDCEFB
    // varint_four_bytes (0xFE) followed by little endian uint32
    data_chunk const buffer{0xFE, 0xFB, 0xCE, 0xBD, 0xAC};
    byte_reader reader(buffer);

    auto const result = reader.read_variable_little_endian();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read variable little endian 8 bytes", "[byte_reader tests]") {
    uint64_t const expected = 0xd4b14be5d8f02abe;
    // varint_eight_bytes (0xFF) followed by little endian uint64
    data_chunk const buffer{0xFF, 0xbe, 0x2a, 0xf0, 0xd8, 0xe5, 0x4b, 0xb1, 0xd4};
    byte_reader reader(buffer);

    auto const result = reader.read_variable_little_endian();
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
}

TEST_CASE("byte_reader - read bytes", "[byte_reader tests]") {
    data_chunk const expected{
        0xfb, 0x44, 0x68, 0x84, 0xc6, 0xbf, 0x33, 0xc6, 0x27, 0x54, 0x73, 0x92,
        0x52, 0xa7, 0xb0, 0xf7, 0x47, 0x87, 0x89, 0x28, 0xf2, 0xf4, 0x18, 0x1d,
        0x01, 0x3f, 0xb7, 0xa2, 0xe9, 0x66, 0x69, 0xbf, 0x06, 0x83, 0x45, 0x34,
        0x8e, 0xc2, 0x9b, 0x3c, 0x86, 0xa9, 0xb8, 0x5f, 0xf7, 0x11, 0xa2, 0x00,
        0x5a, 0xa8
    };

    byte_reader reader(expected);
    auto const result = reader.read_bytes(expected.size());

    REQUIRE(result.has_value());
    REQUIRE(std::equal(result->begin(), result->end(), expected.begin()));
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("byte_reader - read_packed hash_digest", "[byte_reader tests]") {
    hash_digest const expected{{
        0x4d, 0xc9, 0x32, 0x18, 0x4d, 0x86, 0xa0, 0xb2, 0xe4, 0xba, 0x65, 0xa8,
        0x36, 0x1f, 0xea, 0x05, 0xf0, 0x26, 0x68, 0xa5, 0x09, 0x69, 0x10, 0x39,
        0x08, 0x95, 0x00, 0x7d, 0xa4, 0x2e, 0x7c, 0x12
    }};

    data_chunk const buffer(expected.begin(), expected.end());
    byte_reader reader(buffer);
    auto const result = reader.read_packed<hash_digest>();

    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("byte_reader - read_packed short_hash", "[byte_reader tests]") {
    short_hash const expected{{
        0xed, 0x36, 0x48, 0xaf, 0x53, 0xc2, 0x8a, 0x79, 0x90, 0xab, 0x62, 0x04,
        0xb5, 0x2c, 0x6a, 0x40, 0xdc, 0x6d, 0xa5, 0xfe
    }};

    data_chunk const buffer(expected.begin(), expected.end());
    byte_reader reader(buffer);
    auto const result = reader.read_packed<short_hash>();

    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("byte_reader - read_string fixed size", "[byte_reader tests]") {
    std::string const expected = "my string ";  // 10 chars
    data_chunk const buffer(expected.begin(), expected.end());
    byte_reader reader(buffer);

    auto const result = reader.read_string(10);
    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("byte_reader - read_string fixed size with null terminator", "[byte_reader tests]") {
    std::string const input = "hello";
    data_chunk buffer(input.begin(), input.end());
    buffer.push_back(0x00);  // null terminator
    buffer.push_back('x');
    buffer.push_back('y');
    buffer.push_back('z');

    byte_reader reader(buffer);
    auto const result = reader.read_string(9);

    REQUIRE(result.has_value());
    REQUIRE(*result == "hello");  // Should stop at null
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("byte_reader - read_string with varint prefix", "[byte_reader tests]") {
    std::string const expected = "my string data";
    data_chunk buffer;
    buffer.push_back(static_cast<uint8_t>(expected.length()));  // varint length
    buffer.insert(buffer.end(), expected.begin(), expected.end());

    byte_reader reader(buffer);
    auto const result = reader.read_string();

    REQUIRE(result.has_value());
    REQUIRE(*result == expected);
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("byte_reader - read_remaining_bytes", "[byte_reader tests]") {
    data_chunk const expected{
        0x4d, 0xc9, 0x32, 0x18, 0x4d, 0x86, 0xa0, 0xb2, 0xe4, 0xba, 0x65, 0xa8,
        0x36, 0x1f, 0xea, 0x05, 0xf0, 0x26, 0x68, 0xa5, 0x09, 0x69, 0x10, 0x39,
        0x08, 0x95, 0x00, 0x7d, 0xa4, 0x2e, 0x7c, 0x12
    };

    byte_reader reader(expected);
    auto const result = reader.read_remaining_bytes();

    REQUIRE(result.has_value());
    REQUIRE(std::equal(result->begin(), result->end(), expected.begin()));
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("byte_reader - skip advances position", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03, 0x04, 0x05};
    byte_reader reader(buffer);

    auto skip_result = reader.skip(3);
    REQUIRE(skip_result.has_value());
    REQUIRE(reader.position() == 3);

    auto byte_result = reader.read_byte();
    REQUIRE(byte_result.has_value());
    REQUIRE(*byte_result == 0x04);
}

TEST_CASE("byte_reader - skip past end returns error", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02};
    byte_reader reader(buffer);

    auto skip_result = reader.skip(5);
    REQUIRE( ! skip_result.has_value());
    REQUIRE(skip_result.error() == error::skip_past_end_of_buffer);
}

TEST_CASE("byte_reader - remaining_size decreases as reading", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03, 0x04, 0x05};
    byte_reader reader(buffer);

    REQUIRE(reader.remaining_size() == 5);
    reader.read_byte();
    REQUIRE(reader.remaining_size() == 4);
    reader.read_bytes(2);
    REQUIRE(reader.remaining_size() == 2);
}

TEST_CASE("byte_reader - reset restores position to beginning", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03};
    byte_reader reader(buffer);

    reader.read_bytes(2);
    REQUIRE(reader.position() == 2);

    reader.reset();
    REQUIRE(reader.position() == 0);
    REQUIRE(reader.remaining_size() == 3);
}

TEST_CASE("byte_reader - buffer_size returns total size", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03, 0x04, 0x05};
    byte_reader reader(buffer);

    REQUIRE(reader.buffer_size() == 5);
    reader.read_bytes(3);
    REQUIRE(reader.buffer_size() == 5);  // Total size unchanged
}

// End Test Suite
