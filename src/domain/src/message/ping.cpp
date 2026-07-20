// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/ping.hpp>

// #include <kth/domain/message/version.hpp>
namespace kth::domain::message {

std::string const ping::command = "ping";
uint32_t const ping::version_minimum = version::level::minimum;
uint32_t const ping::version_maximum = version::level::maximum;

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<ping> ping::from_data(byte_reader& reader, uint32_t version) {
    // Before BIP31 a ping carries no nonce; the wire size and layout are
    // driven by `version` in `to_data` / `satoshi_fixed_size`, so a zero nonce
    // is all that is needed here.
    if (version < version::level::bip31) {
        return ping(0);
    }
    auto const nonce = reader.read_little_endian<uint64_t>();
    if ( ! nonce) {
        return std::unexpected(nonce.error());
    }
    return ping(*nonce);
}

// Serialization.
//-----------------------------------------------------------------------------

expect<void> ping::to_data(byte_writer& writer, uint32_t version) const {
        if (version >= version::level::bip31) {
            if (auto r = writer.write_little_endian<uint64_t>(nonce_); ! r) return r;
        }
        return {};
}

} // namespace kth::domain::message
