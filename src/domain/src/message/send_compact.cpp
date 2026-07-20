// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/send_compact.hpp>

#include <cstdint>

#include <kth/domain/message/version.hpp>
namespace kth::domain::message {

std::string const send_compact::command = "sendcmpct";
uint32_t const send_compact::version_minimum = version::level::bip152;
uint32_t const send_compact::version_maximum = version::level::bip152;

size_t send_compact::satoshi_fixed_size(uint32_t /*version*/) {
    return 9;
}

send_compact::send_compact(bool high_bandwidth_mode, uint64_t version)
    : high_bandwidth_mode_(high_bandwidth_mode),
      version_(version) {
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<send_compact> send_compact::from_data(byte_reader& reader, uint32_t version) {
    auto const mode = reader.read_byte();
    if ( ! mode) {
        return std::unexpected(mode.error());
    }
    if (*mode > 1) {
        return std::unexpected(error::illegal_value);
    }
    auto const protocol_version = reader.read_little_endian<uint64_t>();
    if ( ! protocol_version) {
        return std::unexpected(protocol_version.error());
    }
    if (version < send_compact::version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    return send_compact(*mode, *protocol_version);
}


// Serialization.
//-----------------------------------------------------------------------------



size_t send_compact::serialized_size(uint32_t version) const {
    return send_compact::satoshi_fixed_size(version);
}

bool send_compact::high_bandwidth_mode() const {
    return high_bandwidth_mode_;
}

void send_compact::set_high_bandwidth_mode(bool mode) {
    high_bandwidth_mode_ = mode;
}

uint64_t send_compact::version() const {
    return version_;
}

void send_compact::set_version(uint64_t version) {
    version_ = version;
}

expect<void> send_compact::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_byte(uint8_t(high_bandwidth_mode_)); ! r) return r;
        if (auto r = writer.write_little_endian<uint64_t>(this->version_); ! r) return r;
        return {};
}

} // namespace kth::domain::message
