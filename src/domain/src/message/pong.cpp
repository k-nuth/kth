// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/pong.hpp>

#include <kth/domain/message/version.hpp>
namespace kth::domain::message {

std::string const pong::command = "pong";
uint32_t const pong::version_minimum = version::level::minimum;
uint32_t const pong::version_maximum = version::level::maximum;

size_t pong::satoshi_fixed_size(uint32_t /*version*/) {
    return sizeof(nonce_);
}

pong::pong(uint64_t nonce)
    : nonce_(nonce), valid_(true) {
}

bool pong::operator==(pong const& x) const {
    return (nonce_ == x.nonce_);
}

bool pong::operator!=(pong const& x) const {
    return !(*this == x);
}


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



bool pong::is_valid() const {
    return valid_ || (nonce_ != 0);
}

void pong::reset() {
    nonce_ = 0;
    valid_ = false;
}

size_t pong::serialized_size(uint32_t version) const {
    return satoshi_fixed_size(version);
}

uint64_t pong::nonce() const {
    return nonce_;
}

void pong::set_nonce(uint64_t value) {
    nonce_ = value;
}

expect<void> pong::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_little_endian<uint64_t>(nonce_); ! r) return r;
        return {};
}

} // namespace kth::domain::message
