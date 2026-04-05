// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/send_addrv2.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

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

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<send_addrv2> send_addrv2::from_data(byte_reader& reader, uint32_t version) {
    if (version < send_addrv2::version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    auto const insufficient_version = false;
    return send_addrv2(insufficient_version);
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk send_addrv2::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void send_addrv2::to_data(uint32_t /*version*/, data_sink& /*stream*/) const {
    // Empty message - no payload
}

void send_addrv2::to_data(uint32_t /*version*/, writer& /*sink*/) const {
    // Empty message - no payload
}

size_t send_addrv2::serialized_size(uint32_t version) const {
    return send_addrv2::satoshi_fixed_size(version);
}

} // namespace kth::domain::message
