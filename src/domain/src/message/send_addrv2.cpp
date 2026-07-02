// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/send_addrv2.hpp>

#include <kth/domain/message/version.hpp>
namespace kth::domain::message {

std::string const send_addrv2::command = "sendaddrv2";
uint32_t const send_addrv2::version_minimum = version::level::feature_negotiation;
uint32_t const send_addrv2::version_maximum = version::level::maximum;

size_t send_addrv2::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

// protected
send_addrv2::send_addrv2(bool insufficient_version)
    : insufficient_version_(insufficient_version) {
}

bool send_addrv2::is_valid() const {
    return !insufficient_version_;
}

// This is again a default instance so is invalid.
void send_addrv2::reset() {
    insufficient_version_ = true;
}

// Serialization.
//-----------------------------------------------------------------------------

// static
expect<send_addrv2> send_addrv2::from_data(byte_reader& reader, uint32_t version) {
    if (version < send_addrv2::version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    auto const insufficient_version = false;
    return send_addrv2(insufficient_version);
}

expect<void> send_addrv2::to_data(byte_writer& /*writer*/, uint32_t /*version*/) const {
    // Empty message - no payload
    return {};
}

size_t send_addrv2::serialized_size(uint32_t version) const {
    return send_addrv2::satoshi_fixed_size(version);
}

} // namespace kth::domain::message
