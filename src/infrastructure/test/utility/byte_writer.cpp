// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure;

// Start Test Suite: byte_writer tests

TEST_CASE("byte_writer - empty buffer is exhausted", "[byte_writer tests]") {
    data_chunk buffer{};
    byte_writer writer(buffer);
    REQUIRE(writer.is_exhausted());
    REQUIRE(writer.position() == 0u);
    REQUIRE(writer.remaining_size() == 0u);
}

TEST_CASE("byte_writer - non-empty buffer starts at position 0", "[byte_writer tests]") {
    data_chunk buffer(8u);
    byte_writer writer(buffer);
    REQUIRE( ! writer.is_exhausted());
    REQUIRE(writer.position() == 0u);
    REQUIRE(writer.remaining_size() == 8u);
}

TEST_CASE("byte_writer - write_byte advances position", "[byte_writer tests]") {
    data_chunk buffer(2u);
    byte_writer writer(buffer);

    REQUIRE(writer.write_byte(0xAA).has_value());
    REQUIRE(writer.position() == 1u);
    REQUIRE(buffer[0] == 0xAA);

    REQUIRE(writer.write_byte(0xBB).has_value());
    REQUIRE(writer.position() == 2u);
    REQUIRE(buffer[1] == 0xBB);
    REQUIRE(writer.is_exhausted());
}

TEST_CASE("byte_writer - write_byte past end returns error", "[byte_writer tests]") {
    data_chunk buffer(1u);
    byte_writer writer(buffer);

    REQUIRE(writer.write_byte(0xAA).has_value());
    auto const result = writer.write_byte(0xBB);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == error::write_past_end_of_buffer);
    REQUIRE(writer.position() == 1u);
}

TEST_CASE("byte_writer - write_bytes copies span", "[byte_writer tests]") {
    data_chunk buffer(4u);
    byte_writer writer(buffer);

    data_chunk const payload{0x11, 0x22, 0x33};
    REQUIRE(writer.write_bytes(payload).has_value());
    REQUIRE(writer.position() == 3u);
    REQUIRE(buffer[0] == 0x11);
    REQUIRE(buffer[1] == 0x22);
    REQUIRE(buffer[2] == 0x33);
}

TEST_CASE("byte_writer - write_bytes past end returns error and does not partial-write", "[byte_writer tests]") {
    data_chunk buffer(2u);
    byte_writer writer(buffer);
    data_chunk const payload{0x11, 0x22, 0x33};

    auto const result = writer.write_bytes(payload);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == error::write_past_end_of_buffer);
    REQUIRE(writer.position() == 0u);
}

TEST_CASE("byte_writer - write_little_endian uint32 produces canonical bytes", "[byte_writer tests]") {
    data_chunk buffer(4u);
    byte_writer writer(buffer);

    REQUIRE(writer.write_little_endian<uint32_t>(0x12345678u).has_value());
    REQUIRE(buffer[0] == 0x78);
    REQUIRE(buffer[1] == 0x56);
    REQUIRE(buffer[2] == 0x34);
    REQUIRE(buffer[3] == 0x12);
}

TEST_CASE("byte_writer - write_little_endian round-trips with read_little_endian", "[byte_writer tests]") {
    data_chunk buffer(8u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_little_endian<uint64_t>(0x0123456789ABCDEFull).has_value());

    byte_reader reader(buffer);
    auto const back = reader.read_little_endian<uint64_t>();
    REQUIRE(back.has_value());
    REQUIRE(*back == 0x0123456789ABCDEFull);
}

TEST_CASE("byte_writer - write_big_endian round-trips", "[byte_writer tests]") {
    data_chunk buffer(4u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_big_endian<uint32_t>(0xCAFEBABEu).has_value());
    REQUIRE(buffer[0] == 0xCA);
    REQUIRE(buffer[1] == 0xFE);
    REQUIRE(buffer[2] == 0xBA);
    REQUIRE(buffer[3] == 0xBE);
}

TEST_CASE("byte_writer - write_variable_little_endian single byte form", "[byte_writer tests]") {
    data_chunk buffer(1u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_variable_little_endian(0xFCu).has_value());
    REQUIRE(writer.position() == 1u);
    REQUIRE(buffer[0] == 0xFC);
}

TEST_CASE("byte_writer - write_variable_little_endian two byte form", "[byte_writer tests]") {
    data_chunk buffer(3u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_variable_little_endian(0x1234u).has_value());
    REQUIRE(writer.position() == 3u);
    REQUIRE(buffer[0] == 0xFD);
    REQUIRE(buffer[1] == 0x34);
    REQUIRE(buffer[2] == 0x12);
}

TEST_CASE("byte_writer - write_variable_little_endian four byte form", "[byte_writer tests]") {
    data_chunk buffer(5u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_variable_little_endian(0x12345678ull).has_value());
    REQUIRE(writer.position() == 5u);
    REQUIRE(buffer[0] == 0xFE);
    REQUIRE(buffer[1] == 0x78);
    REQUIRE(buffer[2] == 0x56);
    REQUIRE(buffer[3] == 0x34);
    REQUIRE(buffer[4] == 0x12);
}

TEST_CASE("byte_writer - write_variable_little_endian eight byte form", "[byte_writer tests]") {
    data_chunk buffer(9u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_variable_little_endian(0xDEADBEEFCAFEBABEull).has_value());
    REQUIRE(writer.position() == 9u);
    REQUIRE(buffer[0] == 0xFF);
    REQUIRE(buffer[1] == 0xBE);
    REQUIRE(buffer[2] == 0xBA);
    REQUIRE(buffer[3] == 0xFE);
    REQUIRE(buffer[4] == 0xCA);
    REQUIRE(buffer[5] == 0xEF);
    REQUIRE(buffer[6] == 0xBE);
    REQUIRE(buffer[7] == 0xAD);
    REQUIRE(buffer[8] == 0xDE);
}

TEST_CASE("byte_writer - write_variable round-trips through byte_reader", "[byte_writer tests]") {
    SECTION("small (1 byte)") {
        data_chunk buffer(1u);
        byte_writer writer(buffer);
        REQUIRE(writer.write_variable_little_endian(42u).has_value());
        byte_reader reader(buffer);
        auto const back = reader.read_variable_little_endian();
        REQUIRE(back.has_value());
        REQUIRE(*back == 42u);
    }
    SECTION("medium (3 bytes)") {
        data_chunk buffer(3u);
        byte_writer writer(buffer);
        REQUIRE(writer.write_variable_little_endian(0xFFFFu).has_value());
        byte_reader reader(buffer);
        auto const back = reader.read_variable_little_endian();
        REQUIRE(back.has_value());
        REQUIRE(*back == 0xFFFFu);
    }
    SECTION("large (9 bytes)") {
        data_chunk buffer(9u);
        byte_writer writer(buffer);
        REQUIRE(writer.write_variable_little_endian(0x100000000ull).has_value());
        byte_reader reader(buffer);
        auto const back = reader.read_variable_little_endian();
        REQUIRE(back.has_value());
        REQUIRE(*back == 0x100000000ull);
    }
}

TEST_CASE("byte_writer - write_size_little_endian delegates to variable encoding", "[byte_writer tests]") {
    data_chunk buffer(3u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_size_little_endian(size_t{0x0100u}).has_value());
    REQUIRE(buffer[0] == 0xFD);
    REQUIRE(buffer[1] == 0x00);
    REQUIRE(buffer[2] == 0x01);
}

TEST_CASE("byte_writer - write_array uses memcpy and advances", "[byte_writer tests]") {
    data_chunk buffer(4u);
    byte_writer writer(buffer);
    std::array<uint8_t, 3> const a{0xDE, 0xAD, 0xBE};
    REQUIRE(writer.write_array(a).has_value());
    REQUIRE(writer.position() == 3u);
    REQUIRE(buffer[0] == 0xDE);
    REQUIRE(buffer[1] == 0xAD);
    REQUIRE(buffer[2] == 0xBE);
}

TEST_CASE("byte_writer - write_hash writes the digest verbatim", "[byte_writer tests]") {
    hash_digest hash{};
    for (size_t i = 0; i < hash.size(); ++i) {
        hash[i] = static_cast<uint8_t>(i);
    }
    data_chunk buffer(hash_size);
    byte_writer writer(buffer);
    REQUIRE(writer.write_hash(hash).has_value());
    REQUIRE(writer.position() == hash_size);
    for (size_t i = 0; i < hash.size(); ++i) {
        REQUIRE(buffer[i] == static_cast<uint8_t>(i));
    }
}

TEST_CASE("byte_writer - write_string writes varint length then bytes", "[byte_writer tests]") {
    std::string const value = "hello";
    data_chunk buffer(1u + value.size());
    byte_writer writer(buffer);

    REQUIRE(writer.write_string(value).has_value());
    REQUIRE(writer.position() == buffer.size());
    REQUIRE(buffer[0] == 5u);
    REQUIRE(buffer[1] == 'h');
    REQUIRE(buffer[2] == 'e');
    REQUIRE(buffer[3] == 'l');
    REQUIRE(buffer[4] == 'l');
    REQUIRE(buffer[5] == 'o');
}

TEST_CASE("byte_writer - write_string round-trips through byte_reader", "[byte_writer tests]") {
    std::string const value = "knuth";
    data_chunk buffer(1u + value.size());
    byte_writer writer(buffer);
    REQUIRE(writer.write_string(value).has_value());

    byte_reader reader(buffer);
    auto const back = reader.read_string();
    REQUIRE(back.has_value());
    REQUIRE(*back == value);
}

TEST_CASE("byte_writer - write_string_fixed pads with terminator", "[byte_writer tests]") {
    data_chunk buffer(8u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_string_fixed("abc", 8u).has_value());
    REQUIRE(writer.position() == 8u);
    REQUIRE(buffer[0] == 'a');
    REQUIRE(buffer[1] == 'b');
    REQUIRE(buffer[2] == 'c');
    REQUIRE(buffer[3] == 0x00);
    REQUIRE(buffer[4] == 0x00);
    REQUIRE(buffer[5] == 0x00);
    REQUIRE(buffer[6] == 0x00);
    REQUIRE(buffer[7] == 0x00);
}

TEST_CASE("byte_writer - write_string_fixed truncates over-length input", "[byte_writer tests]") {
    data_chunk buffer(3u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_string_fixed("hello", 3u).has_value());
    REQUIRE(writer.position() == 3u);
    REQUIRE(buffer[0] == 'h');
    REQUIRE(buffer[1] == 'e');
    REQUIRE(buffer[2] == 'l');
}

TEST_CASE("byte_writer - reset returns position to start", "[byte_writer tests]") {
    data_chunk buffer(4u);
    byte_writer writer(buffer);
    REQUIRE(writer.write_byte(0xAA).has_value());
    REQUIRE(writer.write_byte(0xBB).has_value());
    REQUIRE(writer.position() == 2u);
    writer.reset();
    REQUIRE(writer.position() == 0u);
    REQUIRE( ! writer.is_exhausted());
}

TEST_CASE("byte_writer - unsafe_write_byte writes without bounds-check", "[byte_writer tests]") {
    data_chunk buffer(2u);
    byte_writer writer(buffer);
    writer.unsafe_write_byte(0x11);
    writer.unsafe_write_byte(0x22);
    REQUIRE(writer.position() == 2u);
    REQUIRE(buffer[0] == 0x11);
    REQUIRE(buffer[1] == 0x22);
}
