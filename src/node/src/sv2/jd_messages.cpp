// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_messages.hpp>

#include <utility>

#include <kth/node/sv2/primitives.hpp>

namespace kth::node::sv2 {

namespace {

template <typename Message>
expect<Message> complete(byte_reader& source, Message message) {
    if ( ! source.is_exhausted()) return std::unexpected(error::operation_failed);
    return message;
}

expect<void> write_hash_seq_u16(byte_writer& sink, std::vector<hash_digest> const& items) {
    if (items.size() > b0_64k_max) return std::unexpected(error::operation_failed);
    if (auto r = sink.write_little_endian<uint16_t>(static_cast<uint16_t>(items.size())); ! r) return r;
    for (auto const& hash : items) {
        if (auto r = sink.write_bytes(byte_span{hash}); ! r) return r;
    }
    return {};
}

expect<std::vector<hash_digest>> read_hash_seq_u16(byte_reader& source) {
    auto const count = source.read_little_endian<uint16_t>();
    if ( ! count) return std::unexpected(count.error());
    if (*count > source.remaining_size() / hash_size) return std::unexpected(error::operation_failed);
    std::vector<hash_digest> result;
    result.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto const hash = source.read_array<hash_size>();
        if ( ! hash) return std::unexpected(hash.error());
        result.push_back(*hash);
    }
    return result;
}

} // namespace

expect<void> allocate_mining_job_token::to_data(byte_writer& sink) const {
    if (auto r = write_string_u8(sink, user_identifier); ! r) return r;
    return sink.write_little_endian<uint32_t>(request_id);
}

expect<allocate_mining_job_token> allocate_mining_job_token::from_data(byte_reader& source) {
    auto user_identifier = read_string_u8(source);
    if ( ! user_identifier) return std::unexpected(user_identifier.error());
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    return complete(source, allocate_mining_job_token{std::move(*user_identifier), *request_id});
}

expect<void> allocate_mining_job_token_success::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    if (auto r = write_bytes_u8(sink, mining_job_token); ! r) return r;
    return write_bytes_u16(sink, coinbase_tx_outputs);
}

expect<allocate_mining_job_token_success> allocate_mining_job_token_success::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto const mining_job_token = read_bytes_u8(source);
    if ( ! mining_job_token) return std::unexpected(mining_job_token.error());
    auto const coinbase_tx_outputs = read_bytes_u16(source);
    if ( ! coinbase_tx_outputs) return std::unexpected(coinbase_tx_outputs.error());
    return complete(source, allocate_mining_job_token_success{*request_id,
        data_chunk(mining_job_token->begin(), mining_job_token->end()),
        data_chunk(coinbase_tx_outputs->begin(), coinbase_tx_outputs->end())});
}

size_t declare_mining_job::serialized_size() const {
    return 4 + 1 + mining_job_token.size() + 4 + 2 + coinbase_tx_prefix.size()
        + 2 + coinbase_tx_suffix.size() + 2 + hash_size * wtxid_list.size() + 2 + excess_data.size();
}

expect<void> declare_mining_job::to_data(byte_writer& sink) const {
    // msg_length is a U24. The hash list of a full template can pass 1 MiB
    // and still be a legal frame.
    if (serialized_size() > jd_frame_max_payload) return std::unexpected(error::operation_failed);
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    if (auto r = write_bytes_u8(sink, mining_job_token); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(version); ! r) return r;
    if (auto r = write_bytes_u16(sink, coinbase_tx_prefix); ! r) return r;
    if (auto r = write_bytes_u16(sink, coinbase_tx_suffix); ! r) return r;
    if (auto r = write_hash_seq_u16(sink, wtxid_list); ! r) return r;
    return write_bytes_u16(sink, excess_data);
}

expect<declare_mining_job> declare_mining_job::from_data(byte_reader& source) {
    if (source.remaining_size() > jd_frame_max_payload) return std::unexpected(error::operation_failed);
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto const mining_job_token = read_bytes_u8(source);
    if ( ! mining_job_token) return std::unexpected(mining_job_token.error());
    auto const version = source.read_little_endian<uint32_t>();
    if ( ! version) return std::unexpected(version.error());
    auto const coinbase_tx_prefix = read_bytes_u16(source);
    if ( ! coinbase_tx_prefix) return std::unexpected(coinbase_tx_prefix.error());
    auto const coinbase_tx_suffix = read_bytes_u16(source);
    if ( ! coinbase_tx_suffix) return std::unexpected(coinbase_tx_suffix.error());
    auto wtxid_list = read_hash_seq_u16(source);
    if ( ! wtxid_list) return std::unexpected(wtxid_list.error());
    auto const excess_data = read_bytes_u16(source);
    if ( ! excess_data) return std::unexpected(excess_data.error());
    return complete(source, declare_mining_job{*request_id,
        data_chunk(mining_job_token->begin(), mining_job_token->end()), *version,
        data_chunk(coinbase_tx_prefix->begin(), coinbase_tx_prefix->end()),
        data_chunk(coinbase_tx_suffix->begin(), coinbase_tx_suffix->end()),
        std::move(*wtxid_list), data_chunk(excess_data->begin(), excess_data->end())});
}

expect<void> declare_mining_job_success::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    return write_bytes_u8(sink, new_mining_job_token);
}

expect<declare_mining_job_success> declare_mining_job_success::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto const token = read_bytes_u8(source);
    if ( ! token) return std::unexpected(token.error());
    return complete(source, declare_mining_job_success{*request_id, data_chunk(token->begin(), token->end())});
}

expect<void> declare_mining_job_error::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    if (auto r = write_string_u8(sink, error_code); ! r) return r;
    return write_bytes_u16(sink, error_details);
}

expect<declare_mining_job_error> declare_mining_job_error::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto error_code = read_string_u8(source);
    if ( ! error_code) return std::unexpected(error_code.error());
    auto const details = read_bytes_u16(source);
    if ( ! details) return std::unexpected(details.error());
    return complete(source, declare_mining_job_error{*request_id, std::move(*error_code),
        data_chunk(details->begin(), details->end())});
}

expect<void> provide_missing_transactions::to_data(byte_writer& sink) const {
    if (unknown_tx_position_list.size() > b0_64k_max) return std::unexpected(error::operation_failed);
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint16_t>(static_cast<uint16_t>(unknown_tx_position_list.size())); ! r) return r;
    for (auto const position : unknown_tx_position_list) {
        if (auto r = sink.write_little_endian<uint16_t>(position); ! r) return r;
    }
    return {};
}

expect<provide_missing_transactions> provide_missing_transactions::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto const count = source.read_little_endian<uint16_t>();
    if ( ! count) return std::unexpected(count.error());
    if (*count > source.remaining_size() / sizeof(uint16_t)) return std::unexpected(error::operation_failed);
    std::vector<uint16_t> positions;
    positions.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto const position = source.read_little_endian<uint16_t>();
        if ( ! position) return std::unexpected(position.error());
        positions.push_back(*position);
    }
    return complete(source, provide_missing_transactions{*request_id, std::move(positions)});
}

size_t provide_missing_transactions_success::serialized_size() const {
    size_t size = 4 + 2;
    for (auto const& tx : transaction_list) size += 3 + tx.size();
    return size;
}

expect<void> provide_missing_transactions_success::to_data(byte_writer& sink) const {
    // The transactions the server asked for, not the whole template. A legal
    // reply can be larger than the 1 MiB inbound cap and at most one U24.
    if (serialized_size() > jd_frame_max_payload) return std::unexpected(error::operation_failed);
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    return write_bytes_seq_u16(sink, transaction_list);
}

expect<provide_missing_transactions_success> provide_missing_transactions_success::from_data(byte_reader& source) {
    if (source.remaining_size() > jd_frame_max_payload) return std::unexpected(error::operation_failed);
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto const count = source.read_little_endian<uint16_t>();
    if ( ! count) return std::unexpected(count.error());
    if (*count > source.remaining_size() / 3) return std::unexpected(error::operation_failed);
    std::vector<data_chunk> transactions;
    transactions.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto const tx = read_bytes_u24(source);
        if ( ! tx) return std::unexpected(tx.error());
        transactions.emplace_back(tx->begin(), tx->end());
    }
    return complete(source, provide_missing_transactions_success{*request_id, std::move(transactions)});
}

expect<void> push_solution::to_data(byte_writer& sink) const {
    if (extranonce.size() > 32) return std::unexpected(error::operation_failed);
    if (auto r = write_bytes_u8(sink, extranonce); ! r) return r;
    if (auto r = sink.write_bytes(byte_span{prev_hash}); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(nonce); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(ntime); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(nbits); ! r) return r;
    return sink.write_little_endian<uint32_t>(version);
}

expect<push_solution> push_solution::from_data(byte_reader& source) {
    auto const extranonce = read_bytes_u8(source);
    if ( ! extranonce) return std::unexpected(extranonce.error());
    if (extranonce->size() > 32) return std::unexpected(error::operation_failed);
    auto const prev_hash = source.read_array<hash_size>();
    if ( ! prev_hash) return std::unexpected(prev_hash.error());
    auto const nonce = source.read_little_endian<uint32_t>();
    if ( ! nonce) return std::unexpected(nonce.error());
    auto const ntime = source.read_little_endian<uint32_t>();
    if ( ! ntime) return std::unexpected(ntime.error());
    auto const nbits = source.read_little_endian<uint32_t>();
    if ( ! nbits) return std::unexpected(nbits.error());
    auto const version = source.read_little_endian<uint32_t>();
    if ( ! version) return std::unexpected(version.error());
    return complete(source, push_solution{data_chunk(extranonce->begin(), extranonce->end()),
        *prev_hash, *nonce, *ntime, *nbits, *version});
}

size_t set_custom_mining_job::serialized_size() const {
    return 4 + 4 + 1 + mining_job_token.size() + 4 + hash_size + 4 + 4 + 4
        + 1 + coinbase_prefix.size() + 4 + 2 + coinbase_tx_outputs.size() + 4
        + 1 + hash_size * merkle_path.size();
}

expect<void> set_custom_mining_job::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    if (auto r = write_bytes_u8(sink, mining_job_token); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(version); ! r) return r;
    if (auto r = sink.write_bytes(byte_span{prev_hash}); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(ntime_start); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(nbits); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(coinbase_tx_version); ! r) return r;
    if (auto r = write_bytes_u8(sink, coinbase_prefix); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(coinbase_tx_input_sequence); ! r) return r;
    if (auto r = write_bytes_u16(sink, coinbase_tx_outputs); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(coinbase_tx_locktime); ! r) return r;
    return write_hash_seq_u8(sink, merkle_path);
}

expect<set_custom_mining_job> set_custom_mining_job::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto const token = read_bytes_u8(source);
    if ( ! token) return std::unexpected(token.error());
    auto const version = source.read_little_endian<uint32_t>();
    if ( ! version) return std::unexpected(version.error());
    auto const prev_hash = source.read_array<hash_size>();
    if ( ! prev_hash) return std::unexpected(prev_hash.error());
    auto const ntime_start = source.read_little_endian<uint32_t>();
    if ( ! ntime_start) return std::unexpected(ntime_start.error());
    auto const nbits = source.read_little_endian<uint32_t>();
    if ( ! nbits) return std::unexpected(nbits.error());
    auto const coinbase_tx_version = source.read_little_endian<uint32_t>();
    if ( ! coinbase_tx_version) return std::unexpected(coinbase_tx_version.error());
    auto const coinbase_prefix = read_bytes_u8(source);
    if ( ! coinbase_prefix) return std::unexpected(coinbase_prefix.error());
    auto const input_sequence = source.read_little_endian<uint32_t>();
    if ( ! input_sequence) return std::unexpected(input_sequence.error());
    auto const outputs = read_bytes_u16(source);
    if ( ! outputs) return std::unexpected(outputs.error());
    auto const locktime = source.read_little_endian<uint32_t>();
    if ( ! locktime) return std::unexpected(locktime.error());
    auto merkle_path = read_hash_seq_u8(source);
    if ( ! merkle_path) return std::unexpected(merkle_path.error());
    return complete(source, set_custom_mining_job{*channel_id, *request_id,
        data_chunk(token->begin(), token->end()), *version, *prev_hash, *ntime_start,
        *nbits, *coinbase_tx_version, data_chunk(coinbase_prefix->begin(), coinbase_prefix->end()),
        *input_sequence, data_chunk(outputs->begin(), outputs->end()), *locktime, std::move(*merkle_path)});
}

expect<void> set_custom_mining_job_success::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    return sink.write_little_endian<uint32_t>(job_id);
}

expect<set_custom_mining_job_success> set_custom_mining_job_success::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto const job_id = source.read_little_endian<uint32_t>();
    if ( ! job_id) return std::unexpected(job_id.error());
    return complete(source, set_custom_mining_job_success{*channel_id, *request_id, *job_id});
}

expect<void> set_custom_mining_job_error::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    return write_string_u8(sink, error_code);
}

expect<set_custom_mining_job_error> set_custom_mining_job_error::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto error_code = read_string_u8(source);
    if ( ! error_code) return std::unexpected(error_code.error());
    return complete(source, set_custom_mining_job_error{*channel_id, *request_id, std::move(*error_code)});
}

} // namespace kth::node::sv2
