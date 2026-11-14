// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/extensions.hpp>

#include <utility>

#include <kth/node/sv2/primitives.hpp>

namespace kth::node::sv2 {

namespace {

template <typename Message>
expect<Message> complete(byte_reader& source, Message message) {
    if ( ! source.is_exhausted()) return std::unexpected(error::operation_failed);
    return message;
}

expect<void> write_u16_seq(byte_writer& sink, std::vector<uint16_t> const& items) {
    if (items.size() > b0_64k_max) return std::unexpected(error::operation_failed);
    if (auto r = sink.write_little_endian<uint16_t>(static_cast<uint16_t>(items.size())); ! r) return r;
    for (auto const item : items) {
        if (auto r = sink.write_little_endian<uint16_t>(item); ! r) return r;
    }
    return {};
}

expect<std::vector<uint16_t>> read_u16_seq(byte_reader& source) {
    auto const count = source.read_little_endian<uint16_t>();
    if ( ! count) return std::unexpected(count.error());
    if (*count > source.remaining_size() / sizeof(uint16_t)) {
        return std::unexpected(error::operation_failed);
    }
    std::vector<uint16_t> items;
    items.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto const item = source.read_little_endian<uint16_t>();
        if ( ! item) return std::unexpected(item.error());
        items.push_back(*item);
    }
    return items;
}

size_t u16_seq_size(std::vector<uint16_t> const& items) {
    return 2 + 2 * items.size();
}

// The bytes of `text`, at most `limit`, that are valid UTF-8 and end between
// code points. Stops at the first byte that does not continue valid UTF-8.
size_t utf8_prefix(std::string_view text, size_t limit) {
    size_t valid = 0;
    while (valid < text.size()) {
        auto position = valid;
        auto const first = static_cast<uint8_t>(text[position++]);
        size_t following = 0;
        if (first < 0x80) following = 0;
        else if (first >= 0xc2 && first <= 0xdf) following = 1;
        else if (first >= 0xe0 && first <= 0xef) following = 2;
        else if (first >= 0xf0 && first <= 0xf4) following = 3;
        else break;
        if (following > text.size() - position) break;
        uint32_t point = following == 0 ? first : first & ((1u << (6 - following)) - 1);
        bool continued = true;
        for (size_t i = 0; i < following; ++i) {
            auto const next = static_cast<uint8_t>(text[position++]);
            if ((next & 0xc0) != 0x80) {
                continued = false;
                break;
            }
            point = (point << 6) | (next & 0x3f);
        }
        if ( ! continued || (following == 2 && point < 0x800) || (following == 3 && point < 0x10000)
            || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) break;
        if (position > limit) break;
        valid = position;
    }
    return valid;
}

} // namespace

size_t request_extensions::serialized_size() const {
    return 2 + u16_seq_size(requested_extensions);
}

expect<void> request_extensions::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint16_t>(request_id); ! r) return r;
    return write_u16_seq(sink, requested_extensions);
}

expect<request_extensions> request_extensions::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint16_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto requested = read_u16_seq(source);
    if ( ! requested) return std::unexpected(requested.error());
    return complete(source, request_extensions{*request_id, std::move(*requested)});
}

size_t request_extensions_success::serialized_size() const {
    return 2 + u16_seq_size(supported_extensions);
}

expect<void> request_extensions_success::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint16_t>(request_id); ! r) return r;
    return write_u16_seq(sink, supported_extensions);
}

expect<request_extensions_success> request_extensions_success::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint16_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto supported = read_u16_seq(source);
    if ( ! supported) return std::unexpected(supported.error());
    return complete(source, request_extensions_success{*request_id, std::move(*supported)});
}

size_t request_extensions_error::serialized_size() const {
    return 2 + u16_seq_size(unsupported_extensions) + u16_seq_size(required_extensions);
}

expect<void> request_extensions_error::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint16_t>(request_id); ! r) return r;
    if (auto r = write_u16_seq(sink, unsupported_extensions); ! r) return r;
    return write_u16_seq(sink, required_extensions);
}

expect<request_extensions_error> request_extensions_error::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint16_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto unsupported = read_u16_seq(source);
    if ( ! unsupported) return std::unexpected(unsupported.error());
    auto required = read_u16_seq(source);
    if ( ! required) return std::unexpected(required.error());
    return complete(source, request_extensions_error{*request_id,
        std::move(*unsupported), std::move(*required)});
}

expect<void> tlv_field::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint16_t>(extension_type); ! r) return r;
    if (auto r = sink.write_byte(field_type); ! r) return r;
    return write_bytes_u16(sink, value);
}

expect<tlv_field> tlv_field::from_data(byte_reader& source) {
    auto const extension_type = source.read_little_endian<uint16_t>();
    if ( ! extension_type) return std::unexpected(extension_type.error());
    auto const field_type = source.read_byte();
    if ( ! field_type) return std::unexpected(field_type.error());
    auto const value = read_bytes_u16(source);
    if ( ! value) return std::unexpected(value.error());
    return tlv_field{*extension_type, *field_type, data_chunk(value->begin(), value->end())};
}

expect<std::vector<tlv_field>> read_tlv_fields(byte_reader& source) {
    std::vector<tlv_field> fields;
    while ( ! source.is_exhausted()) {
        auto field = tlv_field::from_data(source);
        if ( ! field) return std::unexpected(field.error());
        fields.push_back(std::move(*field));
    }
    return fields;
}

tlv_field user_identity_tlv(std::string_view identity) {
    auto const kept = identity.substr(0, utf8_prefix(identity, max_user_identity_size));
    return {worker_hashrate_tracking_extension, user_identity_field, data_chunk(kept.begin(), kept.end())};
}

} // namespace kth::node::sv2
