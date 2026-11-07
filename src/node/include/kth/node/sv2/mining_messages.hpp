// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_MINING_MESSAGES_HPP
#define KTH_NODE_SV2_MINING_MESSAGES_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

// Stratum V2 Mining Protocol messages (sv2-spec 05, message types in 08).
// channel_message is the channel_msg bit from the message-type table. The
// common SetupConnection messages stay in connection.hpp.

namespace kth::node::sv2 {

inline constexpr uint32_t mining_flag_requires_standard_jobs = 1u << 0;
inline constexpr uint32_t mining_flag_requires_work_selection = 1u << 1;
inline constexpr uint32_t mining_flag_requires_version_rolling = 1u << 2;
inline constexpr uint32_t mining_supported_client_flags =
    mining_flag_requires_standard_jobs | mining_flag_requires_version_rolling;

// Success flags. Neither is set: version rolling is allowed, and standard
// channels are accepted (sv2-spec 5.3.1).
inline constexpr uint32_t mining_flag_requires_fixed_version = 1u << 0;
inline constexpr uint32_t mining_flag_requires_extended_channels = 1u << 1;

struct open_standard_mining_channel {
    static constexpr uint8_t message_type = 0x10;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    std::string user_identity;
    float nominal_hash_rate = 0;
    hash_digest max_target{};

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<open_standard_mining_channel> from_data(byte_reader& source);
};

struct open_standard_mining_channel_success {
    static constexpr uint8_t message_type = 0x11;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    uint32_t channel_id = 0;
    hash_digest target{};
    data_chunk extranonce_prefix;
    uint32_t group_channel_id = 0;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<open_standard_mining_channel_success> from_data(byte_reader& source);
};

struct open_mining_channel_error {
    static constexpr uint8_t message_type = 0x12;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    std::string error_code;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<open_mining_channel_error> from_data(byte_reader& source);
};

struct open_extended_mining_channel {
    static constexpr uint8_t message_type = 0x13;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    std::string user_identity;
    float nominal_hash_rate = 0;
    hash_digest max_target{};
    uint16_t min_extranonce_size = 0;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<open_extended_mining_channel> from_data(byte_reader& source);
};

struct open_extended_mining_channel_success {
    static constexpr uint8_t message_type = 0x14;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    uint32_t channel_id = 0;
    hash_digest target{};
    uint16_t extranonce_size = 0;
    data_chunk extranonce_prefix;
    uint32_t group_channel_id = 0;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<open_extended_mining_channel_success> from_data(byte_reader& source);
};

struct new_mining_job {
    static constexpr uint8_t message_type = 0x15;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t job_id = 0;
    std::optional<uint32_t> ntime_start;
    uint32_t version = 0;
    hash_digest merkle_root{};

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<new_mining_job> from_data(byte_reader& source);
};

struct update_channel {
    static constexpr uint8_t message_type = 0x16;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    float nominal_hash_rate = 0;
    hash_digest max_target{};

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<update_channel> from_data(byte_reader& source);
};

struct update_channel_error {
    static constexpr uint8_t message_type = 0x17;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    std::string error_code;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<update_channel_error> from_data(byte_reader& source);
};

struct close_channel {
    static constexpr uint8_t message_type = 0x18;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    std::string reason_code;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<close_channel> from_data(byte_reader& source);
};

struct set_extranonce_prefix {
    static constexpr uint8_t message_type = 0x19;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    data_chunk extranonce_prefix;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<set_extranonce_prefix> from_data(byte_reader& source);
};

struct submit_shares_standard {
    static constexpr uint8_t message_type = 0x1a;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t sequence_number = 0;
    uint32_t job_id = 0;
    uint32_t nonce = 0;
    uint32_t ntime = 0;
    uint32_t version = 0;

    [[nodiscard]] size_t serialized_size() const { return 6 * 4; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<submit_shares_standard> from_data(byte_reader& source);
};

struct submit_shares_extended {
    static constexpr uint8_t message_type = 0x1b;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t sequence_number = 0;
    uint32_t job_id = 0;
    uint32_t nonce = 0;
    uint32_t ntime = 0;
    uint32_t version = 0;
    data_chunk extranonce;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<submit_shares_extended> from_data(byte_reader& source);
};

struct submit_shares_success {
    static constexpr uint8_t message_type = 0x1c;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t last_sequence_number = 0;
    uint32_t new_submits_accepted_count = 0;
    uint64_t new_shares_sum = 0;

    [[nodiscard]] size_t serialized_size() const { return 4 + 4 + 4 + 8; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<submit_shares_success> from_data(byte_reader& source);
};

struct submit_shares_error {
    static constexpr uint8_t message_type = 0x1d;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t sequence_number = 0;
    std::string error_code;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<submit_shares_error> from_data(byte_reader& source);
};

struct new_extended_mining_job {
    static constexpr uint8_t message_type = 0x1f;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t job_id = 0;
    std::optional<uint32_t> ntime_start;
    uint32_t version = 0;
    bool version_rolling_allowed = false;
    std::vector<hash_digest> merkle_path;
    data_chunk coinbase_tx_prefix;
    data_chunk coinbase_tx_suffix;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<new_extended_mining_job> from_data(byte_reader& source);
};

// Mining Protocol SetNewPrevHash (0x20). Not the Template Distribution message
// of the same name (0x72): this one names a channel and a future job.
struct mining_set_new_prev_hash {
    static constexpr uint8_t message_type = 0x20;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t job_id = 0;
    hash_digest prev_hash{};
    uint32_t ntime_start = 0;
    uint32_t nbits = 0;

    [[nodiscard]] size_t serialized_size() const { return 4 + 4 + hash_size + 4 + 4; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<mining_set_new_prev_hash> from_data(byte_reader& source);
};

struct set_target {
    static constexpr uint8_t message_type = 0x21;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    hash_digest target{};

    [[nodiscard]] size_t serialized_size() const { return 4 + hash_size; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<set_target> from_data(byte_reader& source);
};

} // namespace kth::node::sv2

#endif
