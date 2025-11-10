// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/databases/header_abla_entry.hpp>

#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::database {

data_chunk to_data_with_abla_state(domain::chain::block const& block) {
    data_chunk data;
    auto const size = block.header().serialized_size(true) + 8 + 8 + 8;
    data.reserve(size);
    data_sink ostream(data);
    to_data_with_abla_state(ostream, block);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void to_data_with_abla_state(std::ostream& stream, domain::chain::block const& block) {
    ostream_writer sink(stream);
    to_data_with_abla_state(sink, block);
}

expect<header_with_abla_state_t> get_header_and_abla_state_from_data(byte_reader& reader) {
    auto header = domain::chain::header::from_data(reader, true);
    if ( ! header) {
        return std::unexpected(header.error());
    }

    auto const block_size = reader.read_little_endian<uint64_t>();
    if ( ! block_size) {
        return std::make_tuple(std::move(*header), 0, 0, 0);
    }

    auto const control_block_size = reader.read_little_endian<uint64_t>();
    if ( ! control_block_size) {
        return std::make_tuple(std::move(*header), 0, 0, 0);
    }

    auto const elastic_buffer_size = reader.read_little_endian<uint64_t>();
    if ( ! elastic_buffer_size) {
        return std::make_tuple(std::move(*header), 0, 0, 0);
    }

    return std::make_tuple(std::move(*header), *block_size, *control_block_size, *elastic_buffer_size);
}


} // namespace kth::database
