// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/connection.hpp>

#include <utility>

#include <kth/node/sv2/primitives.hpp>

namespace kth::node::sv2 {

// --- SetupConnection (0x00) -------------------------------------------------

expect<void> setup_connection::to_data(byte_writer& sink) const {
    if (auto r = sink.write_byte(protocol); ! r) return r;
    if (auto r = sink.write_little_endian<uint16_t>(min_version); ! r) return r;
    if (auto r = sink.write_little_endian<uint16_t>(max_version); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(flags); ! r) return r;
    if (auto r = write_string_u8(sink, endpoint_host); ! r) return r;
    if (auto r = sink.write_little_endian<uint16_t>(endpoint_port); ! r) return r;
    if (auto r = write_string_u8(sink, vendor); ! r) return r;
    if (auto r = write_string_u8(sink, hardware_version); ! r) return r;
    if (auto r = write_string_u8(sink, firmware); ! r) return r;
    return write_string_u8(sink, device_id);
}

expect<setup_connection> setup_connection::from_data(byte_reader& source) {
    auto const protocol = source.read_byte();
    if ( ! protocol) return std::unexpected(protocol.error());
    auto const min_version = source.read_little_endian<uint16_t>();
    if ( ! min_version) return std::unexpected(min_version.error());
    auto const max_version = source.read_little_endian<uint16_t>();
    if ( ! max_version) return std::unexpected(max_version.error());
    auto const flags = source.read_little_endian<uint32_t>();
    if ( ! flags) return std::unexpected(flags.error());
    auto endpoint_host = read_string_u8(source);
    if ( ! endpoint_host) return std::unexpected(endpoint_host.error());
    auto const endpoint_port = source.read_little_endian<uint16_t>();
    if ( ! endpoint_port) return std::unexpected(endpoint_port.error());
    auto vendor = read_string_u8(source);
    if ( ! vendor) return std::unexpected(vendor.error());
    auto hardware_version = read_string_u8(source);
    if ( ! hardware_version) return std::unexpected(hardware_version.error());
    auto firmware = read_string_u8(source);
    if ( ! firmware) return std::unexpected(firmware.error());
    auto device_id = read_string_u8(source);
    if ( ! device_id) return std::unexpected(device_id.error());
    return setup_connection{
        *protocol, *min_version, *max_version, *flags,
        std::move(*endpoint_host), *endpoint_port,
        std::move(*vendor), std::move(*hardware_version),
        std::move(*firmware), std::move(*device_id)};
}

// --- SetupConnection.Success (0x01) -----------------------------------------

expect<void> setup_connection_success::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint16_t>(used_version); ! r) return r;
    return sink.write_little_endian<uint32_t>(flags);
}

expect<setup_connection_success> setup_connection_success::from_data(byte_reader& source) {
    auto const used_version = source.read_little_endian<uint16_t>();
    if ( ! used_version) return std::unexpected(used_version.error());
    auto const flags = source.read_little_endian<uint32_t>();
    if ( ! flags) return std::unexpected(flags.error());
    return setup_connection_success{*used_version, *flags};
}

// --- SetupConnection.Error (0x02) -------------------------------------------

expect<void> setup_connection_error::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(flags); ! r) return r;
    return write_string_u8(sink, error_code);
}

expect<setup_connection_error> setup_connection_error::from_data(byte_reader& source) {
    auto const flags = source.read_little_endian<uint32_t>();
    if ( ! flags) return std::unexpected(flags.error());
    auto error_code = read_string_u8(source);
    if ( ! error_code) return std::unexpected(error_code.error());
    return setup_connection_error{*flags, std::move(*error_code)};
}

} // namespace kth::node::sv2
