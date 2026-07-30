// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <string>

#include <kth/infrastructure.hpp>
#include <kth/node/sv2/primitives.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

// A 32-byte value with distinct, position-dependent bytes.
hash_digest seq_hash(uint8_t start) {
    hash_digest h{};
    for (size_t i = 0; i < h.size(); ++i) {
        h[i] = static_cast<uint8_t>(start + i);
    }
    return h;
}

} // namespace

// Start Test Suite: sv2 primitives tests

TEST_CASE("sv2 B0_255 round-trips a payload behind a U8 length", "[sv2 primitives]") {
    data_chunk const payload{0xAA, 0xBB, 0xCC};

    data_chunk buf(1 + payload.size());
    byte_writer sink(buf);
    REQUIRE(write_bytes_u8(sink, payload).has_value());
    REQUIRE(buf.front() == 0x03);           // U8 length prefix

    byte_reader source(buf);
    auto const got = read_bytes_u8(source);
    REQUIRE(got.has_value());
    REQUIRE(data_chunk(got->begin(), got->end()) == payload);
    REQUIRE(source.is_exhausted());
}

TEST_CASE("sv2 B0_255 handles an empty payload", "[sv2 primitives]") {
    data_chunk buf(1);
    byte_writer sink(buf);
    REQUIRE(write_bytes_u8(sink, byte_span{}).has_value());
    REQUIRE(buf == data_chunk{0x00});

    byte_reader source(buf);
    auto const got = read_bytes_u8(source);
    REQUIRE(got.has_value());
    REQUIRE(got->empty());
}

TEST_CASE("sv2 B0_64K uses a two-byte little-endian length", "[sv2 primitives]") {
    data_chunk const payload(300, 0x7F);    // > 255, needs the U16 prefix

    data_chunk buf(2 + payload.size());
    byte_writer sink(buf);
    REQUIRE(write_bytes_u16(sink, payload).has_value());
    REQUIRE(buf[0] == 0x2C);                 // 300 = 0x012C, little-endian
    REQUIRE(buf[1] == 0x01);

    byte_reader source(buf);
    auto const got = read_bytes_u16(source);
    REQUIRE(got.has_value());
    REQUIRE(data_chunk(got->begin(), got->end()) == payload);
}

TEST_CASE("sv2 B0_16M uses a three-byte little-endian length", "[sv2 primitives]") {
    data_chunk const payload(70000, 0x11);  // > 65535, needs the U24 prefix

    data_chunk buf(3 + payload.size());
    byte_writer sink(buf);
    REQUIRE(write_bytes_u24(sink, payload).has_value());

    byte_reader source(buf);
    auto const got = read_bytes_u24(source);
    REQUIRE(got.has_value());
    REQUIRE(got->size() == payload.size());
}

TEST_CASE("sv2 write_bytes_u8 rejects a payload over 255 bytes", "[sv2 primitives]") {
    data_chunk const payload(256, 0x00);
    data_chunk buf(8);
    byte_writer sink(buf);
    REQUIRE_FALSE(write_bytes_u8(sink, payload).has_value());
}

TEST_CASE("sv2 write_bytes_u16 rejects a payload over 65535 bytes", "[sv2 primitives]") {
    data_chunk const payload(b0_64k_max + 1, 0x00);
    data_chunk buf(8);
    byte_writer sink(buf);
    REQUIRE_FALSE(write_bytes_u16(sink, payload).has_value());
}

TEST_CASE("sv2 read_bytes_u8 fails when the payload is truncated", "[sv2 primitives]") {
    data_chunk const bytes{0x03, 0xAA, 0xBB};   // length says 3, only 2 present
    byte_reader source(bytes);
    REQUIRE_FALSE(read_bytes_u8(source).has_value());
}

TEST_CASE("sv2 STR0_255 round-trips and preserves embedded NUL bytes", "[sv2 primitives]") {
    std::string const text("ab\0cd", 5);   // interior NUL

    data_chunk buf(1 + text.size());
    byte_writer sink(buf);
    REQUIRE(write_string_u8(sink, text).has_value());

    byte_reader source(buf);
    auto const got = read_string_u8(source);
    REQUIRE(got.has_value());
    REQUIRE(got->size() == 5);
    REQUIRE(*got == text);
}

TEST_CASE("sv2 SEQ0_255[U256] round-trips behind a U8 count", "[sv2 primitives]") {
    std::vector<hash_digest> const items{seq_hash(1), seq_hash(0x40), seq_hash(0x80)};

    data_chunk buf(1 + items.size() * 32);
    byte_writer sink(buf);
    REQUIRE(write_hash_seq_u8(sink, items).has_value());
    REQUIRE(buf.front() == 0x03);          // U8 count

    byte_reader source(buf);
    auto const got = read_hash_seq_u8(source);
    REQUIRE(got.has_value());
    REQUIRE(*got == items);
    REQUIRE(source.is_exhausted());
}

TEST_CASE("sv2 SEQ0_255[U256] handles an empty sequence", "[sv2 primitives]") {
    data_chunk buf(1);
    byte_writer sink(buf);
    REQUIRE(write_hash_seq_u8(sink, {}).has_value());
    REQUIRE(buf == data_chunk{0x00});

    byte_reader source(buf);
    auto const got = read_hash_seq_u8(source);
    REQUIRE(got.has_value());
    REQUIRE(got->empty());
}

TEST_CASE("sv2 SEQ0_64K[B0_16M] round-trips variable-size elements behind a U16 count", "[sv2 primitives]") {
    std::vector<data_chunk> const items{{0x01, 0x02}, {}, {0xAA, 0xBB, 0xCC}};

    size_t size = 2;                       // U16 count
    for (auto const& item : items) size += 3 + item.size();  // U24 prefix each

    data_chunk buf(size);
    byte_writer sink(buf);
    REQUIRE(write_bytes_seq_u16(sink, items).has_value());
    REQUIRE(buf[0] == 0x03);               // count, little-endian
    REQUIRE(buf[1] == 0x00);

    byte_reader source(buf);
    auto const got = read_bytes_seq_u16(source);
    REQUIRE(got.has_value());
    REQUIRE(*got == items);
    REQUIRE(source.is_exhausted());
}
