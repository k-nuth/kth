// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_MESSAGES_HPP
#define KTH_NODE_SV2_MESSAGES_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

// Stratum V2 Template Distribution Protocol messages (sv2-spec 07). For all of
// them extension_type is 0 and the channel_msg flag is unset; message_type is
// the value carried in the frame header. Each follows the from_data / to_data
// convention so it flows through from_data_chunk / to_data_chunk. This header
// covers the messages with no sequence fields; NewTemplate and
// RequestTransactionData.Success (which carry SEQ fields) are added separately.

namespace kth::node::sv2 {

// CoinbaseOutputConstraints (0x70, Client -> Server): the downstream declares
// how much extra room its coinbase output and sigops need.
struct coinbase_output_constraints {
    static constexpr uint8_t message_type = 0x70;

    uint32_t coinbase_output_max_additional_size = 0;
    uint16_t coinbase_output_max_additional_sigops = 0;

    [[nodiscard]] size_t serialized_size() const { return 4 + 2; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<coinbase_output_constraints> from_data(byte_reader& source);
};

// SetNewPrevHash (0x72, Server -> Client): the tip moved; work must build on
// prev_hash with the given target.
struct set_new_prev_hash {
    static constexpr uint8_t message_type = 0x72;

    uint64_t template_id = 0;
    hash_digest prev_hash{};
    uint32_t header_timestamp = 0;
    uint32_t n_bits = 0;
    hash_digest target{};

    [[nodiscard]] size_t serialized_size() const { return 8 + hash_size + 4 + 4 + hash_size; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<set_new_prev_hash> from_data(byte_reader& source);
};

// RequestTransactionData (0x73, Client -> Server): ask for a template's full
// transaction list.
struct request_transaction_data {
    static constexpr uint8_t message_type = 0x73;

    uint64_t template_id = 0;

    [[nodiscard]] size_t serialized_size() const { return 8; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_transaction_data> from_data(byte_reader& source);
};

// RequestTransactionData.Error (0x75, Server -> Client).
struct request_transaction_data_error {
    static constexpr uint8_t message_type = 0x75;

    uint64_t template_id = 0;
    std::string error_code;   // STR0_255

    [[nodiscard]] size_t serialized_size() const { return 8 + 1 + error_code.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_transaction_data_error> from_data(byte_reader& source);
};

// SubmitSolution (0x76, Client -> Server): the miner submits the solved header
// fields and the full coinbase transaction.
struct submit_solution {
    static constexpr uint8_t message_type = 0x76;

    uint64_t template_id = 0;
    uint32_t version = 0;
    uint32_t header_timestamp = 0;
    uint32_t header_nonce = 0;
    data_chunk coinbase_tx;   // B0_64K

    [[nodiscard]] size_t serialized_size() const { return 8 + 4 + 4 + 4 + 2 + coinbase_tx.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<submit_solution> from_data(byte_reader& source);
};

// NewTemplate (0x71, Server -> Client): a new block template. The client builds
// the coinbase around coinbase_prefix / coinbase_tx_outputs and pairs it with
// merkle_path to compute the block's merkle root.
struct new_template {
    static constexpr uint8_t message_type = 0x71;

    uint64_t template_id = 0;
    bool future_template = false;              // BOOL
    uint32_t version = 0;
    uint32_t coinbase_tx_version = 0;
    data_chunk coinbase_prefix;                // B0_255
    uint32_t coinbase_tx_input_sequence = 0;
    uint64_t coinbase_tx_value_remaining = 0;
    uint32_t coinbase_tx_outputs_count = 0;
    data_chunk coinbase_tx_outputs;            // B0_64K
    uint32_t coinbase_tx_locktime = 0;
    std::vector<hash_digest> merkle_path;      // SEQ0_255[U256]

    [[nodiscard]] size_t serialized_size() const {
        return 8 + 1 + 4 + 4
            + (1 + coinbase_prefix.size())
            + 4 + 8 + 4
            + (2 + coinbase_tx_outputs.size())
            + 4
            + (1 + merkle_path.size() * hash_size);
    }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<new_template> from_data(byte_reader& source);
};

// RequestTransactionData.Success (0x74, Server -> Client): the full transaction
// list behind a template, plus any excess (segwit-style) data.
struct request_transaction_data_success {
    static constexpr uint8_t message_type = 0x74;

    uint64_t template_id = 0;
    data_chunk excess_data;                    // B0_64K
    std::vector<data_chunk> transaction_list;  // SEQ0_64K[B0_16M]

    [[nodiscard]] size_t serialized_size() const {
        size_t size = 8 + (2 + excess_data.size()) + 2;
        for (auto const& tx : transaction_list) {
            size += 3 + tx.size();
        }
        return size;
    }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_transaction_data_success> from_data(byte_reader& source);
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_MESSAGES_HPP
