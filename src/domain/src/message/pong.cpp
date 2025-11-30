// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/pong.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

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

data_chunk pong::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void pong::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

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

} // namespace kth::domain::message
