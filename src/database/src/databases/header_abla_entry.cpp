// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/databases/header_abla_entry.hpp>

#include <cstddef>
#include <cstdint>

namespace kth::database {

expect<void> to_data_with_abla_state(byte_writer& writer, domain::chain::block const& block, domain::chain::abla::state const& abla_state) {
    if (auto r = block.header().to_data(writer, true); ! r) return r;
    auto const& a = abla_state;
    if (auto r = writer.write_little_endian<uint64_t>(a.block_size); ! r) return r;
    if (auto r = writer.write_little_endian<uint64_t>(a.control_block_size); ! r) return r;
    return writer.write_little_endian<uint64_t>(a.elastic_buffer_size);
}

expect<void> to_data_header_only(byte_writer& writer, domain::chain::header const& header) {
    if (auto r = header.to_data(writer, true); ! r) return r;
    // ABLA state is unknown for headers received before blocks
    // Store zeros, will be updated when block is received
    if (auto r = writer.write_little_endian<uint64_t>(0); ! r) return r;
    if (auto r = writer.write_little_endian<uint64_t>(0); ! r) return r;
    return writer.write_little_endian<uint64_t>(0);
}

expect<void> to_data_header_with_abla_state(byte_writer& writer, domain::chain::header const& header, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size) {
    if (auto r = header.to_data(writer, true); ! r) return r;
    if (auto r = writer.write_little_endian<uint64_t>(block_size); ! r) return r;
    if (auto r = writer.write_little_endian<uint64_t>(control_block_size); ! r) return r;
    return writer.write_little_endian<uint64_t>(elastic_buffer_size);
}

data_chunk to_data_with_abla_state(domain::chain::block const& block, domain::chain::abla::state const& abla_state) {
    auto const size = block.header().serialized_size(true) + 8 + 8 + 8;
    data_chunk data(size);
    byte_writer writer(data);
    auto const r = to_data_with_abla_state(writer, block, abla_state);
    KTH_ASSERT(r.has_value());
    return data;
}

data_chunk to_data_header_only(domain::chain::header const& header) {
    auto const size = header.serialized_size(true) + 8 + 8 + 8;
    data_chunk data(size);
    byte_writer writer(data);
    auto const r = to_data_header_only(writer, header);
    KTH_ASSERT(r.has_value());
    return data;
}

data_chunk to_data_header_with_abla_state(domain::chain::header const& header, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size) {
    auto const size = header.serialized_size(true) + 8 + 8 + 8;
    data_chunk data(size);
    byte_writer writer(data);
    auto const r = to_data_header_with_abla_state(writer, header, block_size, control_block_size, elastic_buffer_size);
    KTH_ASSERT(r.has_value());
    return data;
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
