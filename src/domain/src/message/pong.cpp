// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/pong.hpp>

#include <kth/domain/message/version.hpp>
namespace kth::domain::message {

std::string const pong::command = "pong";
uint32_t const pong::version_minimum = version::level::minimum;
uint32_t const pong::version_maximum = version::level::maximum;

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<pong> pong::from_data(byte_reader& reader, uint32_t version) {
    auto const nonce = reader.read_little_endian<uint64_t>();
    if ( ! nonce) {
        return std::unexpected(nonce.error());
    }
    return pong(*nonce);
}

// Serialization.
//-----------------------------------------------------------------------------

expect<void> pong::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_little_endian<uint64_t>(nonce_); ! r) return r;
        return {};
}

} // namespace kth::domain::message
