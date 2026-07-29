// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/framing.hpp>

namespace kth::node::sv2 {

expect<void> write_u24(byte_writer& sink, uint32_t value) {
    if (value > u24_max) {
        return std::unexpected(error::operation_failed);
    }
    if (auto r = sink.write_byte(static_cast<uint8_t>(value)); ! r) return r;
    if (auto r = sink.write_byte(static_cast<uint8_t>(value >> 8)); ! r) return r;
    return sink.write_byte(static_cast<uint8_t>(value >> 16));
}

expect<uint32_t> read_u24(byte_reader& source) {
    auto const b0 = source.read_byte();
    if ( ! b0) return std::unexpected(b0.error());
    auto const b1 = source.read_byte();
    if ( ! b1) return std::unexpected(b1.error());
    auto const b2 = source.read_byte();
    if ( ! b2) return std::unexpected(b2.error());
    return static_cast<uint32_t>(*b0)
        | (static_cast<uint32_t>(*b1) << 8)
        | (static_cast<uint32_t>(*b2) << 16);
}

expect<void> message_header::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint16_t>(extension_type); ! r) return r;
    if (auto r = sink.write_byte(msg_type); ! r) return r;
    return write_u24(sink, msg_length);
}

expect<message_header> message_header::from_data(byte_reader& source) {
    auto const extension_type = source.read_little_endian<uint16_t>();
    if ( ! extension_type) return std::unexpected(extension_type.error());
    auto const msg_type = source.read_byte();
    if ( ! msg_type) return std::unexpected(msg_type.error());
    auto const msg_length = read_u24(source);
    if ( ! msg_length) return std::unexpected(msg_length.error());
    return message_header{*extension_type, *msg_type, *msg_length};
}

} // namespace kth::node::sv2
