// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/send_headers.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const send_headers::command = "sendheaders";
uint32_t const send_headers::version_minimum = version::level::bip130;
uint32_t const send_headers::version_maximum = version::level::maximum;

size_t send_headers::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

// protected
send_headers::send_headers(bool insufficient_version)
    : insufficient_version_(insufficient_version) {
}

bool send_headers::is_valid() const {
    return !insufficient_version_;
}

// This is again a default instance so is invalid.
void send_headers::reset() {
    insufficient_version_ = true;
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<send_headers> send_headers::from_data(byte_reader& reader, uint32_t version) {
    if (version < send_headers::version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    auto const insufficient_version = false;
    return send_headers(insufficient_version);
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk send_headers::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

//TODO(fernando): empty?
void send_headers::to_data(uint32_t /*version*/, data_sink& /*stream*/) const {
}

size_t send_headers::serialized_size(uint32_t version) const {
    return send_headers::satoshi_fixed_size(version);
}

} // namespace kth::domain::message
