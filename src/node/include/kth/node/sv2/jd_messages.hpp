// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_MESSAGES_HPP
#define KTH_NODE_SV2_JD_MESSAGES_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/sv2/framing.hpp>

namespace kth::node::sv2 {

// Frames this declaration state machine accepts. Tokens, errors, and a
// missing-transaction index list (at most 4 + 2 + 2*65535 bytes) fit well
// below 1 MiB. jd_session::max_payload_size and jd_connection::max_payload_size
// are the same cap on frames the connection reads.
inline constexpr size_t jd_message_max_payload = 1024 * 1024;

// Outbound payload cap. A frame's msg_length is a U24 (sv2-spec 3.2), so one
// message carries at most 2^24 - 1 bytes. u24_max is that width.
inline constexpr size_t jd_frame_max_payload = u24_max;

struct allocate_mining_job_token {
    static constexpr uint8_t message_type = 0x50;
    static constexpr bool channel_message = false;

    std::string user_identifier;
    uint32_t request_id = 0;

    [[nodiscard]] size_t serialized_size() const { return 1 + user_identifier.size() + 4; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<allocate_mining_job_token> from_data(byte_reader& source);
};

struct allocate_mining_job_token_success {
    static constexpr uint8_t message_type = 0x51;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    data_chunk mining_job_token;
    data_chunk coinbase_tx_outputs;

    [[nodiscard]] size_t serialized_size() const { return 4 + 1 + mining_job_token.size() + 2 + coinbase_tx_outputs.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<allocate_mining_job_token_success> from_data(byte_reader& source);
};

struct declare_mining_job {
    static constexpr uint8_t message_type = 0x57;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    data_chunk mining_job_token;
    uint32_t version = 0;
    data_chunk coinbase_tx_prefix;
    data_chunk coinbase_tx_suffix;
    // BCH has no witness: these are transaction ids in the template's CTOR order.
    std::vector<hash_digest> wtxid_list;
    data_chunk excess_data;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<declare_mining_job> from_data(byte_reader& source);
};

struct declare_mining_job_success {
    static constexpr uint8_t message_type = 0x58;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    data_chunk new_mining_job_token;

    [[nodiscard]] size_t serialized_size() const { return 4 + 1 + new_mining_job_token.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<declare_mining_job_success> from_data(byte_reader& source);
};

struct declare_mining_job_error {
    static constexpr uint8_t message_type = 0x59;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    std::string error_code;
    data_chunk error_details;

    [[nodiscard]] size_t serialized_size() const { return 4 + 1 + error_code.size() + 2 + error_details.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<declare_mining_job_error> from_data(byte_reader& source);
};

struct provide_missing_transactions {
    static constexpr uint8_t message_type = 0x55;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    std::vector<uint16_t> unknown_tx_position_list;

    [[nodiscard]] size_t serialized_size() const { return 4 + 2 + 2 * unknown_tx_position_list.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<provide_missing_transactions> from_data(byte_reader& source);
};

struct provide_missing_transactions_success {
    static constexpr uint8_t message_type = 0x56;
    static constexpr bool channel_message = false;

    uint32_t request_id = 0;
    std::vector<data_chunk> transaction_list;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<provide_missing_transactions_success> from_data(byte_reader& source);
};

struct push_solution {
    static constexpr uint8_t message_type = 0x60;
    static constexpr bool channel_message = false;

    data_chunk extranonce;
    hash_digest prev_hash{};
    uint32_t nonce = 0;
    uint32_t ntime = 0;
    uint32_t nbits = 0;
    uint32_t version = 0;

    [[nodiscard]] size_t serialized_size() const { return 1 + extranonce.size() + hash_size + 4 * 4; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<push_solution> from_data(byte_reader& source);
};

struct set_custom_mining_job {
    static constexpr uint8_t message_type = 0x22;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t request_id = 0;
    data_chunk mining_job_token;
    uint32_t version = 0;
    hash_digest prev_hash{};
    uint32_t ntime_start = 0;
    uint32_t nbits = 0;
    uint32_t coinbase_tx_version = 0;
    data_chunk coinbase_prefix;
    uint32_t coinbase_tx_input_sequence = 0;
    data_chunk coinbase_tx_outputs;
    uint32_t coinbase_tx_locktime = 0;
    std::vector<hash_digest> merkle_path;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<set_custom_mining_job> from_data(byte_reader& source);
};

struct set_custom_mining_job_success {
    static constexpr uint8_t message_type = 0x23;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t request_id = 0;
    uint32_t job_id = 0;

    [[nodiscard]] size_t serialized_size() const { return 4 * 3; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<set_custom_mining_job_success> from_data(byte_reader& source);
};

struct set_custom_mining_job_error {
    static constexpr uint8_t message_type = 0x24;
    static constexpr bool channel_message = true;

    uint32_t channel_id = 0;
    uint32_t request_id = 0;
    std::string error_code;

    [[nodiscard]] size_t serialized_size() const { return 4 + 4 + 1 + error_code.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<set_custom_mining_job_error> from_data(byte_reader& source);
};

// Extensions Negotiation, extension 0x0001. Sent on the Job Declaration
// connection immediately after SetupConnection.Success.
inline constexpr uint16_t extensions_negotiation_type = 0x0001;

// Requested Payout Outputs uses the experimental identifier 0x5052, which is
// not in the SV2 extension registry. sv2-spec PR #249 proposed it as 0x0004
// and was closed.
inline constexpr uint16_t requested_payout_outputs_extension = 0x5052;

// Bit 0 of RequestPayoutOutputs.Success.flags. Every other bit must be clear.
inline constexpr uint32_t exact_outputs_flag = 1u;

inline constexpr std::string_view error_stale_payout_outputs = "stale-payout-outputs";

struct request_extensions {
    static constexpr uint8_t message_type = 0x00;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = extensions_negotiation_type;

    uint16_t request_id = 0;
    std::vector<uint16_t> requested_extensions;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_extensions> from_data(byte_reader& source);
};

struct request_extensions_success {
    static constexpr uint8_t message_type = 0x01;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = extensions_negotiation_type;

    uint16_t request_id = 0;
    std::vector<uint16_t> supported_extensions;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_extensions_success> from_data(byte_reader& source);
};

struct request_extensions_error {
    static constexpr uint8_t message_type = 0x02;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = extensions_negotiation_type;

    uint16_t request_id = 0;
    std::vector<uint16_t> unsupported_extensions;
    std::vector<uint16_t> required_extensions;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_extensions_error> from_data(byte_reader& source);
};

// Requested Payout Outputs. Frames use requested_payout_outputs_extension.
struct request_payout_outputs {
    static constexpr uint8_t message_type = 0x00;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = requested_payout_outputs_extension;

    uint32_t request_id = 0;
    data_chunk mining_job_token;
    uint64_t available_payout_value = 0;
    uint32_t height = 0;
    hash_digest prev_hash{};
    uint32_t nbits = 0;
    data_chunk coinbase_prefix;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_payout_outputs> from_data(byte_reader& source);
};

struct request_payout_outputs_success {
    static constexpr uint8_t message_type = 0x01;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = requested_payout_outputs_extension;

    uint32_t request_id = 0;
    uint32_t flags = 0;
    uint32_t coinbase_tx_version = 0;
    uint32_t coinbase_tx_input_sequence = 0;
    uint32_t coinbase_tx_locktime = 0;
    data_chunk coinbase_tx_outputs;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_payout_outputs_success> from_data(byte_reader& source);
};

struct request_payout_outputs_error {
    static constexpr uint8_t message_type = 0x02;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = requested_payout_outputs_extension;

    uint32_t request_id = 0;
    std::string error_code;

    [[nodiscard]] size_t serialized_size() const { return 4 + 1 + error_code.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_payout_outputs_error> from_data(byte_reader& source);
};

struct payout_outputs_stale {
    static constexpr uint8_t message_type = 0x03;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = requested_payout_outputs_extension;

    [[nodiscard]] size_t serialized_size() const { return 0; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<payout_outputs_stale> from_data(byte_reader& source);
};

// BCH large templates, experimental identifier 0x4C54 ("LT"), not yet in the
// SV2 extension registry. Standard Job Declaration bounds the declared list at
// SEQ0_64K and names missing transactions by U16 position, sizes that fit
// Bitcoin blocks. Bitcoin Cash's adaptive block size limit (ABLA) admits more
// transactions than that, so this extension pages the list, names positions
// with U32, and lets a reply that would exceed one frame carry a prefix.
inline constexpr uint16_t large_templates_extension = 0x4C54;

// DeclareMiningJob.Transactions. One page of the declared transaction ids,
// sent before the DeclareMiningJob with the same request_id. The declared
// list is every page in order followed by DeclareMiningJob's own list.
struct declare_mining_job_transactions {
    static constexpr uint8_t message_type = 0x01;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = large_templates_extension;

    uint32_t request_id = 0;
    // BCH has no witness: transaction ids in the template's CTOR order.
    std::vector<hash_digest> txid_list;

    [[nodiscard]] size_t serialized_size() const { return 4 + 2 + hash_size * txid_list.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<declare_mining_job_transactions> from_data(byte_reader& source);
};

// ProvideMissingTransactions.Positions. The core request with U32 positions
// into the whole declared list, 0-based, without the coinbase. It is answered
// with the core ProvideMissingTransactions.Success.
struct provide_missing_transactions_positions {
    static constexpr uint8_t message_type = 0x02;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = large_templates_extension;

    uint32_t request_id = 0;
    std::vector<uint32_t> unknown_tx_position_list;

    [[nodiscard]] size_t serialized_size() const { return 4 + 2 + 4 * unknown_tx_position_list.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<provide_missing_transactions_positions> from_data(byte_reader& source);
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_MESSAGES_HPP
