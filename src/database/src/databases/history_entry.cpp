// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/databases/history_entry.hpp>

#include <cstddef>
#include <cstdint>


namespace kth::database {

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<history_entry> history_entry::from_data(byte_reader& reader) {
    auto const id = reader.read_little_endian<uint64_t>();
    if ( ! id) {
        return std::unexpected(id.error());
    }

    auto const point = domain::chain::point::from_data(reader, false);
    if ( ! point) {
        return std::unexpected(point.error());
    }

    auto const point_kind = reader.read_byte();
    if ( ! point_kind) {
        return std::unexpected(point_kind.error());
    }

    auto const height = reader.read_little_endian<uint32_t>();
    if ( ! height) {
        return std::unexpected(height.error());
    }

    auto const index = reader.read_little_endian<uint32_t>();
    if ( ! index) {
        return std::unexpected(index.error());
    }

    auto const value_or_checksum = reader.read_little_endian<uint64_t>();
    if ( ! value_or_checksum) {
        return std::unexpected(value_or_checksum.error());
    }

    return history_entry(
        *id,
        *point,
        domain::chain::point_kind(*point_kind),
        *height,
        *index,
        *value_or_checksum
    );
}

// Serialization.
//-----------------------------------------------------------------------------

// static
data_chunk history_entry::factory_to_data(uint64_t id, domain::chain::point const& point, domain::chain::point_kind kind, uint32_t height, uint32_t index, uint64_t value_or_checksum) {
    auto const size = serialized_size(point);
    data_chunk data(size);
    byte_writer writer(data);
    auto const r = factory_to_data(writer, id, point, kind, height, index, value_or_checksum);
    KTH_ASSERT(r.has_value());
    return data;
}

data_chunk history_entry::to_data() const {
    auto const size = serialized_size(point_);
    data_chunk data(size);
    byte_writer writer(data);
    auto const r = to_data(writer);
    KTH_ASSERT(r.has_value());
    return data;
}

} // namespace kth::database
