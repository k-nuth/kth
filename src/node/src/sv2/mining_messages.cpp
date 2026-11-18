// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/mining_messages.hpp>

#include <bit>
#include <utility>

#include <kth/node/sv2/primitives.hpp>

namespace kth::node::sv2 {

namespace {

constexpr size_t b0_32_max = 32;

expect<void> write_f32(byte_writer& sink, float value) {
    return sink.write_little_endian<uint32_t>(std::bit_cast<uint32_t>(value));
}

expect<float> read_f32(byte_reader& source) {
    auto const bits = source.read_little_endian<uint32_t>();
    if ( ! bits) return std::unexpected(bits.error());
    return std::bit_cast<float>(*bits);
}

expect<void> write_bool(byte_writer& sink, bool value) {
    return sink.write_byte(value ? uint8_t{1} : uint8_t{0});
}

expect<bool> read_bool(byte_reader& source) {
    auto const byte = source.read_byte();
    if ( ! byte) return std::unexpected(byte.error());
    // Only the low bit is the value (sv2-spec 3.1). The rest are reserved.
    return (*byte & uint8_t{1}) != 0;
}

expect<void> write_option_u32(byte_writer& sink, std::optional<uint32_t> const& value) {
    if ( ! value) return sink.write_byte(0);
    if (auto r = sink.write_byte(1); ! r) return r;
    return sink.write_little_endian<uint32_t>(*value);
}

expect<std::optional<uint32_t>> read_option_u32(byte_reader& source) {
    auto const count = source.read_byte();
    if ( ! count) return std::unexpected(count.error());
    if (*count == 0) return std::optional<uint32_t>{};
    if (*count != 1) return std::unexpected(error::operation_failed);
    auto const value = source.read_little_endian<uint32_t>();
    if ( ! value) return std::unexpected(value.error());
    return std::optional<uint32_t>{*value};
}

expect<void> write_b0_32(byte_writer& sink, byte_span value) {
    if (value.size() > b0_32_max) return std::unexpected(error::operation_failed);
    if (auto r = sink.write_byte(static_cast<uint8_t>(value.size())); ! r) return r;
    return sink.write_bytes(value);
}

expect<data_chunk> read_b0_32(byte_reader& source) {
    auto const size = source.read_byte();
    if ( ! size) return std::unexpected(size.error());
    if (*size > b0_32_max) return std::unexpected(error::operation_failed);
    auto const bytes = source.read_bytes(*size);
    if ( ! bytes) return std::unexpected(bytes.error());
    return data_chunk(bytes->begin(), bytes->end());
}

size_t string_size(std::string const& value) {
    return 1 + value.size();
}

size_t b0_32_size(data_chunk const& value) {
    return 1 + value.size();
}

size_t option_u32_size(std::optional<uint32_t> const& value) {
    return 1 + (value ? 4 : 0);
}

// SEQ0_64K[U32]. Reject a count the unread bytes cannot hold before allocating.
expect<void> write_u32_seq_u16(byte_writer& sink, std::vector<uint32_t> const& items) {
    if (items.size() > b0_64k_max) return std::unexpected(error::operation_failed);
    if (auto r = sink.write_little_endian<uint16_t>(static_cast<uint16_t>(items.size())); ! r) return r;
    for (auto const id : items) {
        if (auto r = sink.write_little_endian<uint32_t>(id); ! r) return r;
    }
    return {};
}

expect<std::vector<uint32_t>> read_u32_seq_u16(byte_reader& source) {
    auto const count = source.read_little_endian<uint16_t>();
    if ( ! count) return std::unexpected(count.error());
    if (*count > source.remaining_size() / sizeof(uint32_t)) {
        return std::unexpected(error::operation_failed);
    }
    std::vector<uint32_t> result;
    result.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto const id = source.read_little_endian<uint32_t>();
        if ( ! id) return std::unexpected(id.error());
        result.push_back(*id);
    }
    return result;
}

} // namespace

size_t open_standard_mining_channel::serialized_size() const {
    return 4 + string_size(user_identity) + 4 + hash_size;
}

expect<void> open_standard_mining_channel::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    if (auto r = write_string_u8(sink, user_identity); ! r) return r;
    if (auto r = write_f32(sink, nominal_hash_rate); ! r) return r;
    return sink.write_bytes(byte_span{max_target});
}

expect<open_standard_mining_channel> open_standard_mining_channel::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto user_identity = read_string_u8(source);
    if ( ! user_identity) return std::unexpected(user_identity.error());
    auto const nominal_hash_rate = read_f32(source);
    if ( ! nominal_hash_rate) return std::unexpected(nominal_hash_rate.error());
    auto const max_target = source.read_array<hash_size>();
    if ( ! max_target) return std::unexpected(max_target.error());
    return open_standard_mining_channel{*request_id, std::move(*user_identity),
        *nominal_hash_rate, *max_target};
}

size_t open_standard_mining_channel_success::serialized_size() const {
    return 4 + 4 + hash_size + b0_32_size(extranonce_prefix) + 4;
}

expect<void> open_standard_mining_channel_success::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_bytes(byte_span{target}); ! r) return r;
    if (auto r = write_b0_32(sink, extranonce_prefix); ! r) return r;
    return sink.write_little_endian<uint32_t>(group_channel_id);
}

expect<open_standard_mining_channel_success>
open_standard_mining_channel_success::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const target = source.read_array<hash_size>();
    if ( ! target) return std::unexpected(target.error());
    auto extranonce_prefix = read_b0_32(source);
    if ( ! extranonce_prefix) return std::unexpected(extranonce_prefix.error());
    auto const group_channel_id = source.read_little_endian<uint32_t>();
    if ( ! group_channel_id) return std::unexpected(group_channel_id.error());
    return open_standard_mining_channel_success{*request_id, *channel_id, *target,
        std::move(*extranonce_prefix), *group_channel_id};
}

size_t open_mining_channel_error::serialized_size() const {
    return 4 + string_size(error_code);
}

expect<void> open_mining_channel_error::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    return write_string_u8(sink, error_code);
}

expect<open_mining_channel_error> open_mining_channel_error::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto error_code = read_string_u8(source);
    if ( ! error_code) return std::unexpected(error_code.error());
    return open_mining_channel_error{*request_id, std::move(*error_code)};
}

size_t open_extended_mining_channel::serialized_size() const {
    return 4 + string_size(user_identity) + 4 + hash_size + 2;
}

expect<void> open_extended_mining_channel::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    if (auto r = write_string_u8(sink, user_identity); ! r) return r;
    if (auto r = write_f32(sink, nominal_hash_rate); ! r) return r;
    if (auto r = sink.write_bytes(byte_span{max_target}); ! r) return r;
    return sink.write_little_endian<uint16_t>(min_extranonce_size);
}

expect<open_extended_mining_channel> open_extended_mining_channel::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto user_identity = read_string_u8(source);
    if ( ! user_identity) return std::unexpected(user_identity.error());
    auto const nominal_hash_rate = read_f32(source);
    if ( ! nominal_hash_rate) return std::unexpected(nominal_hash_rate.error());
    auto const max_target = source.read_array<hash_size>();
    if ( ! max_target) return std::unexpected(max_target.error());
    auto const min_extranonce_size = source.read_little_endian<uint16_t>();
    if ( ! min_extranonce_size) return std::unexpected(min_extranonce_size.error());
    return open_extended_mining_channel{*request_id, std::move(*user_identity),
        *nominal_hash_rate, *max_target, *min_extranonce_size};
}

size_t open_extended_mining_channel_success::serialized_size() const {
    return 4 + 4 + hash_size + 2 + b0_32_size(extranonce_prefix) + 4;
}

expect<void> open_extended_mining_channel_success::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(request_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_bytes(byte_span{target}); ! r) return r;
    if (auto r = sink.write_little_endian<uint16_t>(extranonce_size); ! r) return r;
    if (auto r = write_b0_32(sink, extranonce_prefix); ! r) return r;
    return sink.write_little_endian<uint32_t>(group_channel_id);
}

expect<open_extended_mining_channel_success>
open_extended_mining_channel_success::from_data(byte_reader& source) {
    auto const request_id = source.read_little_endian<uint32_t>();
    if ( ! request_id) return std::unexpected(request_id.error());
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const target = source.read_array<hash_size>();
    if ( ! target) return std::unexpected(target.error());
    auto const extranonce_size = source.read_little_endian<uint16_t>();
    if ( ! extranonce_size) return std::unexpected(extranonce_size.error());
    auto extranonce_prefix = read_b0_32(source);
    if ( ! extranonce_prefix) return std::unexpected(extranonce_prefix.error());
    auto const group_channel_id = source.read_little_endian<uint32_t>();
    if ( ! group_channel_id) return std::unexpected(group_channel_id.error());
    return open_extended_mining_channel_success{*request_id, *channel_id, *target,
        *extranonce_size, std::move(*extranonce_prefix), *group_channel_id};
}

size_t new_mining_job::serialized_size() const {
    return 4 + 4 + option_u32_size(ntime_start) + 4 + hash_size;
}

expect<void> new_mining_job::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(job_id); ! r) return r;
    if (auto r = write_option_u32(sink, ntime_start); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(version); ! r) return r;
    return sink.write_bytes(byte_span{merkle_root});
}

expect<new_mining_job> new_mining_job::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const job_id = source.read_little_endian<uint32_t>();
    if ( ! job_id) return std::unexpected(job_id.error());
    auto ntime_start = read_option_u32(source);
    if ( ! ntime_start) return std::unexpected(ntime_start.error());
    auto const version = source.read_little_endian<uint32_t>();
    if ( ! version) return std::unexpected(version.error());
    auto const merkle_root = source.read_array<hash_size>();
    if ( ! merkle_root) return std::unexpected(merkle_root.error());
    return new_mining_job{*channel_id, *job_id, *ntime_start, *version, *merkle_root};
}

size_t update_channel::serialized_size() const {
    return 4 + 4 + hash_size;
}

expect<void> update_channel::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = write_f32(sink, nominal_hash_rate); ! r) return r;
    return sink.write_bytes(byte_span{max_target});
}

expect<update_channel> update_channel::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const nominal_hash_rate = read_f32(source);
    if ( ! nominal_hash_rate) return std::unexpected(nominal_hash_rate.error());
    auto const max_target = source.read_array<hash_size>();
    if ( ! max_target) return std::unexpected(max_target.error());
    return update_channel{*channel_id, *nominal_hash_rate, *max_target};
}

size_t update_channel_error::serialized_size() const {
    return 4 + string_size(error_code);
}

expect<void> update_channel_error::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    return write_string_u8(sink, error_code);
}

expect<update_channel_error> update_channel_error::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto error_code = read_string_u8(source);
    if ( ! error_code) return std::unexpected(error_code.error());
    return update_channel_error{*channel_id, std::move(*error_code)};
}

size_t close_channel::serialized_size() const {
    return 4 + string_size(reason_code);
}

expect<void> close_channel::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    return write_string_u8(sink, reason_code);
}

expect<close_channel> close_channel::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto reason_code = read_string_u8(source);
    if ( ! reason_code) return std::unexpected(reason_code.error());
    return close_channel{*channel_id, std::move(*reason_code)};
}

size_t set_extranonce_prefix::serialized_size() const {
    return 4 + b0_32_size(extranonce_prefix);
}

expect<void> set_extranonce_prefix::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    return write_b0_32(sink, extranonce_prefix);
}

expect<set_extranonce_prefix> set_extranonce_prefix::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto extranonce_prefix = read_b0_32(source);
    if ( ! extranonce_prefix) return std::unexpected(extranonce_prefix.error());
    return set_extranonce_prefix{*channel_id, std::move(*extranonce_prefix)};
}

expect<void> submit_shares_standard::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(sequence_number); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(job_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(nonce); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(ntime); ! r) return r;
    return sink.write_little_endian<uint32_t>(version);
}

expect<submit_shares_standard> submit_shares_standard::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const sequence_number = source.read_little_endian<uint32_t>();
    if ( ! sequence_number) return std::unexpected(sequence_number.error());
    auto const job_id = source.read_little_endian<uint32_t>();
    if ( ! job_id) return std::unexpected(job_id.error());
    auto const nonce = source.read_little_endian<uint32_t>();
    if ( ! nonce) return std::unexpected(nonce.error());
    auto const ntime = source.read_little_endian<uint32_t>();
    if ( ! ntime) return std::unexpected(ntime.error());
    auto const version = source.read_little_endian<uint32_t>();
    if ( ! version) return std::unexpected(version.error());
    return submit_shares_standard{*channel_id, *sequence_number, *job_id, *nonce, *ntime, *version};
}

size_t submit_shares_extended::serialized_size() const {
    return 6 * 4 + b0_32_size(extranonce);
}

expect<void> submit_shares_extended::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(sequence_number); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(job_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(nonce); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(ntime); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(version); ! r) return r;
    return write_b0_32(sink, extranonce);
}

expect<submit_shares_extended> submit_shares_extended::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const sequence_number = source.read_little_endian<uint32_t>();
    if ( ! sequence_number) return std::unexpected(sequence_number.error());
    auto const job_id = source.read_little_endian<uint32_t>();
    if ( ! job_id) return std::unexpected(job_id.error());
    auto const nonce = source.read_little_endian<uint32_t>();
    if ( ! nonce) return std::unexpected(nonce.error());
    auto const ntime = source.read_little_endian<uint32_t>();
    if ( ! ntime) return std::unexpected(ntime.error());
    auto const version = source.read_little_endian<uint32_t>();
    if ( ! version) return std::unexpected(version.error());
    auto extranonce = read_b0_32(source);
    if ( ! extranonce) return std::unexpected(extranonce.error());
    return submit_shares_extended{*channel_id, *sequence_number, *job_id, *nonce, *ntime,
        *version, std::move(*extranonce)};
}

expect<void> submit_shares_success::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(last_sequence_number); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(new_submits_accepted_count); ! r) return r;
    return sink.write_little_endian<uint64_t>(new_shares_sum);
}

expect<submit_shares_success> submit_shares_success::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const last_sequence_number = source.read_little_endian<uint32_t>();
    if ( ! last_sequence_number) return std::unexpected(last_sequence_number.error());
    auto const new_submits_accepted_count = source.read_little_endian<uint32_t>();
    if ( ! new_submits_accepted_count) return std::unexpected(new_submits_accepted_count.error());
    auto const new_shares_sum = source.read_little_endian<uint64_t>();
    if ( ! new_shares_sum) return std::unexpected(new_shares_sum.error());
    return submit_shares_success{*channel_id, *last_sequence_number,
        *new_submits_accepted_count, *new_shares_sum};
}

size_t submit_shares_error::serialized_size() const {
    return 4 + 4 + string_size(error_code);
}

expect<void> submit_shares_error::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(sequence_number); ! r) return r;
    return write_string_u8(sink, error_code);
}

expect<submit_shares_error> submit_shares_error::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const sequence_number = source.read_little_endian<uint32_t>();
    if ( ! sequence_number) return std::unexpected(sequence_number.error());
    auto error_code = read_string_u8(source);
    if ( ! error_code) return std::unexpected(error_code.error());
    return submit_shares_error{*channel_id, *sequence_number, std::move(*error_code)};
}

size_t new_extended_mining_job::serialized_size() const {
    return 4 + 4 + option_u32_size(ntime_start) + 4 + 1
        + (1 + merkle_path.size() * hash_size)
        + (2 + coinbase_tx_prefix.size())
        + (2 + coinbase_tx_suffix.size());
}

expect<void> new_extended_mining_job::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(job_id); ! r) return r;
    if (auto r = write_option_u32(sink, ntime_start); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(version); ! r) return r;
    if (auto r = write_bool(sink, version_rolling_allowed); ! r) return r;
    if (auto r = write_hash_seq_u8(sink, merkle_path); ! r) return r;
    if (auto r = write_bytes_u16(sink, coinbase_tx_prefix); ! r) return r;
    return write_bytes_u16(sink, coinbase_tx_suffix);
}

expect<new_extended_mining_job> new_extended_mining_job::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const job_id = source.read_little_endian<uint32_t>();
    if ( ! job_id) return std::unexpected(job_id.error());
    auto ntime_start = read_option_u32(source);
    if ( ! ntime_start) return std::unexpected(ntime_start.error());
    auto const version = source.read_little_endian<uint32_t>();
    if ( ! version) return std::unexpected(version.error());
    auto const version_rolling_allowed = read_bool(source);
    if ( ! version_rolling_allowed) return std::unexpected(version_rolling_allowed.error());
    auto merkle_path = read_hash_seq_u8(source);
    if ( ! merkle_path) return std::unexpected(merkle_path.error());
    auto const coinbase_tx_prefix = read_bytes_u16(source);
    if ( ! coinbase_tx_prefix) return std::unexpected(coinbase_tx_prefix.error());
    auto const coinbase_tx_suffix = read_bytes_u16(source);
    if ( ! coinbase_tx_suffix) return std::unexpected(coinbase_tx_suffix.error());
    return new_extended_mining_job{*channel_id, *job_id, *ntime_start, *version,
        *version_rolling_allowed, std::move(*merkle_path),
        data_chunk(coinbase_tx_prefix->begin(), coinbase_tx_prefix->end()),
        data_chunk(coinbase_tx_suffix->begin(), coinbase_tx_suffix->end())};
}

expect<void> mining_set_new_prev_hash::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(job_id); ! r) return r;
    if (auto r = sink.write_bytes(byte_span{prev_hash}); ! r) return r;
    if (auto r = sink.write_little_endian<uint32_t>(ntime_start); ! r) return r;
    return sink.write_little_endian<uint32_t>(nbits);
}

expect<mining_set_new_prev_hash> mining_set_new_prev_hash::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const job_id = source.read_little_endian<uint32_t>();
    if ( ! job_id) return std::unexpected(job_id.error());
    auto const prev_hash = source.read_array<hash_size>();
    if ( ! prev_hash) return std::unexpected(prev_hash.error());
    auto const ntime_start = source.read_little_endian<uint32_t>();
    if ( ! ntime_start) return std::unexpected(ntime_start.error());
    auto const nbits = source.read_little_endian<uint32_t>();
    if ( ! nbits) return std::unexpected(nbits.error());
    return mining_set_new_prev_hash{*channel_id, *job_id, *prev_hash, *ntime_start, *nbits};
}

expect<void> set_target::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(channel_id); ! r) return r;
    return sink.write_bytes(byte_span{target});
}

expect<set_target> set_target::from_data(byte_reader& source) {
    auto const channel_id = source.read_little_endian<uint32_t>();
    if ( ! channel_id) return std::unexpected(channel_id.error());
    auto const target = source.read_array<hash_size>();
    if ( ! target) return std::unexpected(target.error());
    return set_target{*channel_id, *target};
}

size_t set_group_channel::serialized_size() const {
    return 4 + 2 + 4 * channel_ids.size();
}

expect<void> set_group_channel::to_data(byte_writer& sink) const {
    if (auto r = sink.write_little_endian<uint32_t>(group_channel_id); ! r) return r;
    return write_u32_seq_u16(sink, channel_ids);
}

expect<set_group_channel> set_group_channel::from_data(byte_reader& source) {
    auto const group_channel_id = source.read_little_endian<uint32_t>();
    if ( ! group_channel_id) return std::unexpected(group_channel_id.error());
    auto channel_ids = read_u32_seq_u16(source);
    if ( ! channel_ids) return std::unexpected(channel_ids.error());
    return set_group_channel{*group_channel_id, std::move(*channel_ids)};
}

} // namespace kth::node::sv2
