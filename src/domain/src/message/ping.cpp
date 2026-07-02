// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/ping.hpp>

// #include <kth/domain/message/version.hpp>
namespace kth::domain::message {

std::string const ping::command = "ping";
uint32_t const ping::version_minimum = version::level::minimum;
uint32_t const ping::version_maximum = version::level::maximum;

size_t ping::satoshi_fixed_size(uint32_t version) {
    return version < version::level::bip31 ? 0 : sizeof(nonce_);
}

//TODO(fernando): nonceless_ is never used! Check it!
ping::ping(uint64_t nonce, bool nonceless /* = false */)
    : nonce_(nonce), valid_(true), nonceless_(nonceless)
{}

bool ping::operator==(ping const& x) const {
    // Nonce should be zero if not used.
    return (nonce_ == x.nonce_);
}

bool ping::operator!=(ping const& x) const {
    // Nonce should be zero if not used.
    return !(*this == x);
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<ping> ping::from_data(byte_reader& reader, uint32_t version) {
    auto const nonceless = (version < version::level::bip31);
    if (nonceless) {
        return ping(0, true);
    }
    auto const nonce = reader.read_little_endian<uint64_t>();
    if ( ! nonce) {
        return std::unexpected(nonce.error());
    }
    return ping(*nonce, false);
}

// Serialization.
//-----------------------------------------------------------------------------



bool ping::is_valid() const {
    return valid_ || nonceless_ || nonce_ != 0;
}

void ping::reset() {
    nonce_ = 0;
    nonceless_ = false;
    valid_ = false;
}

size_t ping::serialized_size(uint32_t version) const {
    return satoshi_fixed_size(version);
}

uint64_t ping::nonce() const {
    return nonce_;
}

void ping::set_nonce(uint64_t value) {
    nonce_ = value;
}

expect<void> ping::to_data(byte_writer& writer, uint32_t version) const {
        if (version >= version::level::bip31) {
            if (auto r = writer.write_little_endian<uint64_t>(nonce_); ! r) return r;
        }
        return {};
}

} // namespace kth::domain::message
