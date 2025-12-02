// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure;

// Start Test Suite: serializer tests

TEST_CASE("serializer - roundtrip serialize deserialize", "[serializer tests]") {
    data_chunk data(1 + 2 + 4 + 8 + 4 + 4 + 3 + 7);
    auto writer = make_unsafe_serializer(data.begin());
    writer.write_byte(0x80);
    writer.write_2_bytes_little_endian(0x8040);
    writer.write_4_bytes_little_endian(0x80402010);
    writer.write_8_bytes_little_endian(0x8040201011223344);
    writer.write_4_bytes_big_endian(0x80402010);
    writer.write_variable_little_endian(1234);
    writer.write_bytes(to_chunk(to_little_endian<uint32_t>(0xbadf00d)));
    writer.write_string("hello");

    byte_reader reader(data);
    REQUIRE(*reader.read_byte() == 0x80u);
    REQUIRE(*reader.read_little_endian<uint16_t>() == 0x8040u);
    REQUIRE(*reader.read_little_endian<uint32_t>() == 0x80402010u);
    REQUIRE(*reader.read_little_endian<uint64_t>() == 0x8040201011223344u);
    REQUIRE(*reader.read_big_endian<uint32_t>() == 0x80402010u);
    REQUIRE(*reader.read_variable_little_endian() == 1234u);
    REQUIRE(from_little_endian_unsafe<uint32_t>(*reader.read_bytes(4)) == 0xbadf00du);
    REQUIRE(*reader.read_string() == "hello");
    REQUIRE(*reader.read_byte() == 0u);
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("serializer - byte_reader exhaustion", "[serializer tests]") {
    data_chunk const data(42);
    byte_reader reader(data);
    REQUIRE(reader.read_bytes(42).has_value());
    REQUIRE(reader.is_exhausted());
    REQUIRE( ! reader.read_byte().has_value());
}

TEST_CASE("serializer - is exhausted initialized empty stream returns true", "[serializer tests]") {
    data_chunk const data(0);
    byte_reader source(data);
    REQUIRE(source.is_exhausted());
}

TEST_CASE("serializer - is exhausted initialized nonempty stream returns false", "[serializer tests]") {
    data_chunk const data(1);
    byte_reader source(data);
    REQUIRE( ! source.is_exhausted());
}

TEST_CASE("serializer - peek byte nonempty stream does not advance", "[serializer tests]") {
    uint8_t const expected = 0x42;
    data_chunk const data({ expected, 0x00 });
    byte_reader source(data);
    REQUIRE(*source.peek_byte() == expected);
    REQUIRE(*source.peek_byte() == expected);
    REQUIRE(*source.peek_byte() == expected);
}

TEST_CASE("serializer - roundtrip byte", "[serializer tests]") {
    uint8_t const expected = 0xAA;
    data_chunk data(1);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_byte(expected);
    byte_reader source(data);
    auto const result = source.read_byte();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip error code", "[serializer tests]") {
    code const expected(error::futuristic_timestamp);
    data_chunk data(4);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_error_code(expected);
    byte_reader source(data);
    auto const result = source.read_little_endian<uint32_t>();

    REQUIRE(result.has_value());
    REQUIRE(expected == code(static_cast<error::error_code_t>(*result)));
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip 2 bytes little endian", "[serializer tests]") {
    constexpr uint16_t expected = 43707;
    data_chunk data(2);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_2_bytes_little_endian(expected);
    byte_reader source(data);
    auto const result = source.read_little_endian<uint16_t>();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip 4 bytes little endian", "[serializer tests]") {
    constexpr uint32_t expected = 2898120443;
    data_chunk data(4);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_4_bytes_little_endian(expected);
    byte_reader source(data);
    auto const result = source.read_little_endian<uint32_t>();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip 8 bytes little endian", "[serializer tests]") {
    constexpr uint64_t expected = 0xd4b14be5d8f02abe;
    data_chunk data(8);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_8_bytes_little_endian(expected);
    byte_reader source(data);
    auto const result = source.read_little_endian<uint64_t>();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip 2 bytes big endian", "[serializer tests]") {
    constexpr uint16_t expected = 43707;
    data_chunk data(2);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_2_bytes_big_endian(expected);
    byte_reader source(data);
    auto const result = source.read_big_endian<uint16_t>();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip 4 bytes big endian", "[serializer tests]") {
    constexpr uint32_t expected = 2898120443;
    data_chunk data(4);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_4_bytes_big_endian(expected);
    byte_reader source(data);
    auto const result = source.read_big_endian<uint32_t>();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip 8 bytes big endian", "[serializer tests]") {
    uint64_t const expected = 0xd4b14be5d8f02abe;
    data_chunk data(8);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_8_bytes_big_endian(expected);
    byte_reader source(data);
    auto const result = source.read_big_endian<uint64_t>();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip variable uint little endian 1 byte", "[serializer tests]") {
    uint64_t const expected = 0xAA;
    data_chunk data(1);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_variable_little_endian(expected);
    byte_reader source(data);
    auto const result = source.read_variable_little_endian();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip variable uint little endian 2 bytes", "[serializer tests]") {
    uint64_t const expected = 43707;
    data_chunk data(3);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_variable_little_endian(expected);
    byte_reader source(data);
    auto const result = source.read_variable_little_endian();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip variable uint little endian 4 bytes", "[serializer tests]") {
    uint64_t const expected = 2898120443;
    data_chunk data(sizeof(uint32_t) + 1);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_variable_little_endian(expected);
    byte_reader source(data);
    auto const result = source.read_variable_little_endian();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip variable uint little endian 8 bytes", "[serializer tests]") {
    uint64_t const expected = 0xd4b14be5d8f02abe;
    data_chunk data(sizeof(uint64_t) + 1);
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_variable_little_endian(expected);
    byte_reader source(data);
    auto const result = source.read_variable_little_endian();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip data chunk", "[serializer tests]") {
    static data_chunk const expected
    {
        0xfb, 0x44, 0x68, 0x84, 0xc6, 0xbf, 0x33, 0xc6, 0x27, 0x54, 0x73, 0x92,
        0x52, 0xa7, 0xb0, 0xf7, 0x47, 0x87, 0x89, 0x28, 0xf2, 0xf4, 0x18, 0x1d,
        0x01, 0x3f, 0xb7, 0xa2, 0xe9, 0x66, 0x69, 0xbf, 0x06, 0x83, 0x45, 0x34,
        0x8e, 0xc2, 0x9b, 0x3c, 0x86, 0xa9, 0xb8, 0x5f, 0xf7, 0x11, 0xa2, 0x00,
        0x5a, 0xa8
    };

    data_chunk data(expected.size());
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_bytes(expected);
    byte_reader source(data);
    auto const result = source.read_bytes(expected.size());

    REQUIRE(result.has_value());
    REQUIRE(std::ranges::equal(expected, *result));
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip hash", "[serializer tests]") {
    static hash_digest const expected
    {
        0x4d, 0xc9, 0x32, 0x18, 0x4d, 0x86, 0xa0, 0xb2, 0xe4, 0xba, 0x65, 0xa8,
        0x36, 0x1f, 0xea, 0x05, 0xf0, 0x26, 0x68, 0xa5, 0x09, 0x69, 0x10, 0x39,
        0x08, 0x95, 0x00, 0x7d, 0xa4, 0x2e, 0x7c, 0x12
    };

    data_chunk data(expected.size());
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_hash(expected);
    byte_reader source(data);
    auto const result = source.read_packed<hash_digest>();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip short hash", "[serializer tests]") {
    static short_hash const expected
    {
        0xed, 0x36, 0x48, 0xaf, 0x53, 0xc2, 0x8a, 0x79, 0x90, 0xab, 0x62, 0x04,
        0xb5, 0x2c, 0x6a, 0x40 , 0xdc, 0x6d, 0xa5, 0xfe
    };

    data_chunk data(expected.size());
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_short_hash(expected);
    byte_reader source(data);
    auto const result = source.read_packed<short_hash>();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip fixed string", "[serializer tests]") {
    std::string const expected = "my string data";

    data_chunk data(expected.size());
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_string(expected, 10);
    byte_reader source(data);
    auto const result = source.read_string(10);

    REQUIRE(result.has_value());
    REQUIRE(expected.substr(0, 10) == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - roundtrip string", "[serializer tests]") {
    std::string const expected = "my string data";

    data_chunk data((expected.length() + message::variable_uint_size(expected.length())));
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_string(expected);
    byte_reader source(data);
    auto const result = source.read_string();

    REQUIRE(result.has_value());
    REQUIRE(expected == *result);
    REQUIRE((bool)sink);
}

TEST_CASE("serializer - read bytes to eof", "[serializer tests]") {
    static data_chunk const expected
    {
        0x4d, 0xc9, 0x32, 0x18, 0x4d, 0x86, 0xa0, 0xb2, 0xe4, 0xba, 0x65, 0xa8,
        0x36, 0x1f, 0xea, 0x05, 0xf0, 0x26, 0x68, 0xa5, 0x09, 0x69, 0x10, 0x39,
        0x08, 0x95, 0x00, 0x7d, 0xa4, 0x2e, 0x7c, 0x12
    };

    data_chunk data(expected.size());
    auto sink = make_unsafe_serializer(data.begin());

    sink.write_bytes(expected);
    byte_reader source(data);
    auto const result = source.read_remaining_bytes();

    REQUIRE(result.has_value());
    REQUIRE(std::ranges::equal(expected, *result));
    REQUIRE((bool)sink);
}

// End Test Suite
