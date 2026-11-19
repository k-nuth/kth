// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace kth;
using namespace kd;

namespace {

constexpr auto wire_version = message::version::level::maximum;

data_chunk bytes(std::string const& hex) {
    auto const result = decode_base16(hex);
    REQUIRE(result);
    return *result;
}

// Thirty-two bytes that count up from `first`, so a reversed or shifted hash
// is seen. Hashes go on the wire in the order they are held.
hash_digest counting(uint8_t first) {
    hash_digest hash{};
    for (size_t i = 0; i < hash.size(); ++i) hash[i] = uint8_t(first + i);
    return hash;
}

std::string hex_of(hash_digest const& hash) {
    return encode_base16(hash);
}

template <typename Message>
void check_boundaries(Message const& message) {
    auto const raw = to_data_chunk(message, wire_version);
    REQUIRE(raw.size() == message.serialized_size(wire_version));
    // No incomplete payload may parse, wherever it is cut, and no short buffer
    // may take the message.
    for (size_t size = 0; size < raw.size(); ++size) {
        INFO("payload prefix " << size);
        byte_reader reader(byte_span{raw.data(), size});
        REQUIRE( ! Message::from_data(reader, wire_version));
        data_chunk short_buffer(size);
        byte_writer writer(short_buffer);
        auto const written = message.to_data(writer, wire_version);
        REQUIRE( ! written);
        REQUIRE(written.error() == error::write_past_end_of_buffer);
    }
    auto trailing = raw;
    trailing.push_back(0);
    REQUIRE( ! from_data_chunk<Message>(trailing, wire_version));
    auto const again = from_data_chunk<Message>(raw, wire_version);
    REQUIRE(again);
    REQUIRE(*again == message);
}

message::message_type type_of(std::string const& command) {
    return message::heading(0xe8f3e1e3, command, 0, 0).type();
}

} // namespace

TEST_CASE("compact filter messages carry their commands", "[compact filters][message]") {
    REQUIRE(message::get_compact_filters::command == "getcfilters");
    REQUIRE(message::compact_filter::command == "cfilter");
    REQUIRE(message::get_compact_filter_headers::command == "getcfheaders");
    REQUIRE(message::compact_filter_headers::command == "cfheaders");
    REQUIRE(message::get_compact_filter_checkpoint::command == "getcfcheckpt");
    REQUIRE(message::compact_filter_checkpoint::command == "cfcheckpt");

    REQUIRE(type_of("getcfilters") == message::message_type::get_compact_filters);
    REQUIRE(type_of("cfilter") == message::message_type::compact_filter);
    REQUIRE(type_of("getcfheaders") == message::message_type::get_compact_filter_headers);
    REQUIRE(type_of("cfheaders") == message::message_type::compact_filter_headers);
    REQUIRE(type_of("getcfcheckpt") == message::message_type::get_compact_filter_checkpoint);
    REQUIRE(type_of("cfcheckpt") == message::message_type::compact_filter_checkpoint);
    // A longer or shorter name is another message.
    REQUIRE(type_of("cfilters") == message::message_type::unknown);
    REQUIRE(type_of("getcfilter") == message::message_type::unknown);
}

TEST_CASE("getcfilters and getcfheaders are a type, a start height and a stop block", "[compact filters][message]") {
    auto const stop = counting(0x10);
    // The type, the height 0x00030201 little-endian, the stop hash.
    auto const raw = bytes("00" "01020300" + hex_of(stop));
    REQUIRE(raw.size() == 37);

    auto const filters = from_data_chunk<message::get_compact_filters>(raw, wire_version);
    REQUIRE(filters);
    REQUIRE(filters->filter_type == message::regular_compact_filter);
    REQUIRE(filters->start_height == 0x00030201);
    REQUIRE(filters->stop_hash == stop);
    REQUIRE(to_data_chunk(*filters, wire_version) == raw);
    check_boundaries(*filters);

    auto const headers = from_data_chunk<message::get_compact_filter_headers>(raw, wire_version);
    REQUIRE(headers);
    REQUIRE(headers->filter_type == 0);
    REQUIRE(headers->start_height == 0x00030201);
    REQUIRE(headers->stop_hash == stop);
    REQUIRE(to_data_chunk(*headers, wire_version) == raw);
    check_boundaries(*headers);

    // Any filter type parses: whether the node serves it is the node's answer.
    auto other = raw;
    other[0] = 0x07;
    auto const unknown = from_data_chunk<message::get_compact_filters>(other, wire_version);
    REQUIRE(unknown);
    REQUIRE(unknown->filter_type == 0x07);
    // The largest height.
    auto const top = from_data_chunk<message::get_compact_filter_headers>(
        bytes("00" "ffffffff" + hex_of(stop)), wire_version);
    REQUIRE(top);
    REQUIRE(top->start_height == 0xffffffff);
}

TEST_CASE("getcfcheckpt is a type and a stop block", "[compact filters][message]") {
    auto const stop = counting(0x40);
    auto const raw = bytes("00" + hex_of(stop));
    REQUIRE(raw.size() == 33);
    auto const request = from_data_chunk<message::get_compact_filter_checkpoint>(raw, wire_version);
    REQUIRE(request);
    REQUIRE(request->filter_type == 0);
    REQUIRE(request->stop_hash == stop);
    REQUIRE(to_data_chunk(*request, wire_version) == raw);
    check_boundaries(*request);
}

TEST_CASE("cfilter carries one block's filter bytes", "[compact filters][message]") {
    auto const block = counting(0x80);
    // A filter of three bytes: one item, then two bytes of its bit stream.
    auto const raw = bytes("00" + hex_of(block) + "03" "01aabb");
    auto const filter = from_data_chunk<message::compact_filter>(raw, wire_version);
    REQUIRE(filter);
    REQUIRE(filter->filter_type == 0);
    REQUIRE(filter->block_hash == block);
    REQUIRE(filter->filter == bytes("01aabb"));
    REQUIRE(to_data_chunk(*filter, wire_version) == raw);
    check_boundaries(*filter);

    // A block with nothing to put in a filter still has one: a count of zero.
    auto const empty_raw = bytes("00" + hex_of(block) + "01" "00");
    auto const empty = from_data_chunk<message::compact_filter>(empty_raw, wire_version);
    REQUIRE(empty);
    REQUIRE(empty->filter == bytes("00"));
    // And a message may carry no bytes at all.
    auto const none = from_data_chunk<message::compact_filter>(bytes("00" + hex_of(block) + "00"), wire_version);
    REQUIRE(none);
    REQUIRE(none->filter.empty());
    check_boundaries(*none);

    // A filter of 300 bytes takes a three-byte length.
    message::compact_filter wide{0, block, data_chunk(300, 0x5a)};
    auto const wide_raw = to_data_chunk(wide, wire_version);
    REQUIRE(wide_raw.size() == 1 + 32 + 3 + 300);
    REQUIRE(wide_raw[33] == 0xfd);
    REQUIRE(wide_raw[34] == 0x2c);
    REQUIRE(wide_raw[35] == 0x01);
    check_boundaries(wide);
}

TEST_CASE("cfilter refuses a filter beyond the size limit before reading it", "[compact filters][message]") {
    auto const block = counting(0x80);
    // The limit itself is taken.
    message::compact_filter largest{0, block, data_chunk(message::max_compact_filter_size, 0x11)};
    auto const largest_raw = to_data_chunk(largest, wire_version);
    auto const parsed = from_data_chunk<message::compact_filter>(largest_raw, wire_version);
    REQUIRE(parsed);
    REQUIRE(parsed->filter.size() == message::max_compact_filter_size);

    // One byte more is refused on its length alone: the bytes are not there.
    auto const claim = bytes("00" + hex_of(block) + "fe" "01004000");
    auto const refused = from_data_chunk<message::compact_filter>(claim, wire_version);
    REQUIRE( ! refused);
    REQUIRE(refused.error() == error::invalid_size);
    // So is a length that would wrap a 32-bit size.
    auto const huge = bytes("00" + hex_of(block) + "ff" "ffffffffffffffff");
    REQUIRE( ! from_data_chunk<message::compact_filter>(huge, wire_version));
}

TEST_CASE("cfheaders carries the previous filter header and the filter hashes", "[compact filters][message]") {
    auto const stop = counting(0x10);
    auto const previous = counting(0x30);
    auto const first = counting(0x50);
    auto const second = counting(0x70);
    auto const raw = bytes("00" + hex_of(stop) + hex_of(previous) + "02" + hex_of(first) + hex_of(second));
    auto const headers = from_data_chunk<message::compact_filter_headers>(raw, wire_version);
    REQUIRE(headers);
    REQUIRE(headers->filter_type == 0);
    REQUIRE(headers->stop_hash == stop);
    REQUIRE(headers->previous_filter_header == previous);
    REQUIRE(headers->filter_hashes == std::vector<hash_digest>{first, second});
    REQUIRE(to_data_chunk(*headers, wire_version) == raw);
    check_boundaries(*headers);

    // No hashes: the answer for a range the node has nothing for.
    auto const none = from_data_chunk<message::compact_filter_headers>(
        bytes("00" + hex_of(stop) + hex_of(previous) + "00"), wire_version);
    REQUIRE(none);
    REQUIRE(none->filter_hashes.empty());
    check_boundaries(*none);
}

TEST_CASE("cfheaders takes two thousand hashes and refuses one more", "[compact filters][message]") {
    message::compact_filter_headers full;
    full.stop_hash = counting(1);
    full.previous_filter_header = counting(2);
    full.filter_hashes.assign(message::max_compact_filter_headers, counting(3));
    auto const raw = to_data_chunk(full, wire_version);
    REQUIRE(raw.size() == 1 + 32 + 32 + 3 + 2000 * 32);
    auto const parsed = from_data_chunk<message::compact_filter_headers>(raw, wire_version);
    REQUIRE(parsed);
    REQUIRE(parsed->filter_hashes.size() == 2000);

    // 2001 is refused on the count alone: none of the hashes are there.
    auto const claim = bytes("00" + hex_of(counting(1)) + hex_of(counting(2)) + "fd" "d107");
    auto const refused = from_data_chunk<message::compact_filter_headers>(claim, wire_version);
    REQUIRE( ! refused);
    REQUIRE(refused.error() == error::invalid_size);
}

TEST_CASE("cfcheckpt carries the filter headers at the interval", "[compact filters][message]") {
    REQUIRE(message::compact_filter_checkpoint_interval == 1000);
    auto const stop = counting(0x10);
    auto const first = counting(0x50);
    auto const second = counting(0x70);
    auto const third = counting(0x90);
    auto const raw = bytes("00" + hex_of(stop) + "03" + hex_of(first) + hex_of(second) + hex_of(third));
    auto const checkpoint = from_data_chunk<message::compact_filter_checkpoint>(raw, wire_version);
    REQUIRE(checkpoint);
    REQUIRE(checkpoint->filter_type == 0);
    REQUIRE(checkpoint->stop_hash == stop);
    REQUIRE(checkpoint->filter_headers == std::vector<hash_digest>{first, second, third});
    REQUIRE(to_data_chunk(*checkpoint, wire_version) == raw);
    check_boundaries(*checkpoint);

    // A chain shorter than one interval has none.
    auto const none = from_data_chunk<message::compact_filter_checkpoint>(bytes("00" + hex_of(stop) + "00"), wire_version);
    REQUIRE(none);
    REQUIRE(none->filter_headers.empty());

    // More than the limit is refused on the count alone.
    auto const claim = bytes("00" + hex_of(stop) + "fe" "a1860100");
    auto const refused = from_data_chunk<message::compact_filter_checkpoint>(claim, wire_version);
    REQUIRE( ! refused);
    REQUIRE(refused.error() == error::invalid_size);
}
