// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/xverack.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const xverack::command = "xverack";
uint32_t const xverack::version_minimum = version::level::minimum;
uint32_t const xverack::version_maximum = version::level::maximum;

bool xverack::is_valid() const {
    return true;
}

void xverack::reset() {
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<xverack> xverack::from_data(byte_reader& /*reader*/, uint32_t /*version*/) {
    return xverack();
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk xverack::to_data(uint32_t version) const {
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
void xverack::to_data(uint32_t /*version*/, data_sink& /*stream*/) const {}

size_t xverack::serialized_size(uint32_t version) const {
    return xverack::satoshi_fixed_size(version);
}

size_t xverack::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

} // namespace kth::domain::message
