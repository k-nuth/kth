// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <array>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <test_helpers.hpp>

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure;

// Start Test Suite: byte_reader tests

// Construction, the non-allocating accessors and the fixed-size primitives that
// `from_data` factories build on cannot throw: they bounds-check against
// remaining_size(), copy trivially copyable bytes and build the error through
// `error::make_error_code`, which is itself noexcept. The operations that
// allocate (`read_string`) or that are not needed by a noexcept parser keep
// their potentially-throwing signature.
static_assert(std::is_nothrow_constructible_v<byte_reader, byte_span>);
static_assert(noexcept(byte_reader(std::declval<byte_span>())));
static_assert(noexcept(std::declval<byte_reader const&>().buffer_size()));
static_assert(noexcept(std::declval<byte_reader const&>().buffer()));
static_assert(noexcept(std::declval<byte_reader const&>().remaining_size()));
static_assert(noexcept(std::declval<byte_reader const&>().position()));
static_assert(noexcept(std::declval<byte_reader const&>().is_exhausted()));
static_assert(noexcept(std::declval<byte_reader&>().unsafe_read_byte()));
static_assert(noexcept(std::declval<byte_reader&>().read_byte()));
static_assert(noexcept(std::declval<byte_reader&>().read_bytes(1)));
static_assert(noexcept(std::declval<byte_reader&>().read_bytes_to(std::declval<std::span<uint8_t>>())));
static_assert(noexcept(std::declval<byte_reader&>().skip_remaining()));
static_assert(noexcept(std::declval<byte_reader&>().read_little_endian<uint8_t>()));
static_assert(noexcept(std::declval<byte_reader&>().read_little_endian<uint16_t>()));
static_assert(noexcept(std::declval<byte_reader&>().read_little_endian<uint32_t>()));
static_assert(noexcept(std::declval<byte_reader&>().read_little_endian<uint64_t>()));
static_assert(noexcept(std::declval<byte_reader&>().read_variable_little_endian()));
static_assert(noexcept(error::make_error_code(error::read_past_end_of_buffer)));

// `read_packed<T>` default constructs and moves a `T`, so it is only noexcept
// when `T` is nothrow default constructible and nothrow move constructible.
namespace {
struct packed_nothrow { uint32_t a; uint32_t b; };
struct packed_throwing_default {
    packed_throwing_default() noexcept(false) {}
    uint32_t a;
};
// Trivially copyable, yet `operator&` throws. `read_packed` must not use it.
struct packed_throwing_address_of {
    uint32_t a;
    packed_throwing_address_of* operator&() noexcept(false) {
        throw std::runtime_error("operator& must not be called");
    }
};
} // namespace
static_assert(noexcept(std::declval<byte_reader&>().read_packed<packed_throwing_address_of>()));
static_assert(noexcept(std::declval<byte_reader&>().read_packed<packed_nothrow>()));
static_assert( ! noexcept(std::declval<byte_reader&>().read_packed<packed_throwing_default>()));

// Everything else is deliberately left potentially-throwing.
static_assert( ! noexcept(std::declval<byte_reader&>().skip(1)));
static_assert( ! noexcept(std::declval<byte_reader&>().skip_byte()));
static_assert( ! noexcept(std::declval<byte_reader const&>().peek_byte()));
static_assert( ! noexcept(std::declval<byte_reader&>().read_array<4>()));
static_assert( ! noexcept(std::declval<byte_reader&>().read_big_endian<uint16_t>()));
static_assert( ! noexcept(std::declval<byte_reader&>().read_size_little_endian()));
static_assert( ! noexcept(std::declval<byte_reader&>().read_string()));
static_assert( ! noexcept(std::declval<byte_reader&>().read_string(1)));

TEST_CASE("byte_reader - noexcept accessors report buffer state", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03};
    byte_reader reader(byte_span(buffer.data(), buffer.size()));
    REQUIRE(reader.buffer_size() == 3);
    REQUIRE(reader.buffer().data() == buffer.data());
    REQUIRE(reader.remaining_size() == 3);
    REQUIRE(reader.position() == 0);
    REQUIRE( ! reader.is_exhausted());
    REQUIRE(reader.read_byte().has_value());
    REQUIRE(reader.remaining_size() == 2);
    REQUIRE(reader.position() == 1);
}

TEST_CASE("byte_reader - noexcept construction over a null empty span", "[byte_reader tests]") {
    byte_reader reader(byte_span(static_cast<uint8_t const*>(nullptr), 0));
    REQUIRE(reader.buffer_size() == 0);
    REQUIRE(reader.is_exhausted());
}

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
    buffer.push_back(uint8_t(expected.length()));  // varint length
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
    [[maybe_unused]] auto r1 = reader.skip(1);
    REQUIRE(reader.remaining_size() == 4);
    [[maybe_unused]] auto r2 = reader.read_bytes(2);
    REQUIRE(reader.remaining_size() == 2);
}

TEST_CASE("byte_reader - reset restores position to beginning", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03};
    byte_reader reader(buffer);

    [[maybe_unused]] auto r = reader.read_bytes(2);
    REQUIRE(reader.position() == 2);

    reader.reset();
    REQUIRE(reader.position() == 0);
    REQUIRE(reader.remaining_size() == 3);
}

TEST_CASE("byte_reader - buffer_size returns total size", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03, 0x04, 0x05};
    byte_reader reader(buffer);

    REQUIRE(reader.buffer_size() == 5);
    [[maybe_unused]] auto r = reader.read_bytes(3);
    REQUIRE(reader.buffer_size() == 5);  // Total size unchanged
}

TEST_CASE("byte_reader - skip_byte advances position by one", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03};
    byte_reader reader(buffer);

    REQUIRE(reader.position() == 0);
    auto result = reader.skip_byte();
    REQUIRE(result.has_value());
    REQUIRE(reader.position() == 1);

    result = reader.skip_byte();
    REQUIRE(result.has_value());
    REQUIRE(reader.position() == 2);
}

TEST_CASE("byte_reader - skip_byte fails at end of buffer", "[byte_reader tests]") {
    data_chunk const buffer{0x01};
    byte_reader reader(buffer);

    auto result = reader.skip_byte();
    REQUIRE(result.has_value());
    REQUIRE(reader.position() == 1);

    result = reader.skip_byte();
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == error::skip_past_end_of_buffer);
}

TEST_CASE("byte_reader - unsafe_skip_byte advances position without bounds check", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03};
    byte_reader reader(buffer);

    REQUIRE(reader.position() == 0);
    reader.unsafe_skip_byte();
    REQUIRE(reader.position() == 1);
    reader.unsafe_skip_byte();
    REQUIRE(reader.position() == 2);
}

TEST_CASE("byte_reader - read_bytes_to copies bytes to destination", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03, 0x04, 0x05};
    byte_reader reader(buffer);

    std::array<uint8_t, 3> dest{};
    auto result = reader.read_bytes_to(dest);
    REQUIRE(result.has_value());
    REQUIRE(dest[0] == 0x01);
    REQUIRE(dest[1] == 0x02);
    REQUIRE(dest[2] == 0x03);
    REQUIRE(reader.position() == 3);
}

TEST_CASE("byte_reader - read_bytes_to fails when not enough bytes", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02};
    byte_reader reader(buffer);

    std::array<uint8_t, 5> dest{};
    auto result = reader.read_bytes_to(dest);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == error::read_past_end_of_buffer);
}

TEST_CASE("byte_reader - read_array returns fixed-size array", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03, 0x04, 0x05};
    byte_reader reader(buffer);

    auto result = reader.read_array<3>();
    REQUIRE(result.has_value());
    REQUIRE((*result)[0] == 0x01);
    REQUIRE((*result)[1] == 0x02);
    REQUIRE((*result)[2] == 0x03);
    REQUIRE(reader.position() == 3);
}

TEST_CASE("byte_reader - read_array fails when not enough bytes", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02};
    byte_reader reader(buffer);

    auto result = reader.read_array<5>();
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == error::read_past_end_of_buffer);
}

// Reading nothing: a default span's data() is null, and so is a zero-length
// array's and an empty buffer's. It succeeds, moves nothing and touches
// nothing; memcpy is undefined for a null pointer even with a count of zero.
TEST_CASE("byte_reader - read_bytes_to a default span reads nothing", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03};
    byte_reader reader(buffer);
    REQUIRE(reader.read_byte().has_value());

    std::span<uint8_t> const nothing{};
    REQUIRE(nothing.data() == nullptr);
    REQUIRE(reader.read_bytes_to(nothing).has_value());
    REQUIRE(reader.position() == 1u);

    // And at the end of the buffer, and from an empty one.
    REQUIRE(reader.read_bytes(2u).has_value());
    REQUIRE(reader.read_bytes_to(nothing).has_value());
    REQUIRE(reader.position() == 3u);

    data_chunk const empty{};
    byte_reader from_nothing(empty);
    REQUIRE(from_nothing.read_bytes_to(nothing).has_value());
    REQUIRE(from_nothing.position() == 0u);
}

TEST_CASE("byte_reader - read_array of zero bytes reads nothing", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02};
    byte_reader reader(buffer);
    REQUIRE(reader.read_byte().has_value());

    auto const result = reader.read_array<0>();
    REQUIRE(result.has_value());
    REQUIRE(result->empty());
    REQUIRE(reader.position() == 1u);

    data_chunk const empty{};
    byte_reader from_nothing(empty);
    REQUIRE(from_nothing.read_array<0>().has_value());
    REQUIRE(from_nothing.position() == 0u);
}

// ---------------------------------------------------------------------------
// Regression: integer-overflow / wrap-around in the bounds checks (0xaudron
// report). Before the fix, `position_ + count > buffer_.size()` wrapped modulo
// 2^64, so a huge count passed the guard and moved the cursor backwards. The
// fix compares against remaining_size(). These tests pin the fixed behavior.
// ---------------------------------------------------------------------------

TEST_CASE("byte_reader - skip rejects wrapping count and does not move cursor", "[overflow-poc]") {
    data_chunk const buffer(100, 0x00);
    byte_reader reader(buffer);

    // Advance to a known position.
    REQUIRE(reader.skip(50).has_value());
    REQUIRE(reader.position() == 50);

    // count = 2^64 - 10.  Pre-fix, 50 + count wrapped to 40 and passed the
    // guard; post-fix it is correctly rejected (10 remaining bytes < count).
    size_t const wrapping_count = std::numeric_limits<size_t>::max() - 9;
    auto const result = reader.skip(wrapping_count);

    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == error::skip_past_end_of_buffer);
    REQUIRE(reader.position() == 50);   // cursor untouched
}

TEST_CASE("byte_reader - read_bytes rejects wrapping size", "[overflow-poc]") {
    data_chunk const buffer(100, 0x00);
    byte_reader reader(buffer);

    REQUIRE(reader.skip(50).has_value());

    // size = 2^64 - 10.  Pre-fix this returned a bogus ~2^64-byte span; post-fix
    // it is rejected and the cursor stays put.
    size_t const wrapping_size = std::numeric_limits<size_t>::max() - 9;
    auto const result = reader.read_bytes(wrapping_size);

    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == error::read_past_end_of_buffer);
    REQUIRE(reader.position() == 50);
}

TEST_CASE("byte_reader - noexcept error codes are the bitcoin category", "[byte_reader tests]") {
    auto const ec = error::make_error_code(error::read_past_end_of_buffer);
    REQUIRE(ec.value() == int(error::read_past_end_of_buffer));
    REQUIRE(std::string(ec.category().name()) == "bitcoin");
    REQUIRE(ec == error::read_past_end_of_buffer);
    // The category instance is a single object across calls.
    REQUIRE(&ec.category() == &error::make_error_code(error::skip_past_end_of_buffer).category());
}

TEST_CASE("byte_reader - noexcept primitives read values and report truncation", "[byte_reader tests]") {
    data_chunk const buffer{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    byte_reader reader(byte_span(buffer.data(), buffer.size()));

    auto const byte_exp = reader.read_byte();
    REQUIRE(byte_exp.has_value());
    REQUIRE(*byte_exp == 0x01);

    auto const u16 = reader.read_little_endian<uint16_t>();
    REQUIRE(u16.has_value());
    REQUIRE(*u16 == 0x0302);

    auto const bytes = reader.read_bytes(2);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 2);
    REQUIRE((*bytes)[0] == 0x04);
    REQUIRE((*bytes)[1] == 0x05);

    std::array<uint8_t, 2> dest{};
    REQUIRE(reader.read_bytes_to(dest).has_value());
    REQUIRE(dest[0] == 0x06);
    REQUIRE(dest[1] == 0x07);

    // Two bytes remain: a 4 byte read fails without moving the cursor.
    auto const position = reader.position();
    auto const u32 = reader.read_little_endian<uint32_t>();
    REQUIRE( ! u32.has_value());
    REQUIRE(u32.error() == error::read_past_end_of_buffer);
    REQUIRE( ! reader.read_bytes(3).has_value());
    std::array<uint8_t, 3> too_big{};
    auto const to = reader.read_bytes_to(too_big);
    REQUIRE( ! to.has_value());
    REQUIRE(to.error() == error::read_past_end_of_buffer);
    REQUIRE(reader.position() == position);

    reader.skip_remaining();
    REQUIRE(reader.is_exhausted());
    auto const eof = reader.read_byte();
    REQUIRE( ! eof.has_value());
    REQUIRE(eof.error() == error::read_past_end_of_buffer);
}

TEST_CASE("byte_reader - noexcept read_variable_little_endian covers every prefix", "[byte_reader tests]") {
    {
        data_chunk const buffer{0xfc};
        byte_reader reader(byte_span(buffer.data(), buffer.size()));
        auto const v = reader.read_variable_little_endian();
        REQUIRE(v.has_value());
        REQUIRE(*v == 0xfc);
    }
    {
        data_chunk const buffer{0xfd, 0x34, 0x12};
        byte_reader reader(byte_span(buffer.data(), buffer.size()));
        REQUIRE(*reader.read_variable_little_endian() == 0x1234);
    }
    {
        data_chunk const buffer{0xfe, 0x78, 0x56, 0x34, 0x12};
        byte_reader reader(byte_span(buffer.data(), buffer.size()));
        REQUIRE(*reader.read_variable_little_endian() == 0x12345678);
    }
    {
        data_chunk const buffer{0xff, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01};
        byte_reader reader(byte_span(buffer.data(), buffer.size()));
        REQUIRE(*reader.read_variable_little_endian() == 0x0123456789abcdef);
    }
    {
        // Truncated body after a 4 byte marker.
        data_chunk const buffer{0xfe, 0x78, 0x56};
        byte_reader reader(byte_span(buffer.data(), buffer.size()));
        auto const v = reader.read_variable_little_endian();
        REQUIRE( ! v.has_value());
        REQUIRE(v.error() == error::read_past_end_of_buffer);
    }
    {
        byte_reader reader(byte_span(static_cast<uint8_t const*>(nullptr), 0));
        REQUIRE( ! reader.read_variable_little_endian().has_value());
    }
}

TEST_CASE("byte_reader - read_packed bypasses an overloaded throwing operator&", "[byte_reader tests]") {
    data_chunk const buffer{0x78, 0x56, 0x34, 0x12};
    byte_reader reader(byte_span(buffer.data(), buffer.size()));
    auto const value = reader.read_packed<packed_throwing_address_of>();
    REQUIRE(value.has_value());
    REQUIRE(value->a == 0x12345678u);
}

TEST_CASE("byte_reader - noexcept read_packed reads a trivially copyable struct", "[byte_reader tests]") {
    data_chunk const buffer{0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01};
    byte_reader reader(byte_span(buffer.data(), buffer.size()));
    auto const value = reader.read_packed<packed_nothrow>();
    REQUIRE(value.has_value());
    REQUIRE(value->a == 0x12345678u);
    REQUIRE(value->b == 0x01020304u);
    REQUIRE( ! reader.read_packed<packed_nothrow>().has_value());
}

// End Test Suite

// BCHN serialize.h::ReadCompactSizeWithLimit at
// 07576013c91ff4a3a74acd85f189c69121cdad1b: each marker has a minimum value.
TEST_CASE("CompactSize canonical width boundaries", "[byte_reader tests][compactsize]") {
    struct sample { data_chunk bytes; uint64_t value; };
    for (auto const& s : std::vector<sample>{
        {{0x00}, 0}, {{0xfc}, 252},
        {{0xfd, 0xfd, 0x00}, 253}, {{0xfd, 0xff, 0xff}, 65535},
        {{0xfe, 0x00, 0x00, 0x01, 0x00}, 65536},
        {{0xfe, 0xff, 0xff, 0xff, 0xff}, 0xffffffffULL},
        {{0xff, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, 0x100000000ULL},
        {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, UINT64_MAX}}) {
        byte_reader reader(s.bytes);
        auto const result = reader.read_variable_little_endian();
        REQUIRE(result.has_value());
        CHECK(*result == s.value);
        CHECK(reader.is_exhausted());
    }
}

TEST_CASE("CompactSize rejects noncanonical widths", "[byte_reader tests][compactsize]") {
    for (auto const& bytes : std::vector<data_chunk>{
        {0xfd, 0x00, 0x00}, {0xfd, 0xfc, 0x00},
        {0xfe, 0x00, 0x00, 0x00, 0x00}, {0xfe, 0xfd, 0x00, 0x00, 0x00},
        {0xfe, 0xff, 0xff, 0x00, 0x00},
        {0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xff, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00}}) {
        byte_reader reader(bytes);
        auto const result = reader.read_variable_little_endian();
        CHECK_FALSE(result.has_value());
        if (!result) CHECK(result.error() == error::invalid_size);
        byte_reader size_reader(bytes);
        CHECK_FALSE(size_reader.read_size_little_endian().has_value());
    }
}

TEST_CASE("CompactSize truncated payload retains read error", "[byte_reader tests][compactsize]") {
    for (auto const marker : {byte{0xfd}, byte{0xfe}, byte{0xff}}) {
        size_t const width = marker == 0xfd ? 2 : marker == 0xfe ? 4 : 8;
        for (size_t n = 0; n < width; ++n) {
            data_chunk bytes(1 + n, 0xff);
            bytes[0] = marker;
            byte_reader reader(bytes);
            auto const result = reader.read_variable_little_endian();
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error() == error::read_past_end_of_buffer);
        }
    }
}
