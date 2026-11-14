// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <kth/node/sv2/jd_messages.hpp>
#include <kth/node/sv2/primitives.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

hash_digest marked_hash(uint8_t first) {
    hash_digest hash{};
    for (size_t i = 0; i < hash.size(); ++i) hash[i] = static_cast<uint8_t>(first + i);
    return hash;
}

template <typename Message>
Message require_codec(Message const& message) {
    auto const bytes = to_data_chunk(message);
    REQUIRE(bytes.size() == message.serialized_size());
    auto const parsed = from_data_chunk<Message>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(to_data_chunk(*parsed) == bytes);
    // Every field must arrive in full, and a future core field is not silently
    // accepted. Transport tests separately exercise these bytes across reads.
    for (size_t size = 0; size < bytes.size(); ++size) {
        INFO("truncated size " << size);
        REQUIRE_FALSE(from_data_chunk<Message>(byte_span(bytes).first(size)).has_value());
        data_chunk output(size);
        byte_writer writer(output);
        REQUIRE_FALSE(message.to_data(writer).has_value());
    }
    auto trailing = bytes;
    trailing.push_back(0);
    REQUIRE_FALSE(from_data_chunk<Message>(trailing).has_value());
    return *parsed;
}

template <typename Message>
void require_refused(Message const& message) {
    data_chunk bytes(message.serialized_size());
    byte_writer writer(bytes);
    REQUIRE_FALSE(message.to_data(writer).has_value());
}

} // namespace

TEST_CASE("sv2 jd allocation keeps identity before request id and outputs opaque", "[sv2 jd]") {
    allocate_mining_job_token const request{"u", 0x04030201u};
    REQUIRE(to_data_chunk(request) == data_chunk{1, 'u', 1, 2, 3, 4});
    auto const parsed = require_codec(request);
    REQUIRE(parsed.user_identifier == "u");
    REQUIRE(parsed.request_id == 0x04030201u);
    REQUIRE(allocate_mining_job_token::message_type == 0x50);
    REQUIRE_FALSE(allocate_mining_job_token::channel_message);

    allocate_mining_job_token_success const response{7, {0, 1, 0}, {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0x51}};
    auto const allocation = require_codec(response);
    REQUIRE(allocation.request_id == 7);
    REQUIRE(allocation.mining_job_token == response.mining_job_token);
    REQUIRE(allocation.coinbase_tx_outputs == response.coinbase_tx_outputs);
    REQUIRE(allocate_mining_job_token_success::message_type == 0x51);

    REQUIRE(require_codec(allocate_mining_job_token{}).user_identifier.empty());
    REQUIRE(require_codec(allocate_mining_job_token_success{}).mining_job_token.empty());
    REQUIRE(from_data_chunk<allocate_mining_job_token>(to_data_chunk(
        allocate_mining_job_token{std::string(b0_255_max, 'u'), 1})).has_value());
    REQUIRE(from_data_chunk<allocate_mining_job_token_success>(to_data_chunk(
        allocate_mining_job_token_success{1, data_chunk(b0_255_max, 1), data_chunk(b0_64k_max, 2)})).has_value());
    require_refused(allocate_mining_job_token{std::string(b0_255_max + 1, 'u'), 1});
    require_refused(allocate_mining_job_token_success{1, data_chunk(b0_255_max + 1), {}});
    require_refused(allocate_mining_job_token_success{1, {}, data_chunk(b0_64k_max + 1)});
}

TEST_CASE("sv2 jd declaration preserves coinbase pieces and ordered BCH transaction ids", "[sv2 jd]") {
    declare_mining_job const declaration{42, {4, 0, 5}, 0x20000000u,
        {2, 0, 0, 0, 1, 0}, {0xff, 1, 0, 0}, {marked_hash(1), marked_hash(64)}, {0, 7}};
    auto const parsed = require_codec(declaration);
    REQUIRE(parsed.request_id == 42);
    REQUIRE(parsed.mining_job_token == declaration.mining_job_token);
    REQUIRE(parsed.version == declaration.version);
    REQUIRE(parsed.coinbase_tx_prefix == declaration.coinbase_tx_prefix);
    REQUIRE(parsed.coinbase_tx_suffix == declaration.coinbase_tx_suffix);
    REQUIRE(parsed.wtxid_list == declaration.wtxid_list);
    REQUIRE(parsed.excess_data == declaration.excess_data);
    REQUIRE(declare_mining_job::message_type == 0x57);
    REQUIRE_FALSE(declare_mining_job::channel_message);

    auto const bytes = to_data_chunk(declaration);
    auto const hash_count_offset = 4 + 1 + declaration.mining_job_token.size() + 4
        + 2 + declaration.coinbase_tx_prefix.size() + 2 + declaration.coinbase_tx_suffix.size();
    REQUIRE(bytes[hash_count_offset] == 2);
    REQUIRE(bytes[hash_count_offset + 1] == 0);
    REQUIRE(byte_span(bytes).subspan(hash_count_offset + 2, hash_size).front() == 1);
    REQUIRE(byte_span(bytes).subspan(hash_count_offset + 2 + hash_size, hash_size).front() == 64);

    auto const success = require_codec(declare_mining_job_success{42, {9, 0, 8}});
    REQUIRE(success.request_id == 42);
    REQUIRE(success.new_mining_job_token == data_chunk{9, 0, 8});
    REQUIRE(declare_mining_job_success::message_type == 0x58);
    auto const error = require_codec(declare_mining_job_error{42, "rejected", {0, 3, 0}});
    REQUIRE(error.request_id == 42);
    REQUIRE(error.error_code == "rejected");
    REQUIRE(error.error_details == data_chunk{0, 3, 0});
    REQUIRE(declare_mining_job_error::message_type == 0x59);
    REQUIRE(require_codec(declare_mining_job{}).wtxid_list.empty());
    REQUIRE(require_codec(declare_mining_job_success{}).new_mining_job_token.empty());
    REQUIRE(require_codec(declare_mining_job_error{}).error_details.empty());
}

TEST_CASE("sv2 jd missing transaction codecs retain positions and exact raw bytes", "[sv2 jd]") {
    provide_missing_transactions const request{0x04030201u, {2, 0, 65535}};
    REQUIRE(to_data_chunk(request) == data_chunk{1, 2, 3, 4, 3, 0, 2, 0, 0, 0, 255, 255});
    auto const parsed = require_codec(request);
    REQUIRE(parsed.request_id == request.request_id);
    REQUIRE(parsed.unknown_tx_position_list == request.unknown_tx_position_list);
    REQUIRE(provide_missing_transactions::message_type == 0x55);
    REQUIRE_FALSE(provide_missing_transactions::channel_message);

    provide_missing_transactions_success const response{7, {{0, 1, 0, 0xff}, {0, 0}, {0x51}}};
    REQUIRE(to_data_chunk(response) == data_chunk{7, 0, 0, 0, 3, 0,
        4, 0, 0, 0, 1, 0, 255, 2, 0, 0, 0, 0, 1, 0, 0, 0x51});
    auto const transactions = require_codec(response);
    REQUIRE(transactions.request_id == 7);
    REQUIRE(transactions.transaction_list == response.transaction_list);
    REQUIRE(provide_missing_transactions_success::message_type == 0x56);
    REQUIRE(require_codec(provide_missing_transactions{}).unknown_tx_position_list.empty());
    REQUIRE(require_codec(provide_missing_transactions_success{}).transaction_list.empty());
    REQUIRE(require_codec(provide_missing_transactions_success{7, {{}}}).transaction_list.front().empty());

    REQUIRE(from_data_chunk<provide_missing_transactions>(to_data_chunk(
        provide_missing_transactions{1, std::vector<uint16_t>(b0_64k_max, 65535)})).has_value());
    REQUIRE(from_data_chunk<provide_missing_transactions_success>(to_data_chunk(
        provide_missing_transactions_success{1, std::vector<data_chunk>(b0_64k_max)})).has_value());
    require_refused(provide_missing_transactions{1, std::vector<uint16_t>(b0_64k_max + 1)});
    require_refused(provide_missing_transactions_success{1, std::vector<data_chunk>(b0_64k_max + 1)});
    // The announced transaction length cannot allocate past the frame's bytes.
    REQUIRE_FALSE(from_data_chunk<provide_missing_transactions_success>(
        data_chunk{1, 0, 0, 0, 1, 0, 255, 255, 255}).has_value());
    REQUIRE_FALSE(from_data_chunk<provide_missing_transactions_success>(
        data_chunk{1, 0, 0, 0, 255, 255}).has_value());
}

TEST_CASE("sv2 jd custom job codecs retain consensus and coinbase fields", "[sv2 jd]") {
    set_custom_mining_job const request{3, 7, {4, 0, 9}, 0x20000000u,
        marked_hash(12), 123456, 0x207fffffu, 2, {1, 42}, 0xffffffffu,
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0x51}, 0, {marked_hash(80), marked_hash(120)}};
    auto const parsed = require_codec(request);
    REQUIRE(parsed.channel_id == 3);
    REQUIRE(parsed.request_id == 7);
    REQUIRE(parsed.mining_job_token == request.mining_job_token);
    REQUIRE(parsed.version == request.version);
    REQUIRE(parsed.prev_hash == request.prev_hash);
    REQUIRE(parsed.ntime_start == request.ntime_start);
    REQUIRE(parsed.nbits == request.nbits);
    REQUIRE(parsed.coinbase_tx_version == 2);
    REQUIRE(parsed.coinbase_prefix == request.coinbase_prefix);
    REQUIRE(parsed.coinbase_tx_input_sequence == 0xffffffffu);
    REQUIRE(parsed.coinbase_tx_outputs == request.coinbase_tx_outputs);
    REQUIRE(parsed.coinbase_tx_locktime == 0);
    REQUIRE(parsed.merkle_path == request.merkle_path);
    REQUIRE(set_custom_mining_job::message_type == 0x22);
    REQUIRE(set_custom_mining_job::channel_message);
    auto const success = require_codec(set_custom_mining_job_success{3, 7, 11});
    REQUIRE(success.channel_id == 3);
    REQUIRE(success.request_id == 7);
    REQUIRE(success.job_id == 11);
    REQUIRE(set_custom_mining_job_success::message_type == 0x23);
    REQUIRE(set_custom_mining_job_success::channel_message);
    auto const error = require_codec(set_custom_mining_job_error{3, 7, "rejected"});
    REQUIRE(error.channel_id == 3);
    REQUIRE(error.request_id == 7);
    REQUIRE(error.error_code == "rejected");
    REQUIRE(set_custom_mining_job_error::message_type == 0x24);
    REQUIRE(set_custom_mining_job_error::channel_message);
    REQUIRE(require_codec(set_custom_mining_job{}).merkle_path.empty());
    REQUIRE(require_codec(set_custom_mining_job_error{}).error_code.empty());

    auto maximum = request;
    maximum.mining_job_token.resize(b0_255_max);
    maximum.coinbase_prefix.resize(b0_255_max);
    maximum.coinbase_tx_outputs.resize(b0_64k_max);
    maximum.merkle_path.resize(b0_255_max);
    REQUIRE(from_data_chunk<set_custom_mining_job>(to_data_chunk(maximum)).has_value());
    auto invalid = request;
    SECTION("oversized token") { invalid.mining_job_token.resize(b0_255_max + 1); }
    SECTION("oversized script prefix") { invalid.coinbase_prefix.resize(b0_255_max + 1); }
    SECTION("oversized outputs") { invalid.coinbase_tx_outputs.resize(b0_64k_max + 1); }
    SECTION("oversized merkle path") { invalid.merkle_path.resize(b0_255_max + 1); }
    require_refused(invalid);
}

TEST_CASE("sv2 jd solution keeps the full extranonce and solved header fields", "[sv2 jd]") {
    push_solution const message{{0, 1, 2, 0}, marked_hash(33), 0x04030201u,
        0x08070605u, 0x0c0b0a09u, 0x100f0e0du};
    auto const parsed = require_codec(message);
    REQUIRE(parsed.extranonce == message.extranonce);
    REQUIRE(parsed.prev_hash == message.prev_hash);
    REQUIRE(parsed.nonce == message.nonce);
    REQUIRE(parsed.ntime == message.ntime);
    REQUIRE(parsed.nbits == message.nbits);
    REQUIRE(parsed.version == message.version);
    auto const bytes = to_data_chunk(message);
    REQUIRE(data_chunk(bytes.end() - 16, bytes.end()) == data_chunk{1, 2, 3, 4,
        5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
    REQUIRE(push_solution::message_type == 0x60);
    REQUIRE_FALSE(push_solution::channel_message);
    REQUIRE(require_codec(push_solution{}).extranonce.empty());
    auto maximum = message;
    maximum.extranonce.resize(32);
    REQUIRE(from_data_chunk<push_solution>(to_data_chunk(maximum)).has_value());
    auto invalid = to_data_chunk(maximum);
    invalid[0] = 33;
    invalid.insert(invalid.begin() + 1, 0);
    REQUIRE_FALSE(from_data_chunk<push_solution>(invalid).has_value());
    maximum.extranonce.resize(33);
    require_refused(maximum);
}

TEST_CASE("sv2 jd declaration and transaction response respect the upstream frame bound", "[sv2 jd]") {
    declare_mining_job declaration;
    declaration.mining_job_token.resize(b0_255_max);
    declaration.coinbase_tx_prefix.resize(b0_64k_max);
    declaration.coinbase_tx_suffix.resize(b0_64k_max);
    declaration.excess_data.resize(b0_64k_max);
    // Even with every field at its maximum, DeclareMiningJob stays under the
    // U24 frame cap; what bounds it is the SEQ0_64K transaction id list.
    declaration.wtxid_list.resize(65535);
    REQUIRE(declaration.serialized_size() <= jd_frame_max_payload);
    REQUIRE(from_data_chunk<declare_mining_job>(to_data_chunk(declaration)).has_value());
    declaration.wtxid_list.push_back({});
    require_refused(declaration);
    REQUIRE_FALSE(from_data_chunk<declare_mining_job>(data_chunk(jd_frame_max_payload + 1)).has_value());

    // ProvideMissingTransactions.Success can reach the U24 frame cap: request id,
    // a SEQ0_64K count and one B0_16M length prefix are 9 bytes of overhead.
    provide_missing_transactions_success transactions{1, {data_chunk(jd_frame_max_payload - 9, 0xab)}};
    REQUIRE(transactions.serialized_size() == jd_frame_max_payload);
    auto const parsed = from_data_chunk<provide_missing_transactions_success>(to_data_chunk(transactions));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->transaction_list == transactions.transaction_list);
    transactions.transaction_list.front().push_back(0);
    require_refused(transactions);
    REQUIRE_FALSE(from_data_chunk<provide_missing_transactions_success>(data_chunk(jd_frame_max_payload + 1)).has_value());

    declare_mining_job small;
    SECTION("token") { small.mining_job_token.resize(b0_255_max + 1); }
    SECTION("prefix") { small.coinbase_tx_prefix.resize(b0_64k_max + 1); }
    SECTION("suffix") { small.coinbase_tx_suffix.resize(b0_64k_max + 1); }
    SECTION("excess data") { small.excess_data.resize(b0_64k_max + 1); }
    require_refused(small);
    require_refused(declare_mining_job_success{1, data_chunk(b0_255_max + 1)});
    require_refused(declare_mining_job_error{1, std::string(b0_255_max + 1, 'x'), {}});
    require_refused(declare_mining_job_error{1, {}, data_chunk(b0_64k_max + 1)});
    require_refused(set_custom_mining_job_error{1, 2, std::string(b0_255_max + 1, 'x')});
}
