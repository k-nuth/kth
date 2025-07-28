// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_HEADER_ABLA_ENTRY_HPP_
#define KTH_DATABASE_HEADER_ABLA_ENTRY_HPP_

#include <kth/database/define.hpp>
#include <kth/domain.hpp>

namespace kth::database {

using header_with_abla_state_t = std::tuple<domain::chain::header, uint64_t, uint64_t, uint64_t>;

data_chunk to_data_with_abla_state(domain::chain::block const& block);
void to_data_with_abla_state(std::ostream& stream, domain::chain::block const& block);

template <typename W, KTH_IS_WRITER(W)>
void to_data_with_abla_state(W& sink, domain::chain::block const& block) {
    block.header().to_data(sink, true);

    if (block.validation.state) {
        sink.write_8_bytes_little_endian(block.validation.state->abla_state().block_size);
        sink.write_8_bytes_little_endian(block.validation.state->abla_state().control_block_size);
        sink.write_8_bytes_little_endian(block.validation.state->abla_state().elastic_buffer_size);
    } else {
        sink.write_8_bytes_little_endian(0);
        sink.write_8_bytes_little_endian(0);
        sink.write_8_bytes_little_endian(0);
    }
}

expect<header_with_abla_state_t> get_header_and_abla_state_from_data(byte_reader& reader);

}  // namespace kth::database

#endif  // KTH_DATABASE_HEADER_ABLA_ENTRY_HPP_
