// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/primitives.hpp>

#include <kth/node/sv2/framing.hpp>

namespace kth::node::sv2 {

expect<void> write_bytes_u8(byte_writer& sink, byte_span value) {
    if (value.size() > b0_255_max) {
        return std::unexpected(error::operation_failed);
    }
    if (auto r = sink.write_byte(static_cast<uint8_t>(value.size())); ! r) return r;
    return sink.write_bytes(value);
}

expect<void> write_bytes_u16(byte_writer& sink, byte_span value) {
    if (value.size() > b0_64k_max) {
        return std::unexpected(error::operation_failed);
    }
    if (auto r = sink.write_little_endian<uint16_t>(static_cast<uint16_t>(value.size())); ! r) return r;
    return sink.write_bytes(value);
}

expect<void> write_bytes_u24(byte_writer& sink, byte_span value) {
    if (value.size() > b0_16m_max) {
        return std::unexpected(error::operation_failed);
    }
    if (auto r = write_u24(sink, static_cast<uint32_t>(value.size())); ! r) return r;
    return sink.write_bytes(value);
}

expect<byte_span> read_bytes_u8(byte_reader& source) {
    auto const size = source.read_byte();
    if ( ! size) return std::unexpected(size.error());
    return source.read_bytes(*size);
}

expect<byte_span> read_bytes_u16(byte_reader& source) {
    auto const size = source.read_little_endian<uint16_t>();
    if ( ! size) return std::unexpected(size.error());
    return source.read_bytes(*size);
}

expect<byte_span> read_bytes_u24(byte_reader& source) {
    auto const size = read_u24(source);
    if ( ! size) return std::unexpected(size.error());
    return source.read_bytes(*size);
}

expect<void> write_string_u8(byte_writer& sink, std::string_view value) {
    return write_bytes_u8(sink, byte_span{
        reinterpret_cast<uint8_t const*>(value.data()), value.size()});
}

expect<std::string> read_string_u8(byte_reader& source) {
    auto const bytes = read_bytes_u8(source);
    if ( ! bytes) return std::unexpected(bytes.error());
    return std::string(bytes->begin(), bytes->end());
}

expect<void> write_hash_seq_u8(byte_writer& sink, std::vector<hash_digest> const& items) {
    if (items.size() > b0_255_max) {
        return std::unexpected(error::operation_failed);
    }
    if (auto r = sink.write_byte(static_cast<uint8_t>(items.size())); ! r) return r;
    for (auto const& hash : items) {
        if (auto r = sink.write_bytes(byte_span{hash}); ! r) return r;
    }
    return {};
}

expect<std::vector<hash_digest>> read_hash_seq_u8(byte_reader& source) {
    auto const count = source.read_byte();
    if ( ! count) return std::unexpected(count.error());
    std::vector<hash_digest> items;
    items.reserve(*count);
    for (uint8_t i = 0; i < *count; ++i) {
        auto const hash = source.read_array<hash_size>();
        if ( ! hash) return std::unexpected(hash.error());
        items.push_back(*hash);
    }
    return items;
}

expect<void> write_bytes_seq_u16(byte_writer& sink, std::vector<data_chunk> const& items) {
    if (items.size() > b0_64k_max) {
        return std::unexpected(error::operation_failed);
    }
    if (auto r = sink.write_little_endian<uint16_t>(static_cast<uint16_t>(items.size())); ! r) return r;
    for (auto const& item : items) {
        if (auto r = write_bytes_u24(sink, byte_span{item}); ! r) return r;
    }
    return {};
}

expect<std::vector<data_chunk>> read_bytes_seq_u16(byte_reader& source) {
    auto const count = source.read_little_endian<uint16_t>();
    if ( ! count) return std::unexpected(count.error());
    std::vector<data_chunk> items;
    items.reserve(*count);
    for (uint16_t i = 0; i < *count; ++i) {
        auto const bytes = read_bytes_u24(source);
        if ( ! bytes) return std::unexpected(bytes.error());
        items.emplace_back(bytes->begin(), bytes->end());
    }
    return items;
}

} // namespace kth::node::sv2
