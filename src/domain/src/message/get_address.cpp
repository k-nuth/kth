// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/get_address.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const get_address::command = "getaddr";
uint32_t const get_address::version_minimum = version::level::minimum;
uint32_t const get_address::version_maximum = version::level::maximum;

bool get_address::is_valid() const {
    return true;
}

void get_address::reset() {}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<get_address> get_address::from_data(byte_reader& reader, uint32_t /*version*/) {
    return get_address();
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk get_address::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void get_address::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

size_t get_address::serialized_size(uint32_t version) const {
    return get_address::satoshi_fixed_size(version);
}

size_t get_address::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

} // namespace kth::domain::message
