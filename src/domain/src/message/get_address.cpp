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


size_t get_address::serialized_size(uint32_t version) const {
    return get_address::satoshi_fixed_size(version);
}

size_t get_address::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

} // namespace kth::domain::message
