// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <string>

#include <kth/infrastructure.hpp>
#include <kth/node/sv2/messages.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

// A 32-byte value with distinct, position-dependent bytes.
hash_digest seq_hash(uint8_t start) {
    hash_digest h{};
    for (size_t i = 0; i < h.size(); ++i) {
        h[i] = static_cast<uint8_t>(start + i);
    }
    return h;
}

} // namespace

// Start Test Suite: sv2 messages tests

TEST_CASE("sv2 CoinbaseOutputConstraints round-trips and matches the wire layout", "[sv2 messages]") {
    coinbase_output_constraints const msg{/*size*/ 0x01020304u, /*sigops*/ 0x0506u};

    auto const bytes = to_data_chunk(msg);
    REQUIRE(bytes == data_chunk{0x04, 0x03, 0x02, 0x01, 0x06, 0x05});  // U32 LE, U16 LE

    auto const parsed = from_data_chunk<coinbase_output_constraints>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->coinbase_output_max_additional_size == msg.coinbase_output_max_additional_size);
    REQUIRE(parsed->coinbase_output_max_additional_sigops == msg.coinbase_output_max_additional_sigops);
    REQUIRE(coinbase_output_constraints::message_type == 0x70);
}

TEST_CASE("sv2 SetNewPrevHash round-trips", "[sv2 messages]") {
    set_new_prev_hash const msg{
        /*template_id*/ 0x1122334455667788ull,
        /*prev_hash*/ seq_hash(0x01),
        /*header_timestamp*/ 0xAABBCCDDu,
        /*n_bits*/ 0x1d00ffffu,
        /*target*/ seq_hash(0x80)};

    auto const bytes = to_data_chunk(msg);
    REQUIRE(bytes.size() == msg.serialized_size());
    REQUIRE(bytes.size() == 80u);

    auto const parsed = from_data_chunk<set_new_prev_hash>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->template_id == msg.template_id);
    REQUIRE(parsed->prev_hash == msg.prev_hash);
    REQUIRE(parsed->header_timestamp == msg.header_timestamp);
    REQUIRE(parsed->n_bits == msg.n_bits);
    REQUIRE(parsed->target == msg.target);
    REQUIRE(set_new_prev_hash::message_type == 0x72);
}

TEST_CASE("sv2 RequestTransactionData round-trips", "[sv2 messages]") {
    request_transaction_data const msg{0xdeadbeefcafef00dull};

    auto const bytes = to_data_chunk(msg);
    REQUIRE(bytes.size() == 8u);

    auto const parsed = from_data_chunk<request_transaction_data>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->template_id == msg.template_id);
    REQUIRE(request_transaction_data::message_type == 0x73);
}

TEST_CASE("sv2 RequestTransactionData.Error round-trips its id and reason", "[sv2 messages]") {
    request_transaction_data_error const msg{42u, "template not found"};

    auto const bytes = to_data_chunk(msg);
    auto const parsed = from_data_chunk<request_transaction_data_error>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->template_id == msg.template_id);
    REQUIRE(parsed->error_code == msg.error_code);
    REQUIRE(request_transaction_data_error::message_type == 0x75);
}

TEST_CASE("sv2 SubmitSolution round-trips its header fields and coinbase", "[sv2 messages]") {
    submit_solution const msg{
        /*template_id*/ 7u,
        /*version*/ 0x20000000u,
        /*header_timestamp*/ 1234u,
        /*header_nonce*/ 0x89abcdefu,
        /*coinbase_tx*/ data_chunk{0x01, 0x02, 0x03, 0x04, 0x05}};

    auto const bytes = to_data_chunk(msg);
    auto const parsed = from_data_chunk<submit_solution>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->template_id == msg.template_id);
    REQUIRE(parsed->version == msg.version);
    REQUIRE(parsed->header_timestamp == msg.header_timestamp);
    REQUIRE(parsed->header_nonce == msg.header_nonce);
    REQUIRE(parsed->coinbase_tx == msg.coinbase_tx);
    REQUIRE(submit_solution::message_type == 0x76);
}

TEST_CASE("sv2 message from_data rejects a truncated buffer", "[sv2 messages]") {
    data_chunk const short_buffer{0x04, 0x03, 0x02};  // needs 6 for the constraints
    auto const parsed = from_data_chunk<coinbase_output_constraints>(short_buffer);
    REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("sv2 NewTemplate round-trips all its fields", "[sv2 messages]") {
    new_template const msg{
        /*template_id*/ 0x0102030405060708ull,
        /*future_template*/ true,
        /*version*/ 0x20000000u,
        /*coinbase_tx_version*/ 2u,
        /*coinbase_prefix*/ data_chunk{0x03, 0x51, 0x52, 0x53},
        /*coinbase_tx_input_sequence*/ 0xffffffffu,
        /*coinbase_tx_value_remaining*/ 5000000000ull,
        /*coinbase_tx_outputs_count*/ 1u,
        /*coinbase_tx_outputs*/ data_chunk{0xAA, 0xBB, 0xCC, 0xDD},
        /*coinbase_tx_locktime*/ 0u,
        /*merkle_path*/ std::vector<hash_digest>{seq_hash(0x10), seq_hash(0x20)}};

    auto const bytes = to_data_chunk(msg);
    REQUIRE(bytes.size() == msg.serialized_size());

    auto const parsed = from_data_chunk<new_template>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->template_id == msg.template_id);
    REQUIRE(parsed->future_template == msg.future_template);
    REQUIRE(parsed->version == msg.version);
    REQUIRE(parsed->coinbase_tx_version == msg.coinbase_tx_version);
    REQUIRE(parsed->coinbase_prefix == msg.coinbase_prefix);
    REQUIRE(parsed->coinbase_tx_input_sequence == msg.coinbase_tx_input_sequence);
    REQUIRE(parsed->coinbase_tx_value_remaining == msg.coinbase_tx_value_remaining);
    REQUIRE(parsed->coinbase_tx_outputs_count == msg.coinbase_tx_outputs_count);
    REQUIRE(parsed->coinbase_tx_outputs == msg.coinbase_tx_outputs);
    REQUIRE(parsed->coinbase_tx_locktime == msg.coinbase_tx_locktime);
    REQUIRE(parsed->merkle_path == msg.merkle_path);
    REQUIRE(new_template::message_type == 0x71);
}

TEST_CASE("sv2 RequestTransactionData.Success round-trips its excess data and tx list", "[sv2 messages]") {
    request_transaction_data_success const msg{
        /*template_id*/ 99u,
        /*excess_data*/ data_chunk{0x01, 0x02, 0x03},
        /*transaction_list*/ std::vector<data_chunk>{{0xDE, 0xAD}, {}, {0xBE, 0xEF, 0x00, 0x11}}};

    auto const bytes = to_data_chunk(msg);
    REQUIRE(bytes.size() == msg.serialized_size());

    auto const parsed = from_data_chunk<request_transaction_data_success>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->template_id == msg.template_id);
    REQUIRE(parsed->excess_data == msg.excess_data);
    REQUIRE(parsed->transaction_list == msg.transaction_list);
    REQUIRE(request_transaction_data_success::message_type == 0x74);
}
