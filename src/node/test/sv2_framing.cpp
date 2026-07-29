// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <array>
#include <cstdint>

#include <kth/infrastructure.hpp>
#include <kth/node/sv2/framing.hpp>

using namespace kth;
using namespace kth::node::sv2;

// Start Test Suite: sv2 framing tests

TEST_CASE("sv2 message_header serializes to the little-endian wire layout", "[sv2 framing]") {
    message_header const h{/*extension_type*/ 0x0102u, /*msg_type*/ 0x03u,
                           /*msg_length*/ 0x040506u};

    auto const bytes = to_data_chunk(h);

    // extension_type (U16 LE), msg_type (U8), msg_length (U24 LE).
    data_chunk const expected{0x02, 0x01, 0x03, 0x06, 0x05, 0x04};
    REQUIRE(bytes == expected);
    REQUIRE(bytes.size() == message_header::size);
}

TEST_CASE("sv2 message_header round-trips through from_data", "[sv2 framing]") {
    message_header const h{0x8001u, 0x2au, 0x123456u};

    auto const bytes = to_data_chunk(h);
    auto const parsed = from_data_chunk<message_header>(bytes);

    REQUIRE(parsed.has_value());
    REQUIRE(parsed->extension_type == h.extension_type);
    REQUIRE(parsed->msg_type == h.msg_type);
    REQUIRE(parsed->msg_length == h.msg_length);
}

TEST_CASE("sv2 message_header channel_msg flag and extension id", "[sv2 framing]") {
    message_header const channel{0x8005u, 0u, 0u};
    REQUIRE(channel.channel_msg());
    REQUIRE(channel.extension() == 0x0005u);

    message_header const plain{0x0005u, 0u, 0u};
    REQUIRE_FALSE(plain.channel_msg());
    REQUIRE(plain.extension() == 0x0005u);
}

TEST_CASE("sv2 message_header from_data rejects a short buffer", "[sv2 framing]") {
    data_chunk const truncated{0x02, 0x01, 0x03, 0x06, 0x05};  // 5 bytes, needs 6
    auto const parsed = from_data_chunk<message_header>(truncated);
    REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("sv2 u24 round-trips at the boundaries", "[sv2 framing]") {
    for (uint32_t const value : {uint32_t{0}, uint32_t{1}, uint32_t{0x123456}, u24_max}) {
        std::array<uint8_t, 3> buf{};
        byte_writer sink(buf);
        REQUIRE(write_u24(sink, value).has_value());

        byte_reader source(buf);
        auto const got = read_u24(source);
        REQUIRE(got.has_value());
        REQUIRE(*got == value);
    }
}

TEST_CASE("sv2 write_u24 rejects a value above 2^24 - 1", "[sv2 framing]") {
    std::array<uint8_t, 3> buf{};
    byte_writer sink(buf);
    REQUIRE_FALSE(write_u24(sink, u24_max + 1).has_value());
}

TEST_CASE("sv2 read_u24 is little-endian", "[sv2 framing]") {
    data_chunk const bytes{0x06, 0x05, 0x04};  // 0x040506
    byte_reader source(bytes);
    auto const got = read_u24(source);
    REQUIRE(got.has_value());
    REQUIRE(*got == 0x040506u);
}
