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

} // namespace kth::node::sv2
