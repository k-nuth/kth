// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <kth/node/sv2/extensions.hpp>
#include <kth/node/sv2/framing.hpp>
#include <kth/node/sv2/primitives.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

// Every shorter buffer is refused, and so is one trailing byte.
template <typename Message>
Message require_codec(Message const& message) {
    auto const bytes = to_data_chunk(message);
    REQUIRE(bytes.size() == message.serialized_size());
    auto const parsed = from_data_chunk<Message>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(to_data_chunk(*parsed) == bytes);
    for (size_t size = 0; size < bytes.size(); ++size) {
        CAPTURE(size);
        REQUIRE_FALSE(from_data_chunk<Message>(data_chunk(bytes.begin(),
            bytes.begin() + static_cast<std::ptrdiff_t>(size))).has_value());
    }
    auto trailing = bytes;
    trailing.push_back(0);
    REQUIRE_FALSE(from_data_chunk<Message>(trailing).has_value());
    return *parsed;
}

std::string value_of(tlv_field const& field) {
    return std::string(field.value.begin(), field.value.end());
}

} // namespace

TEST_CASE("sv2 RequestExtensions messages use extension 0x0001 and SEQ0_64K[U16] lists", "[sv2 extensions]") {
    REQUIRE(framed_extension_type<request_extensions>() == 0x0001);
    REQUIRE(framed_extension_type<request_extensions_success>() == 0x0001);
    REQUIRE(framed_extension_type<request_extensions_error>() == 0x0001);
    REQUIRE(request_extensions::message_type == 0x00);
    REQUIRE(request_extensions_success::message_type == 0x01);
    REQUIRE(request_extensions_error::message_type == 0x02);

    // U16 request id, then a U16 count and one U16 extension id.
    request_extensions const ask{1, {worker_hashrate_tracking_extension}};
    REQUIRE(to_data_chunk(ask) == data_chunk{0x01, 0x00, 0x01, 0x00, 0x02, 0x00});
    REQUIRE(require_codec(ask).requested_extensions == ask.requested_extensions);
    REQUIRE(require_codec(request_extensions{}).requested_extensions.empty());
    auto const granted = require_codec(request_extensions_success{7, {0x0002, 0x0003}});
    REQUIRE(granted.request_id == 7);
    REQUIRE(granted.supported_extensions == std::vector<uint16_t>{0x0002, 0x0003});
    auto const refused = require_codec(request_extensions_error{4, {0x0002}, {0x00ab}});
    REQUIRE(refused.unsupported_extensions == std::vector<uint16_t>{0x0002});
    REQUIRE(refused.required_extensions == std::vector<uint16_t>{0x00ab});

    request_extensions over;
    over.requested_extensions.resize(b0_64k_max + 1);
    data_chunk buffer(over.serialized_size());
    byte_writer writer(buffer);
    REQUIRE_FALSE(over.to_data(writer).has_value());
}

TEST_CASE("sv2 TLV fields carry little-endian type and length before the value", "[sv2 extensions]") {
    // The Worker_001 example of sv2-spec 3.4.3 in SV2's little-endian U16s:
    // 02 00 01 0a 00, where the spec text prints 00 02 01 00 0a.
    auto const field = user_identity_tlv("Worker_001");
    REQUIRE(field.extension_type == worker_hashrate_tracking_extension);
    REQUIRE(field.field_type == user_identity_field);
    REQUIRE(to_data_chunk(field) == data_chunk{0x02, 0x00, 0x01, 0x0a, 0x00,
        'W', 'o', 'r', 'k', 'e', 'r', '_', '0', '0', '1'});
    REQUIRE(field.serialized_size() == 15);
    auto const parsed = from_data_chunk<tlv_field>(to_data_chunk(field));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->extension_type == field.extension_type);
    REQUIRE(parsed->field_type == field.field_type);
    REQUIRE(value_of(*parsed) == "Worker_001");

    // A zero length omits the value.
    auto const empty = user_identity_tlv("");
    REQUIRE(to_data_chunk(empty) == data_chunk{0x02, 0x00, 0x01, 0x00, 0x00});

    // Fields follow each other to the end of the payload, in order.
    auto bytes = to_data_chunk(tlv_field{0x0003, 0x07, {0xaa}});
    auto const second = to_data_chunk(field);
    bytes.insert(bytes.end(), second.begin(), second.end());
    byte_reader source(bytes);
    auto const fields = read_tlv_fields(source);
    REQUIRE(fields.has_value());
    REQUIRE(fields->size() == 2);
    REQUIRE(fields->front().extension_type == 0x0003);
    REQUIRE(fields->front().field_type == 0x07);
    REQUIRE(fields->front().value == data_chunk{0xaa});
    REQUIRE(value_of(fields->back()) == "Worker_001");
    byte_reader none(byte_span{});
    auto const nothing = read_tlv_fields(none);
    REQUIRE(nothing.has_value());
    REQUIRE(nothing->empty());

    // A header or value cut short is malformed.
    for (size_t size = 1; size < second.size(); ++size) {
        CAPTURE(size);
        data_chunk const cut(second.begin(), second.begin() + static_cast<std::ptrdiff_t>(size));
        byte_reader short_source(cut);
        REQUIRE_FALSE(read_tlv_fields(short_source).has_value());
    }
}

TEST_CASE("sv2 worker identity TLV cuts a long or invalid name to valid UTF-8 within 32 bytes", "[sv2 extensions]") {
    REQUIRE(max_user_identity_size == 32);
    std::string const exact(32, 'w');
    REQUIRE(value_of(user_identity_tlv(exact)) == exact);
    REQUIRE(value_of(user_identity_tlv(exact + "x")) == exact);
    // A two-byte code point at bytes 31 and 32 does not fit; it is not split.
    std::string const straddling = std::string(31, 'w') + "\xc3\xa9";
    REQUIRE(value_of(user_identity_tlv(straddling)) == std::string(31, 'w'));
    std::string const fits = std::string(30, 'w') + "\xc3\xa9";
    REQUIRE(value_of(user_identity_tlv(fits)) == fits);
    // A four-byte code point is kept whole.
    REQUIRE(value_of(user_identity_tlv("rig\xf0\x9f\x90\x89")) == "rig\xf0\x9f\x90\x89");
    // The name stops before its first byte that is not valid UTF-8: a bare
    // continuation byte, an overlong form, a surrogate, or a truncated sequence.
    REQUIRE(value_of(user_identity_tlv("rig\x80tail")) == "rig");
    REQUIRE(value_of(user_identity_tlv("rig\xc0\xafx")) == "rig");
    REQUIRE(value_of(user_identity_tlv("rig\xed\xa0\x80x")) == "rig");
    REQUIRE(value_of(user_identity_tlv("rig\xe2\x82")) == "rig");
    REQUIRE(value_of(user_identity_tlv("\xff")).empty());
}
