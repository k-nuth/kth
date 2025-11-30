// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/xversion.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const xversion::command = "xversion";
uint32_t const message::xversion::version_minimum = version::level::minimum;
uint32_t const message::xversion::version_maximum = version::level::maximum;

bool xversion::operator==(xversion const& x) const {
    return true;
}

bool xversion::operator!=(xversion const& x) const {
    return !(*this == x);
}

bool xversion::is_valid() const {
    return true;
}

void xversion::reset() {}


// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<xversion> xversion::from_data(byte_reader& reader, uint32_t version) {
    //TODO(fernando): we are skiping the message for the moment.
    reader.skip_remaining();
    return xversion();
}

// Serialization.
//-----------------------------------------------------------------------------

// data_chunk xversion::to_data(uint32_t version) const {
//     data_chunk data;
//     auto const size = serialized_size(version);
//     data.reserve(size);
//     data_sink ostream(data);
//     to_data(version, ostream);
//     ostream.flush();
//     KTH_ASSERT(data.size() == size);
//     return data;
// }

// void xversion::to_data(uint32_t version, data_sink& stream) const {
//     ostream_writer sink_w(stream);
//     to_data(version, sink_w);
// }

size_t xversion::serialized_size(uint32_t version) const {
    return 0;
}

} // namespace kth::domain::message
