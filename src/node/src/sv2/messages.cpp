// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/messages.hpp>

#include <utility>

#include <kth/node/sv2/primitives.hpp>

namespace kth::node::sv2 {

// --- CoinbaseOutputConstraints (0x70) ---------------------------------------

expect<void> coinbase_output_constraints::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(coinbase_output_max_additional_size); ! r) return r;
    return sink.write_little_endian<uint16_t>(coinbase_output_max_additional_sigops);
}

expect<coinbase_output_constraints> coinbase_output_constraints::from_data(byte_reader& source) {
    auto const size = source.read_little_endian<uint32_t>();
    if ( ! size) return std::unexpected(size.error());
    auto const sigops = source.read_little_endian<uint16_t>();
    if ( ! sigops) return std::unexpected(sigops.error());
    return coinbase_output_constraints{*size, *sigops};
}

// --- SetNewPrevHash (0x72) --------------------------------------------------

expect<void> set_new_prev_hash::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint64_t>(template_id); ! r) return r;
    if (auto r = sink.write_bytes(byte_span{prev_hash}); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(header_timestamp); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(n_bits); ! r) return r;
    return sink.write_bytes(byte_span{target});
}

expect<set_new_prev_hash> set_new_prev_hash::from_data(byte_reader& source) {
    auto const template_id = source.read_little_endian<uint64_t>();
    if ( ! template_id) return std::unexpected(template_id.error());
    auto const prev_hash = source.read_array<hash_size>();
    if ( ! prev_hash) return std::unexpected(prev_hash.error());
    auto const header_timestamp = source.read_little_endian<uint32_t>();
    if ( ! header_timestamp) return std::unexpected(header_timestamp.error());
    auto const n_bits = source.read_little_endian<uint32_t>();
    if ( ! n_bits) return std::unexpected(n_bits.error());
    auto const target = source.read_array<hash_size>();
    if ( ! target) return std::unexpected(target.error());
    return set_new_prev_hash{*template_id, *prev_hash, *header_timestamp, *n_bits, *target};
}

// --- RequestTransactionData (0x73) ------------------------------------------

expect<void> request_transaction_data::to_data(byte_writer& sink) const {
    return sink.write_little_endian<uint64_t>(template_id);
}

expect<request_transaction_data> request_transaction_data::from_data(byte_reader& source) {
    auto const template_id = source.read_little_endian<uint64_t>();
    if ( ! template_id) return std::unexpected(template_id.error());
    return request_transaction_data{*template_id};
}

// --- RequestTransactionData.Error (0x75) ------------------------------------

expect<void> request_transaction_data_error::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint64_t>(template_id); ! r) return r;
    return write_string_u8(sink, error_code);
}

expect<request_transaction_data_error> request_transaction_data_error::from_data(byte_reader& source) {
    auto const template_id = source.read_little_endian<uint64_t>();
    if ( ! template_id) return std::unexpected(template_id.error());
    auto error_code = read_string_u8(source);
    if ( ! error_code) return std::unexpected(error_code.error());
    return request_transaction_data_error{*template_id, std::move(*error_code)};
}

// --- SubmitSolution (0x76) --------------------------------------------------

expect<void> submit_solution::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint64_t>(template_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(version); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(header_timestamp); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(header_nonce); ! r) return r;
    return write_bytes_u16(sink, byte_span{coinbase_tx});
}

expect<submit_solution> submit_solution::from_data(byte_reader& source) {
    auto const template_id = source.read_little_endian<uint64_t>();
    if ( ! template_id) return std::unexpected(template_id.error());
    auto const version = source.read_little_endian<uint32_t>();
    if ( ! version) return std::unexpected(version.error());
    auto const header_timestamp = source.read_little_endian<uint32_t>();
    if ( ! header_timestamp) return std::unexpected(header_timestamp.error());
    auto const header_nonce = source.read_little_endian<uint32_t>();
    if ( ! header_nonce) return std::unexpected(header_nonce.error());
    auto const coinbase_tx = read_bytes_u16(source);
    if ( ! coinbase_tx) return std::unexpected(coinbase_tx.error());
    return submit_solution{*template_id, *version, *header_timestamp, *header_nonce,
        data_chunk(coinbase_tx->begin(), coinbase_tx->end())};
}

// --- NewTemplate (0x71) -----------------------------------------------------

expect<void> new_template::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint64_t>(template_id); ! r) return r;
    if (auto r = sink.write_byte(future_template ? 1 : 0); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(version); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(coinbase_tx_version); ! r) return r;
    if (auto r = write_bytes_u8(sink, byte_span{coinbase_prefix}); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(coinbase_tx_input_sequence); ! r) return r;
    if (auto r = sink.write_little_endian<uint64_t>(coinbase_tx_value_remaining); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(coinbase_tx_outputs_count); ! r) return r;
    if (auto r = write_bytes_u16(sink, byte_span{coinbase_tx_outputs}); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(coinbase_tx_locktime); ! r) return r;
    return write_hash_seq_u8(sink, merkle_path);
}

expect<new_template> new_template::from_data(byte_reader& source) {
    auto const template_id = source.read_little_endian<uint64_t>();
    if ( ! template_id) return std::unexpected(template_id.error());
    auto const future_template = source.read_byte();
    if ( ! future_template) return std::unexpected(future_template.error());
    auto const version = source.read_little_endian<uint32_t>();
    if ( ! version) return std::unexpected(version.error());
    auto const coinbase_tx_version = source.read_little_endian<uint32_t>();
    if ( ! coinbase_tx_version) return std::unexpected(coinbase_tx_version.error());
    auto const coinbase_prefix = read_bytes_u8(source);
    if ( ! coinbase_prefix) return std::unexpected(coinbase_prefix.error());
    auto const coinbase_tx_input_sequence = source.read_little_endian<uint32_t>();
    if ( ! coinbase_tx_input_sequence) return std::unexpected(coinbase_tx_input_sequence.error());
    auto const coinbase_tx_value_remaining = source.read_little_endian<uint64_t>();
    if ( ! coinbase_tx_value_remaining) return std::unexpected(coinbase_tx_value_remaining.error());
    auto const coinbase_tx_outputs_count = source.read_little_endian<uint32_t>();
    if ( ! coinbase_tx_outputs_count) return std::unexpected(coinbase_tx_outputs_count.error());
    auto const coinbase_tx_outputs = read_bytes_u16(source);
    if ( ! coinbase_tx_outputs) return std::unexpected(coinbase_tx_outputs.error());
    auto const coinbase_tx_locktime = source.read_little_endian<uint32_t>();
    if ( ! coinbase_tx_locktime) return std::unexpected(coinbase_tx_locktime.error());
    auto merkle_path = read_hash_seq_u8(source);
    if ( ! merkle_path) return std::unexpected(merkle_path.error());
    return new_template{
        *template_id, *future_template != 0, *version, *coinbase_tx_version,
        data_chunk(coinbase_prefix->begin(), coinbase_prefix->end()),
        *coinbase_tx_input_sequence, *coinbase_tx_value_remaining, *coinbase_tx_outputs_count,
        data_chunk(coinbase_tx_outputs->begin(), coinbase_tx_outputs->end()),
        *coinbase_tx_locktime, std::move(*merkle_path)};
}

// --- RequestTransactionData.Success (0x74) ----------------------------------

expect<void> request_transaction_data_success::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint64_t>(template_id); ! r) return r;
    if (auto r = write_bytes_u16(sink, byte_span{excess_data}); ! r) return r;
    return write_bytes_seq_u16(sink, transaction_list);
}

expect<request_transaction_data_success> request_transaction_data_success::from_data(byte_reader& source) {
    auto const template_id = source.read_little_endian<uint64_t>();
    if ( ! template_id) return std::unexpected(template_id.error());
    auto const excess_data = read_bytes_u16(source);
    if ( ! excess_data) return std::unexpected(excess_data.error());
    auto transaction_list = read_bytes_seq_u16(source);
    if ( ! transaction_list) return std::unexpected(transaction_list.error());
    return request_transaction_data_success{
        *template_id,
        data_chunk(excess_data->begin(), excess_data->end()),
        std::move(*transaction_list)};
}

} // namespace kth::node::sv2
