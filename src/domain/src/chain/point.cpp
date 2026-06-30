// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/point.hpp>

#include <cstdint>
#include <utility>

#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::chain {

constexpr auto store_point_size = std::tuple_size<point>::value;

// Deserialization.
//-----------------------------------------------------------------------------

expect<point> point::from_data(byte_reader& reader, bool wire) {
    auto const hash = read_hash(reader);
    if ( ! hash) {
        return std::unexpected(hash.error());
    }

    if ( ! wire) {
        auto const index = reader.read_little_endian<uint16_t>();
        if ( ! index) {
            return std::unexpected(index.error());
        }
        if (*index == max_uint16) {
            return point {*hash, null_index};
        }
        return point {*hash, *index};
    }

    auto const index = reader.read_little_endian<uint32_t>();
    if ( ! index) {
        return std::unexpected(index.error());
    }
    return point {*hash, *index};
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk point::to_data(bool wire) const {
    data_chunk data;
    auto const size = serialized_size(wire);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream, wire);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void point::to_data(data_sink& stream, bool wire) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, wire);
}

// Iterator.
//-----------------------------------------------------------------------------

point_iterator point::begin() const {
    return {*this};
}

point_iterator point::end() const {
    return {*this, unsigned(store_point_size)};
}

} // namespace kth::domain::chain
