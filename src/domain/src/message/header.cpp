// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/header.hpp>

#include <cstddef>
#include <cstdint>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const header::command = "headers";
uint32_t const header::version_minimum = version::level::minimum;
uint32_t const header::version_maximum = version::level::maximum;

size_t header::satoshi_fixed_size(uint32_t version) {
    auto const canonical = (version == version::level::canonical);
    return chain::header::satoshi_fixed_size() +
           (canonical ? 0 : infrastructure::message::variable_uint_size(0));
}

// Deserialization.
//-----------------------------------------------------------------------------

expect<header> header::from_data(byte_reader& reader, uint32_t version) {
    auto chain_header = chain::header::from_data(reader, true);  // wire=true
    if ( ! chain_header) {
        return std::unexpected(chain_header.error());
    }

    // The header message must trail a zero byte (yes, it's stoopid).
    // bitcoin.org/en/developer-reference#headers
    if (version != version::level::canonical) {
        auto const trail = reader.read_byte();
        if ( ! trail) {
            return std::unexpected(trail.error());
        }
        if (*trail != 0x00) {
            return std::unexpected(error::version_too_new);
        }
    }

    return header{*chain_header};
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk header::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void header::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

size_t header::serialized_size(uint32_t version) const {
    return satoshi_fixed_size(version);
}

} // namespace kth::domain::message
