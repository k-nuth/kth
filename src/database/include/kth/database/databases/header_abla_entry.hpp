// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_HEADER_ABLA_ENTRY_HPP_
#define KTH_DATABASE_HEADER_ABLA_ENTRY_HPP_

#include <kth/domain.hpp>
#include <kth/database/define.hpp>

namespace kth::database {

using header_with_abla_state_t = std::tuple<domain::chain::header, uint64_t, uint64_t, uint64_t>;

data_chunk to_data_with_abla_state(domain::chain::block const& block, domain::chain::abla::state const& abla_state);
data_chunk to_data_header_only(domain::chain::header const& header);
data_chunk to_data_header_with_abla_state(domain::chain::header const& header, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size);

expect<void> to_data_with_abla_state(byte_writer& writer, domain::chain::block const& block, domain::chain::abla::state const& abla_state);
expect<void> to_data_header_only(byte_writer& writer, domain::chain::header const& header);
expect<void> to_data_header_with_abla_state(byte_writer& writer, domain::chain::header const& header, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size);

expect<header_with_abla_state_t> get_header_and_abla_state_from_data(byte_reader& reader);


} // namespace kth::database

#endif // KTH_DATABASE_HEADER_ABLA_ENTRY_HPP_
