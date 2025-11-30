// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/ping.hpp>

// #include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

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

data_chunk ping::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void ping::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

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

} // namespace kth::domain::message
