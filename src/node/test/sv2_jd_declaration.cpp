// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/jd_declaration.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

template <typename Message>
constexpr uint16_t extension() {
    return framed_extension_type<Message>();
}

template <typename Message>
Message decode(data_chunk const& frame) {
    byte_reader source(frame);
    auto const header = message_header::from_data(source);
    REQUIRE(header);
    REQUIRE(header->extension_type == extension<Message>());
    REQUIRE(header->msg_type == Message::message_type);
    REQUIRE(header->msg_length == source.remaining_size());
    auto const message = Message::from_data(source);
    REQUIRE(message);
    REQUIRE(source.is_exhausted());
    return *message;
}

jd_declaration::result process(jd_declaration& state, jd_peer peer,
    uint16_t extension_type, uint8_t type, data_chunk const& payload) {
    return state.process(peer, {extension_type, type, static_cast<uint32_t>(payload.size())}, payload);
}

template <typename Message>
jd_declaration::result process(jd_declaration& state, jd_peer peer, Message const& message) {
    return process(state, peer, extension<Message>(), Message::message_type, to_data_chunk(message));
}

hash_digest target() {
    hash_digest value{};
    value.fill(0xff);
    value.back() = 0x42;
    return value;
}

open_extended_mining_channel_success channel() {
    return {1, 7, target(), jd_session::min_extranonce_size, {0xa1, 0xb2}, 9};
}

domain::chain::output::list outputs() {
    short_hash hash{};
    hash.fill(0x42);
    return {{0, domain::chain::script(
        domain::chain::script::to_pay_public_key_hash_pattern(hash)), std::nullopt},
        {0, domain::chain::script(data_chunk{0x6a, 0x01, 0x42}, false), std::nullopt}};
}

data_chunk output_bytes(domain::chain::output::list const& value) {
    size_t size = size_variable_integer(value.size());
    for (auto const& output : value) size += output.serialized_size(true);
    data_chunk bytes(size);
    byte_writer sink(bytes);
    REQUIRE(sink.write_size_little_endian(value.size()));
    for (auto const& output : value) REQUIRE(output.to_data(sink, true));
    return bytes;
}

domain::chain::output::list read_outputs(data_chunk const& bytes) {
    byte_reader source(bytes);
    auto const count = source.read_size_little_endian();
    REQUIRE(count);
    REQUIRE(*count <= 16);
    domain::chain::output::list value;
    for (size_t i = 0; i < *count; ++i) {
        auto output = domain::chain::output::from_data(source, true);
        REQUIRE(output);
        value.push_back(std::move(*output));
    }
    REQUIRE(source.is_exhausted());
    return value;
}

data_chunk transaction_bytes(transaction_const_ptr const& value) {
    return to_data_chunk(static_cast<domain::chain::transaction const&>(*value), true);
}

blockchain::mining_template mining_template() {
    blockchain::mining_template value{};
    value.version = 0x20000000u;
    value.height = 101;
    value.previous_block_hash.fill(0x24);
    value.bits = 0x207fffffu;
    value.min_time = 100;
    value.current_time = 101;
    value.coinbase_value = 5000000042;
    // ABLA's floor. The real limit is adaptive and comes with each template.
    value.size_limit = 32000000;
    value.sigchecks_limit = 640000;
    for (uint32_t id = 1; id <= 3; ++id) {
        auto paid = outputs();
        paid.resize(1);
        paid.front().set_value(id);
        if (id == 2) paid.front().set_token_data(domain::chain::make_fungible(null_hash, 7));
        value.selection.txs.push_back(std::make_shared<domain::message::transaction>(
            domain::chain::transaction{1, id, {}, std::move(paid)}));
    }
    std::sort(value.selection.txs.begin(), value.selection.txs.end(), [](auto const& left, auto const& right) {
        auto const a = left->hash();
        auto const b = right->hash();
        return std::lexicographical_compare(a.rbegin(), a.rend(), b.rbegin(), b.rend());
    });
    for (auto const& transaction : value.selection.txs) {
        value.selection.total_size += transaction_bytes(transaction).size();
    }
    return value;
}

allocate_mining_job_token_success allocation(uint32_t id = 1) {
    return {id, {1, 2, 3}, output_bytes(outputs())};
}

void require_failure(jd_declaration& state, jd_declaration::result const& result) {
    REQUIRE_FALSE(result.error.empty());
    REQUIRE(result.mining.empty());
    REQUIRE(result.declaration.empty());
    REQUIRE_FALSE(result.progress);
    REQUIRE_FALSE(result.accepted);
    REQUIRE_FALSE(state.pending());
    REQUIRE_FALSE(state.work());
}

jd_declaration allocating(bool full = true) {
    jd_declaration state("account.worker", full);
    auto const result = state.start(mining_template(), channel());
    REQUIRE(result.error.empty());
    REQUIRE(result.progress);
    REQUIRE(state.pending());
    REQUIRE(result.mining.empty());
    auto const request = decode<allocate_mining_job_token>(result.declaration);
    REQUIRE(request.request_id == 1);
    REQUIRE(request.user_identifier == "account.worker");
    return state;
}

jd_declaration declaring() {
    auto state = allocating();
    auto const result = process(state, jd_peer::declaration, allocation());
    REQUIRE(result.error.empty());
    REQUIRE(result.progress);
    REQUIRE_FALSE(result.declaration.empty());
    return state;
}

jd_declaration installing(bool full = true) {
    auto state = allocating(full);
    auto result = process(state, jd_peer::declaration, allocation());
    if (full) result = process(state, jd_peer::declaration, declare_mining_job_success{1, {9, 8}});
    REQUIRE(result.error.empty());
    REQUIRE(result.progress);
    REQUIRE_FALSE(result.mining.empty());
    return state;
}

jd_declaration requesting(bool full = true) {
    jd_declaration state("account.worker", full);
    state.use_payout_outputs(true);
    auto const started = state.start(mining_template(), channel());
    REQUIRE(started.error.empty());
    REQUIRE(decode<allocate_mining_job_token>(started.declaration).request_id == 1);
    auto const allocated = process(state, jd_peer::declaration, allocation());
    REQUIRE(allocated.error.empty());
    REQUIRE(allocated.mining.empty());
    REQUIRE(allocated.progress);
    REQUIRE(state.pending());
    REQUIRE_FALSE(state.work());
    REQUIRE(decode<request_payout_outputs>(allocated.declaration).request_id == 1);
    return state;
}

domain::chain::output::list paid_outputs(bool larger_first = false) {
    domain::chain::output low{1, domain::chain::script(data_chunk{0x51}, false), std::nullopt};
    domain::chain::output high{mining_template().coinbase_value - 1,
        domain::chain::script(data_chunk{0x52}, false), std::nullopt};
    if (larger_first) return {std::move(high), std::move(low)};
    return {std::move(low), std::move(high)};
}

request_payout_outputs_success payout_success(uint32_t id,
    domain::chain::output::list const& paid, bool exact) {
    request_payout_outputs_success success;
    success.request_id = id;
    success.flags = exact ? exact_outputs_flag : 0;
    success.coinbase_tx_version = exact ? 2u : 0u;
    success.coinbase_tx_input_sequence = exact ? 0xfffffffeu : 0u;
    success.coinbase_tx_locktime = exact ? 100u : 0u;
    success.coinbase_tx_outputs = output_bytes(paid);
    return success;
}

domain::chain::transaction declared_coinbase(data_chunk const& prefix, data_chunk const& suffix) {
    auto const upstream = channel();
    auto raw = prefix;
    raw.insert(raw.end(), upstream.extranonce_prefix.begin(), upstream.extranonce_prefix.end());
    raw.insert(raw.end(), upstream.extranonce_size, uint8_t{0});
    raw.insert(raw.end(), suffix.begin(), suffix.end());
    byte_reader source(raw);
    auto tx = domain::chain::transaction::from_data(source, true);
    REQUIRE(tx);
    REQUIRE(source.is_exhausted());
    return std::move(*tx);
}

template <typename Message, typename Factory>
void refuse_incomplete(Message const& message, jd_peer peer, Factory factory) {
    auto const bytes = to_data_chunk(message);
    for (size_t size = 0; size < bytes.size(); ++size) {
        CAPTURE(Message::message_type, size);
        auto state = factory();
        data_chunk const partial(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
        require_failure(state, process(state, peer, extension<Message>(), Message::message_type, partial));
    }
    auto state = factory();
    auto trailing = bytes;
    trailing.push_back(0);
    require_failure(state, process(state, peer, extension<Message>(), Message::message_type, trailing));
}

// One non-coinbase transaction whose script is `script_size` raw bytes.
// The declaration's hash list stays one id; the missing-transaction reply
// carries the script.
blockchain::mining_template template_with_script(size_t script_size) {
    auto value = mining_template();
    auto paid = outputs();
    paid.resize(1);
    paid.front().set_value(1);
    paid.front().set_script(domain::chain::script(data_chunk(script_size, 0x51), false));
    value.selection.txs = {std::make_shared<domain::message::transaction>(
        domain::chain::transaction{1, 1, {}, std::move(paid)})};
    value.selection.total_size = transaction_bytes(value.selection.txs.front()).size();
    return value;
}

} // namespace

TEST_CASE("sv2 JD modes install the same local template and whole-revenue pool coinbase", "[sv2 jd]") {
    for (bool const full : {false, true}) {
        CAPTURE(full);
        auto const tmpl = mining_template();
        auto const upstream = channel();
        auto state = allocating(full);
        auto result = process(state, jd_peer::declaration, allocation());
        REQUIRE(result.error.empty());
        REQUIRE(result.progress);
        REQUIRE_FALSE(result.accepted);
        REQUIRE(state.pending());
        REQUIRE(state.work());
        auto const declared_prefix = state.work()->coinbase_tx_prefix;
        auto const declared_suffix = state.work()->coinbase_tx_suffix;
        if (full) {
            REQUIRE(result.mining.empty());
            auto const declaration = decode<declare_mining_job>(result.declaration);
            REQUIRE(declaration.request_id == 1);
            REQUIRE(declaration.mining_job_token == data_chunk{1, 2, 3});
            REQUIRE(declaration.version == tmpl.version);
            REQUIRE(declaration.coinbase_tx_prefix == declared_prefix);
            REQUIRE(declaration.coinbase_tx_suffix == declared_suffix);
            REQUIRE(declaration.excess_data.empty());
            REQUIRE(declaration.wtxid_list.size() == tmpl.selection.txs.size());
            for (size_t i = 0; i < tmpl.selection.txs.size(); ++i) {
                REQUIRE(declaration.wtxid_list[i] == tmpl.selection.txs[i]->hash());
            }
            auto const missing = process(state, jd_peer::declaration, provide_missing_transactions{1, {2, 0}});
            REQUIRE(missing.error.empty());
            REQUIRE_FALSE(missing.progress);
            REQUIRE(missing.mining.empty());
            auto const response = decode<provide_missing_transactions_success>(missing.declaration);
            REQUIRE(response.request_id == 1);
            REQUIRE(response.transaction_list == std::vector<data_chunk>{
                transaction_bytes(tmpl.selection.txs[2]), transaction_bytes(tmpl.selection.txs[0])});
            auto const second = process(state, jd_peer::declaration, provide_missing_transactions{1, {1}});
            REQUIRE(second.error.empty());
            REQUIRE_FALSE(second.progress);
            REQUIRE(decode<provide_missing_transactions_success>(second.declaration).transaction_list
                == std::vector<data_chunk>{transaction_bytes(tmpl.selection.txs[1])});
            result = process(state, jd_peer::declaration, declare_mining_job_success{1, {9, 8}});
            REQUIRE(result.error.empty());
            REQUIRE(result.progress);
        }
        REQUIRE(result.declaration.empty());
        auto const custom = decode<set_custom_mining_job>(result.mining);
        REQUIRE(custom.channel_id == upstream.channel_id);
        REQUIRE(custom.request_id == 1);
        REQUIRE(custom.mining_job_token == (full ? data_chunk{9, 8} : data_chunk{1, 2, 3}));
        REQUIRE(custom.version == tmpl.version);
        REQUIRE(custom.prev_hash == tmpl.previous_block_hash);
        REQUIRE(custom.ntime_start == tmpl.current_time);
        REQUIRE(custom.nbits == tmpl.bits);
        REQUIRE(custom.coinbase_tx_version == 1);
        REQUIRE(custom.coinbase_tx_input_sequence == 0xffffffffu);
        REQUIRE(custom.coinbase_tx_locktime == 0);
        REQUIRE(custom.coinbase_prefix == state.work()->coinbase_prefix);
        REQUIRE(custom.coinbase_tx_outputs == state.work()->coinbase_tx_outputs);
        REQUIRE(custom.merkle_path == state.work()->merkle_path);
        auto const height = infrastructure::machine::number::from_int(static_cast<int64_t>(tmpl.height));
        REQUIRE(height);
        auto const bip34 = domain::machine::operation(height->data()).to_data();
        REQUIRE(custom.coinbase_prefix.size() >= bip34.size());
        REQUIRE(std::equal(bip34.begin(), bip34.end(), custom.coinbase_prefix.begin()));
        auto expected_outputs = outputs();
        expected_outputs.front().set_value(tmpl.coinbase_value);
        REQUIRE(read_outputs(custom.coinbase_tx_outputs) == expected_outputs);
        auto coinbase = declared_prefix;
        coinbase.insert(coinbase.end(), upstream.extranonce_prefix.begin(), upstream.extranonce_prefix.end());
        coinbase.insert(coinbase.end(), upstream.extranonce_size, uint8_t{0x5a});
        coinbase.insert(coinbase.end(), declared_suffix.begin(), declared_suffix.end());
        byte_reader source(coinbase);
        auto const transaction = domain::chain::transaction::from_data(source, true);
        REQUIRE(transaction);
        REQUIRE(source.is_exhausted());
        REQUIRE(transaction->outputs() == expected_outputs);
        auto const accepted = process(state, jd_peer::mining,
            set_custom_mining_job_success{upstream.channel_id, custom.request_id, 123});
        REQUIRE(accepted.error.empty());
        REQUIRE(accepted.progress);
        REQUIRE(accepted.accepted);
        REQUIRE(accepted.mining.empty());
        REQUIRE(accepted.declaration.empty());
        REQUIRE_FALSE(state.pending());
        REQUIRE(state.work());
        REQUIRE(state.work()->upstream_job_id == 123);
        REQUIRE(state.work()->upstream_channel_id == upstream.channel_id);
        REQUIRE(state.work()->upstream_target == upstream.target);
        REQUIRE(state.work()->upstream_extranonce_prefix == upstream.extranonce_prefix);
        REQUIRE(state.work()->upstream_extranonce_size == upstream.extranonce_size);
        REQUIRE(state.work()->coinbase_tx_prefix == declared_prefix);
        REQUIRE(state.work()->coinbase_tx_suffix == declared_suffix);
        auto next = tmpl;
        ++next.current_time;
        auto const restarted = state.start(next, upstream);
        REQUIRE(restarted.error.empty());
        REQUIRE(decode<allocate_mining_job_token>(restarted.declaration).request_id == 2);
        REQUIRE(state.pending());
    }
}

TEST_CASE("sv2 JD refuses unusable token outputs before any declaration or custom job", "[sv2 jd]") {
    for (unsigned variant = 0; variant < 12; ++variant) {
        CAPTURE(variant);
        auto state = allocating();
        auto response = allocation();
        if (variant == 0) ++response.request_id;
        if (variant == 1) response.mining_job_token.clear();
        if (variant == 2) response.coinbase_tx_outputs.clear();
        if (variant == 3) response.coinbase_tx_outputs = {0};
        if (variant == 4) response.coinbase_tx_outputs.push_back(0);
        if (variant == 5) {
            response.coinbase_tx_outputs.erase(response.coinbase_tx_outputs.begin());
            response.coinbase_tx_outputs.insert(response.coinbase_tx_outputs.begin(), {0xfd, 2, 0});
        }
        if (variant >= 6) {
            auto paid = outputs();
            if (variant == 6) paid.front().set_value(1);
            if (variant == 7) paid.back().set_value(1);
            if (variant == 8) paid.front().set_token_data(domain::chain::make_fungible(null_hash, 1));
            if (variant == 9) {
                auto const first = paid.front();
                paid.resize(257, first);
            }
            response.coinbase_tx_outputs = output_bytes(paid);
            if (variant == 10) response.coinbase_tx_outputs.pop_back();
            if (variant == 11) response.coinbase_tx_outputs = {1, 0};
        }
        require_failure(state, process(state, jd_peer::declaration, response));
        auto const repeated = state.start(mining_template(), channel());
        require_failure(state, repeated);
    }
    refuse_incomplete(allocation(), jd_peer::declaration, [] { return allocating(); });
}

TEST_CASE("sv2 JD declarations preserve every grant from the requested minimum to 32 bytes", "[sv2 jd]") {
    for (bool const full : {false, true}) {
        for (uint16_t size = 8; size <= 32; ++size) {
            CAPTURE(full, size);
            auto upstream = channel();
            upstream.extranonce_size = size;
            upstream.extranonce_prefix.assign(std::min<size_t>(4, 32 - size), 0x42);
            jd_declaration state("worker", full);
            auto result = state.start(mining_template(), upstream);
            REQUIRE(result.error.empty());
            REQUIRE(result.progress);
            result = process(state, jd_peer::declaration, allocation());
            REQUIRE(result.error.empty());
            REQUIRE(state.work());
            REQUIRE(state.work()->upstream_extranonce_size == size);
            REQUIRE(state.work()->upstream_extranonce_prefix == upstream.extranonce_prefix);
            if (full) {
                auto const declaration = decode<declare_mining_job>(result.declaration);
                REQUIRE(declaration.coinbase_tx_prefix == state.work()->coinbase_tx_prefix);
                REQUIRE(declaration.coinbase_tx_suffix == state.work()->coinbase_tx_suffix);
                result = process(state, jd_peer::declaration, declare_mining_job_success{1, {9}});
                REQUIRE(result.error.empty());
            }
            auto const custom = decode<set_custom_mining_job>(result.mining);
            REQUIRE(custom.coinbase_prefix == state.work()->coinbase_prefix);
            REQUIRE(custom.coinbase_tx_outputs == state.work()->coinbase_tx_outputs);
            auto const accepted = process(state, jd_peer::mining,
                set_custom_mining_job_success{upstream.channel_id, custom.request_id, 123});
            REQUIRE(accepted.error.empty());
            REQUIRE(accepted.accepted);
            REQUIRE(state.work()->upstream_extranonce_size == size);
            REQUIRE(state.work()->upstream_extranonce_prefix == upstream.extranonce_prefix);
        }
    }
}

TEST_CASE("sv2 JD bounds local input and refuses replacing an outstanding declaration", "[sv2 jd]") {
    for (auto const& identity : {std::string{}, std::string(256, 'x')}) {
        jd_declaration state(identity, true);
        require_failure(state, state.start(mining_template(), channel()));
    }
    for (unsigned variant = 0; variant < 4; ++variant) {
        CAPTURE(variant);
        auto upstream = channel();
        if (variant == 0) upstream.group_channel_id = upstream.channel_id;
        if (variant == 1) --upstream.extranonce_size;
        if (variant == 2) upstream.extranonce_prefix.resize(33 - upstream.extranonce_size);
        if (variant == 3) upstream.target.fill(0);
        jd_declaration state("worker", true);
        require_failure(state, state.start(mining_template(), upstream));
    }
    for (unsigned variant = 0; variant < 3; ++variant) {
        CAPTURE(variant);
        auto tmpl = mining_template();
        if (variant == 0) tmpl.selection.txs.front().reset();
        if (variant == 1) tmpl.selection.txs.resize(65536, tmpl.selection.txs.front());
        // The kept transactions are bounded by the template's own consensus
        // size limit (ABLA), so a template whose transactions exceed it fails.
        if (variant == 2) tmpl.size_limit = 1;
        jd_declaration state("worker", true);
        auto const result = state.start(std::move(tmpl), channel());
        require_failure(state, result);
        if (variant == 2) {
            REQUIRE(result.error.find("consensus block size limit") != std::string::npos);
        }
    }
    auto pending = allocating();
    require_failure(pending, pending.start(mining_template(), channel()));
    auto failed = jd_declaration("", true);
    require_failure(failed, failed.start(mining_template(), channel()));
    require_failure(failed, failed.start(mining_template(), channel()));

    auto const large = [] {
        auto tmpl = mining_template();
        tmpl.selection.txs.resize(32768, tmpl.selection.txs.front());
        tmpl.selection.total_size = 0;
        for (auto const& transaction : tmpl.selection.txs) {
            tmpl.selection.total_size += transaction_bytes(transaction).size();
        }
        return tmpl;
    };
    // 32768 ids are past the old 1 MiB payload cap and under the U24 frame
    // maximum. The raw transactions stay inside the retention bound, so the
    // declaration is sent.
    jd_declaration full("worker", true);
    auto const full_start = full.start(large(), channel());
    REQUIRE(full_start.error.empty());
    REQUIRE(full_start.progress);
    auto const declared = process(full, jd_peer::declaration, allocation());
    REQUIRE(declared.error.empty());
    REQUIRE(declared.progress);
    auto const job = decode<declare_mining_job>(declared.declaration);
    REQUIRE(job.request_id == 1);
    REQUIRE(job.wtxid_list.size() == 32768);
    REQUIRE(job.serialized_size() > jd_message_max_payload);
    REQUIRE(job.serialized_size() <= jd_frame_max_payload);
    jd_declaration coinbase_only("worker", false);
    REQUIRE(coinbase_only.start(large(), channel()).error.empty());
    auto const installed = process(coinbase_only, jd_peer::declaration, allocation());
    REQUIRE(installed.error.empty());
    REQUIRE(installed.declaration.empty());
    REQUIRE_FALSE(installed.mining.empty());

    auto small = mining_template();
    small.size_limit = 1;
    jd_declaration state("worker", true);
    REQUIRE(state.start(small, channel()).error.empty());
    require_failure(state, process(state, jd_peer::declaration, allocation()));
}

TEST_CASE("sv2 JD keeps unknown extensions inert and all core frame directions strict", "[sv2 jd]") {
    auto state = allocating();
    auto const ignored = process(state, jd_peer::declaration, 0x4000, 0x51, {1, 2, 3});
    REQUIRE(ignored.error.empty());
    REQUIRE(ignored.mining.empty());
    REQUIRE(ignored.declaration.empty());
    REQUIRE_FALSE(ignored.progress);
    REQUIRE(state.pending());
    REQUIRE_FALSE(state.work());
    for (unsigned variant = 0; variant < 5; ++variant) {
        CAPTURE(variant);
        auto current = allocating();
        auto const bytes = to_data_chunk(allocation());
        if (variant == 0) require_failure(current, process(current, jd_peer::mining, allocation()));
        if (variant == 1) require_failure(current, process(current, jd_peer::declaration,
            message_header::channel_msg_flag, allocate_mining_job_token_success::message_type, bytes));
        if (variant == 2) require_failure(current, current.process(jd_peer::declaration,
            {0, allocate_mining_job_token_success::message_type, static_cast<uint32_t>(bytes.size() + 1)}, bytes));
        if (variant == 3) require_failure(current, process(current, jd_peer::declaration,
            0x4000, 0x51, data_chunk(jd_message_max_payload + 1, 0)));
        if (variant == 4) require_failure(current, process(current, jd_peer::declaration,
            declare_mining_job_success{1, {9}}));
    }
    jd_declaration idle("worker", true);
    require_failure(idle, process(idle, jd_peer::declaration, allocation()));
    auto accepted = installing();
    REQUIRE(process(accepted, jd_peer::mining, set_custom_mining_job_success{7, 1, 123}).accepted);
    require_failure(accepted, process(accepted, jd_peer::mining, set_custom_mining_job_success{7, 1, 123}));
}

TEST_CASE("sv2 JD declaration refusals and invalid missing transaction requests are terminal", "[sv2 jd]") {
    auto refused = declaring();
    auto const refusal = process(refused, jd_peer::declaration,
        declare_mining_job_error{1, "unknown-pool-policy", {1, 2}});
    require_failure(refused, refusal);
    REQUIRE(refusal.error.find("unknown-pool-policy") != std::string::npos);
    for (unsigned variant = 0; variant < 7; ++variant) {
        CAPTURE(variant);
        auto state = declaring();
        if (variant == 0) require_failure(state, process(state, jd_peer::declaration,
            declare_mining_job_error{2, "refused", {}}));
        if (variant == 1) require_failure(state, process(state, jd_peer::declaration,
            declare_mining_job_success{2, {9}}));
        if (variant == 2) require_failure(state, process(state, jd_peer::declaration,
            declare_mining_job_success{1, {}}));
        if (variant == 3) require_failure(state, process(state, jd_peer::mining,
            declare_mining_job_success{1, {9}}));
        if (variant == 4) require_failure(state, process(state, jd_peer::declaration,
            provide_missing_transactions{2, {0}}));
        if (variant == 5) require_failure(state, process(state, jd_peer::declaration,
            provide_missing_transactions{1, {0, 0}}));
        if (variant == 6) require_failure(state, process(state, jd_peer::declaration,
            provide_missing_transactions{1, {3}}));
    }
    refuse_incomplete(declare_mining_job_error{1, "refused", {1}}, jd_peer::declaration, declaring);
    refuse_incomplete(declare_mining_job_success{1, {9}}, jd_peer::declaration, declaring);
    refuse_incomplete(provide_missing_transactions{1, {0, 2}}, jd_peer::declaration, declaring);
    auto coinbase_only = installing(false);
    require_failure(coinbase_only, process(coinbase_only, jd_peer::declaration,
        provide_missing_transactions{1, {0}}));
}

TEST_CASE("sv2 JD custom job replies must match the channel request and direction", "[sv2 jd]") {
    auto refused = installing();
    auto const refusal = process(refused, jd_peer::mining, set_custom_mining_job_error{7, 1, "policy-refused"});
    require_failure(refused, refusal);
    REQUIRE(refusal.error.find("policy-refused") != std::string::npos);
    for (unsigned variant = 0; variant < 6; ++variant) {
        CAPTURE(variant);
        auto state = installing();
        if (variant == 0) require_failure(state, process(state, jd_peer::mining,
            set_custom_mining_job_success{8, 1, 123}));
        if (variant == 1) require_failure(state, process(state, jd_peer::mining,
            set_custom_mining_job_success{7, 2, 123}));
        if (variant == 2) require_failure(state, process(state, jd_peer::mining,
            set_custom_mining_job_error{8, 1, "refused"}));
        if (variant == 3) require_failure(state, process(state, jd_peer::mining,
            set_custom_mining_job_error{7, 2, "refused"}));
        if (variant == 4) require_failure(state, process(state, jd_peer::declaration,
            set_custom_mining_job_success{7, 1, 123}));
        if (variant == 5) require_failure(state, process(state, jd_peer::mining, 0,
            set_custom_mining_job_success::message_type, to_data_chunk(set_custom_mining_job_success{7, 1, 123})));
    }
    refuse_incomplete(set_custom_mining_job_success{7, 1, 123}, jd_peer::mining, [] { return installing(); });
    refuse_incomplete(set_custom_mining_job_error{7, 1, "refused"}, jd_peer::mining, [] { return installing(); });
}

TEST_CASE("sv2 JD snapshots a target for the custom job and preserves it after submission", "[sv2 jd]") {
    auto first_target = target();
    first_target.back() = 0x32;
    auto second_target = first_target;
    second_target.back() = 0x22;
    auto later_target = second_target;
    later_target.back() = 0x12;
    auto state = allocating();
    state.note_target(first_target);
    REQUIRE(process(state, jd_peer::declaration, allocation()).error.empty());
    REQUIRE(state.work());
    REQUIRE(state.work()->upstream_target == first_target);
    state.note_target(second_target);
    REQUIRE(state.work()->upstream_target == second_target);
    REQUIRE(process(state, jd_peer::declaration, declare_mining_job_success{1, {9}}).error.empty());
    state.note_target(later_target);
    REQUIRE(state.work()->upstream_target == second_target);
    REQUIRE(process(state, jd_peer::mining, set_custom_mining_job_success{7, 1, 123}).accepted);
    state.note_target(target());
    REQUIRE(state.work()->upstream_target == second_target);
}

TEST_CASE("sv2 JD superseded declaration ignores a late reply and PushSolution follows full-template success", "[sv2 jd]") {
    jd_declaration fresh("worker", true);
    fresh.supersede();
    require_failure(fresh, process(fresh, jd_peer::declaration, allocation()));

    auto allocating_state = allocating();
    REQUIRE(allocating_state.has_template());
    REQUIRE(allocating_state.template_value().height == mining_template().height);
    REQUIRE_FALSE(allocating_state.can_push_solution());
    allocating_state.supersede();
    REQUIRE(allocating_state.has_template());
    REQUIRE_FALSE(allocating_state.pending());
    REQUIRE_FALSE(allocating_state.work());
    auto const late_allocation = process(allocating_state, jd_peer::declaration, allocation());
    REQUIRE(late_allocation.error.empty());
    REQUIRE(late_allocation.mining.empty());
    REQUIRE(late_allocation.declaration.empty());
    REQUIRE_FALSE(late_allocation.progress);
    REQUIRE_FALSE(allocating_state.pending());
    auto const restarted = allocating_state.start(mining_template(), channel());
    REQUIRE(restarted.error.empty());
    REQUIRE(decode<allocate_mining_job_token>(restarted.declaration).request_id == 2);

    auto declaring_state = declaring();
    REQUIRE_FALSE(declaring_state.can_push_solution());
    declaring_state.supersede();
    auto const late_success = process(declaring_state, jd_peer::declaration,
        declare_mining_job_success{1, {9}});
    REQUIRE(late_success.error.empty());
    REQUIRE_FALSE(late_success.progress);
    REQUIRE_FALSE(declaring_state.pending());
    REQUIRE_FALSE(declaring_state.work());
    REQUIRE(declaring_state.start(mining_template(), channel()).error.empty());

    auto full = installing(true);
    REQUIRE(full.can_push_solution());
    REQUIRE(process(full, jd_peer::mining, set_custom_mining_job_success{7, 1, 123}).accepted);
    REQUIRE(full.can_push_solution());
    full.supersede();
    REQUIRE(full.can_push_solution());
    REQUIRE(full.work());
    REQUIRE(full.work()->upstream_job_id == 123);

    auto coinbase_only = installing(false);
    REQUIRE_FALSE(coinbase_only.can_push_solution());
    REQUIRE(process(coinbase_only, jd_peer::mining, set_custom_mining_job_success{7, 1, 123}).accepted);
    REQUIRE_FALSE(coinbase_only.can_push_solution());

    auto refused = declaring();
    require_failure(refused, process(refused, jd_peer::declaration,
        declare_mining_job_error{1, "unknown-pool-policy", {}}));
    REQUIRE(refused.has_template());
    REQUIRE(refused.template_value().previous_block_hash == mining_template().previous_block_hash);
    REQUIRE_FALSE(refused.work());
    REQUIRE_FALSE(refused.can_push_solution());
}

TEST_CASE("sv2 JD payout request carries the token, template and coinbase prefix", "[sv2 jd]") {
    auto const tmpl = mining_template();
    auto const upstream = channel();
    jd_declaration state("account.worker", true);
    state.use_payout_outputs(true);
    auto const started = state.start(tmpl, upstream);
    REQUIRE(started.error.empty());
    auto const allocated = process(state, jd_peer::declaration, allocation());
    REQUIRE(allocated.error.empty());
    REQUIRE(allocated.mining.empty());
    REQUIRE_FALSE(state.work());
    auto const request = decode<request_payout_outputs>(allocated.declaration);
    REQUIRE(request.request_id == 1);
    REQUIRE(request.mining_job_token == data_chunk{1, 2, 3});
    REQUIRE(request.available_payout_value == tmpl.coinbase_value);
    REQUIRE(request.height == 101);
    REQUIRE(request.prev_hash == tmpl.previous_block_hash);
    REQUIRE(request.nbits == tmpl.bits);
    auto const prefix = node::mining::payout_coinbase_prefix(tmpl.height, request.request_id,
        upstream.extranonce_prefix.size(), upstream.extranonce_size);
    REQUIRE(prefix);
    REQUIRE(request.coinbase_prefix == *prefix);
    REQUIRE(allocation().coinbase_tx_outputs.size() >= output_bytes(paid_outputs()).size());
}

TEST_CASE("sv2 JD exact payout outputs are the declared coinbase, in order", "[sv2 jd]") {
    auto const paid = paid_outputs();
    auto const prefix = node::mining::payout_coinbase_prefix(101, 1, 2,
        jd_session::min_extranonce_size);
    REQUIRE(prefix);
    for (bool const full : {false, true}) {
        CAPTURE(full);
        auto state = requesting(full);
        auto const success = payout_success(1, paid, true);
        auto const result = process(state, jd_peer::declaration, success);
        REQUIRE(result.error.empty());
        REQUIRE(result.progress);
        REQUIRE(state.work());
        REQUIRE(state.work()->fixed_script);
        REQUIRE(state.work()->coinbase_version == 2);
        REQUIRE(state.work()->coinbase_input_sequence == 0xfffffffeu);
        REQUIRE(state.work()->coinbase_locktime == 100);
        REQUIRE(state.work()->coinbase_prefix == *prefix);
        REQUIRE(state.work()->outputs == paid);
        declare_mining_job declaration{};
        if (full) {
            REQUIRE(result.mining.empty());
            declaration = decode<declare_mining_job>(result.declaration);
            REQUIRE(declaration.request_id == 1);
            REQUIRE(declaration.mining_job_token == data_chunk{1, 2, 3});
            auto const tx = declared_coinbase(declaration.coinbase_tx_prefix,
                declaration.coinbase_tx_suffix);
            REQUIRE(tx.version() == 2);
            REQUIRE(tx.locktime() == 100);
            REQUIRE(tx.inputs().size() == 1);
            REQUIRE(tx.inputs().front().sequence() == 0xfffffffeu);
            REQUIRE(tx.outputs() == paid);
            auto const script = tx.inputs().front().script().bytes();
            auto const upstream = channel();
            REQUIRE(script.size() == prefix->size() + upstream.extranonce_prefix.size()
                + upstream.extranonce_size);
            REQUIRE(std::equal(prefix->begin(), prefix->end(), script.begin()));
            REQUIRE(std::equal(upstream.extranonce_prefix.begin(), upstream.extranonce_prefix.end(),
                script.begin() + static_cast<std::ptrdiff_t>(prefix->size())));
            auto const installed = process(state, jd_peer::declaration,
                declare_mining_job_success{1, {9, 8}});
            REQUIRE(installed.error.empty());
            declaration = {};
            auto const custom = decode<set_custom_mining_job>(installed.mining);
            REQUIRE(custom.coinbase_tx_version == 2);
            REQUIRE(custom.coinbase_tx_input_sequence == 0xfffffffeu);
            REQUIRE(custom.coinbase_tx_locktime == 100);
            REQUIRE(custom.coinbase_prefix == *prefix);
            REQUIRE(read_outputs(custom.coinbase_tx_outputs) == paid);
        } else {
            REQUIRE(result.declaration.empty());
            auto const custom = decode<set_custom_mining_job>(result.mining);
            REQUIRE(custom.request_id == 1);
            REQUIRE(custom.mining_job_token == data_chunk{1, 2, 3});
            REQUIRE(custom.coinbase_tx_version == 2);
            REQUIRE(custom.coinbase_tx_input_sequence == 0xfffffffeu);
            REQUIRE(custom.coinbase_tx_locktime == 100);
            REQUIRE(custom.coinbase_prefix == *prefix);
            REQUIRE(read_outputs(custom.coinbase_tx_outputs) == paid);
            auto const tx = declared_coinbase(state.work()->coinbase_tx_prefix,
                state.work()->coinbase_tx_suffix);
            REQUIRE(tx.version() == 2);
            REQUIRE(tx.locktime() == 100);
            REQUIRE(tx.inputs().front().sequence() == 0xfffffffeu);
            REQUIRE(tx.outputs() == paid);
        }
    }
}

TEST_CASE("sv2 JD payout outputs without the exact flag keep every output", "[sv2 jd]") {
    auto const paid = paid_outputs(true);
    REQUIRE(paid.front().value() != mining_template().coinbase_value);
    auto state = requesting();
    auto const success = payout_success(1, paid, false);
    REQUIRE(success.flags == 0);
    REQUIRE(success.coinbase_tx_version == 0);
    REQUIRE(success.coinbase_tx_input_sequence == 0);
    REQUIRE(success.coinbase_tx_locktime == 0);
    auto const result = process(state, jd_peer::declaration, success);
    REQUIRE(result.error.empty());
    auto const declaration = decode<declare_mining_job>(result.declaration);
    auto const tx = declared_coinbase(declaration.coinbase_tx_prefix, declaration.coinbase_tx_suffix);
    REQUIRE(tx.version() == 1);
    REQUIRE(tx.locktime() == 0);
    REQUIRE(tx.inputs().front().sequence() == 0xffffffffu);
    REQUIRE(tx.outputs() == paid);
    REQUIRE(state.work()->coinbase_version == 1);
    REQUIRE(state.work()->coinbase_input_sequence == 0xffffffffu);
    REQUIRE(state.work()->coinbase_locktime == 0);
    REQUIRE(state.work()->fixed_script);
    uint64_t sum = 0;
    for (auto const& output : tx.outputs()) sum += output.value();
    REQUIRE(sum == mining_template().coinbase_value);
}

TEST_CASE("sv2 JD rejects a payout success that is not the reserved revenue", "[sv2 jd]") {
    auto nonzero = requesting();
    auto bad_fields = payout_success(1, paid_outputs(), false);
    bad_fields.coinbase_tx_version = 2;
    require_failure(nonzero, process(nonzero, jd_peer::declaration, bad_fields));
    REQUIRE(nonzero.has_template());

    auto flags = requesting();
    auto unknown = payout_success(1, paid_outputs(), true);
    unknown.flags = 2;
    auto const flagged = process(flags, jd_peer::declaration, unknown);
    require_failure(flags, flagged);
    REQUIRE(flagged.error.find("unknown flags") != std::string::npos);

    jd_declaration reserved("account.worker", true);
    reserved.use_payout_outputs(true);
    REQUIRE(reserved.start(mining_template(), channel()).error.empty());
    auto const reserved_request = process(reserved, jd_peer::declaration,
        allocate_mining_job_token_success{1, {1, 2, 3}, data_chunk{0}});
    REQUIRE(reserved_request.error.empty());
    REQUIRE(decode<request_payout_outputs>(reserved_request.declaration).request_id == 1);
    auto const overflow = process(reserved, jd_peer::declaration, payout_success(1, paid_outputs(), true));
    require_failure(reserved, overflow);
    REQUIRE(overflow.error.find("reservation") != std::string::npos);

    auto mismatch = requesting();
    auto wrong = outputs();
    wrong.front().set_value(1);
    wrong.back().set_value(1);
    auto const unequal = process(mismatch, jd_peer::declaration, payout_success(1, wrong, true));
    require_failure(mismatch, unequal);
    REQUIRE(unequal.error.find("invalid payout outputs") != std::string::npos);

    jd_declaration tokens("account.worker", true);
    tokens.use_payout_outputs(true);
    REQUIRE(tokens.start(mining_template(), channel()).error.empty());
    REQUIRE(process(tokens, jd_peer::declaration,
        allocate_mining_job_token_success{1, {1, 2, 3}, data_chunk(400, 0x11)}).error.empty());
    auto cashtoken = paid_outputs();
    cashtoken.front().set_token_data(domain::chain::make_fungible(null_hash, 7));
    REQUIRE(output_bytes(cashtoken).size() <= 400);
    auto const with_token = process(tokens, jd_peer::declaration, payout_success(1, cashtoken, true));
    require_failure(tokens, with_token);
    REQUIRE(with_token.error.find("invalid payout outputs") != std::string::npos);

    auto tall = mining_template();
    tall.height = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;
    jd_declaration height("account.worker", true);
    height.use_payout_outputs(true);
    REQUIRE(height.start(tall, channel()).error.empty());
    auto const unfit = process(height, jd_peer::declaration, allocation());
    require_failure(height, unfit);
    REQUIRE(unfit.error.find("height") != std::string::npos);

    refuse_incomplete(payout_success(1, paid_outputs(), true), jd_peer::declaration, [] { return requesting(); });
    refuse_incomplete(request_payout_outputs_error{1, "invalid-request"}, jd_peer::declaration, [] { return requesting(); });
    refuse_incomplete(payout_outputs_stale{}, jd_peer::declaration, [] { return requesting(); });
}

TEST_CASE("sv2 JD stale payout outputs allocate again until the race repeats", "[sv2 jd]") {
    auto noticed = requesting();
    auto const notice = process(noticed, jd_peer::declaration, payout_outputs_stale{});
    REQUIRE(notice.error.empty());
    REQUIRE(notice.declaration.empty());
    REQUIRE(notice.mining.empty());
    REQUIRE(noticed.pending());
    auto const consumed = process(noticed, jd_peer::declaration, payout_success(1, paid_outputs(), true));
    REQUIRE(consumed.error.empty());
    REQUIRE(decode<allocate_mining_job_token>(consumed.declaration).request_id == 2);
    REQUIRE_FALSE(noticed.work());

    auto raced = requesting();
    auto const first = process(raced, jd_peer::declaration,
        request_payout_outputs_error{1, std::string(error_stale_payout_outputs)});
    REQUIRE(first.error.empty());
    REQUIRE(decode<allocate_mining_job_token>(first.declaration).request_id == 2);
    auto const second_request = process(raced, jd_peer::declaration, allocation(2));
    REQUIRE(decode<request_payout_outputs>(second_request.declaration).request_id == 2);
    auto const repeated = process(raced, jd_peer::declaration,
        request_payout_outputs_error{2, std::string(error_stale_payout_outputs)});
    require_failure(raced, repeated);
    REQUIRE(repeated.error.find("repeated stale") != std::string::npos);
    REQUIRE(raced.has_template());
    REQUIRE_FALSE(raced.work());

    // A notice clears the unexplained count, including one that arrives while
    // the replacement token is still being allocated.
    auto reset = requesting();
    auto const counted = process(reset, jd_peer::declaration,
        request_payout_outputs_error{1, std::string(error_stale_payout_outputs)});
    REQUIRE(counted.error.empty());
    REQUIRE(decode<allocate_mining_job_token>(counted.declaration).request_id == 2);
    auto const while_allocating = process(reset, jd_peer::declaration, payout_outputs_stale{});
    REQUIRE(while_allocating.error.empty());
    REQUIRE(while_allocating.declaration.empty());
    auto const discarded = process(reset, jd_peer::declaration, allocation(2));
    REQUIRE(decode<allocate_mining_job_token>(discarded.declaration).request_id == 3);
    REQUIRE(decode<request_payout_outputs>(
        process(reset, jd_peer::declaration, allocation(3)).declaration).request_id == 3);
    REQUIRE(decode<allocate_mining_job_token>(process(reset, jd_peer::declaration,
        request_payout_outputs_error{3, std::string(error_stale_payout_outputs)}).declaration)
        .request_id == 4);
    REQUIRE(decode<request_payout_outputs>(
        process(reset, jd_peer::declaration, allocation(4)).declaration).request_id == 4);
    auto const again = process(reset, jd_peer::declaration,
        request_payout_outputs_error{4, std::string(error_stale_payout_outputs)});
    require_failure(reset, again);
    REQUIRE(again.error.find("repeated stale") != std::string::npos);

    jd_declaration capped("account.worker", true);
    capped.use_payout_outputs(true);
    REQUIRE(decode<allocate_mining_job_token>(
        capped.start(mining_template(), channel()).declaration).request_id == 1);
    for (uint32_t i = 0; i < 8; ++i) {
        CAPTURE(i);
        auto const stale = process(capped, jd_peer::declaration, payout_outputs_stale{});
        REQUIRE(stale.error.empty());
        REQUIRE(stale.declaration.empty());
        auto const retried = process(capped, jd_peer::declaration, allocation(i + 1));
        REQUIRE(retried.error.empty());
        REQUIRE(decode<allocate_mining_job_token>(retried.declaration).request_id == i + 2);
    }
    REQUIRE(process(capped, jd_peer::declaration, payout_outputs_stale{}).error.empty());
    auto const unsettled = process(capped, jd_peer::declaration, allocation(9));
    require_failure(capped, unsettled);
    REQUIRE(unsettled.error.find("not settled") != std::string::npos);
}

TEST_CASE("sv2 JD other payout refusals fall back and leave the base path alone", "[sv2 jd]") {
    for (char const* code : {"invalid-mining-job-token", "invalid-request",
            "payout-outputs-too-large", "payout-outputs-unavailable"}) {
        CAPTURE(code);
        auto state = requesting();
        auto const refused = process(state, jd_peer::declaration,
            request_payout_outputs_error{1, code});
        require_failure(state, refused);
        REQUIRE(refused.error.find(code) != std::string::npos);
        REQUIRE(state.has_template());
        REQUIRE_FALSE(state.work());
    }

    auto wrong = requesting();
    require_failure(wrong, process(wrong, jd_peer::declaration, payout_success(2, paid_outputs(), true)));
    auto mining = requesting();
    require_failure(mining, process(mining, jd_peer::mining, payout_success(1, paid_outputs(), true)));

    auto base = allocating();
    base.use_payout_outputs(true);
    auto const declared = process(base, jd_peer::declaration, allocation());
    REQUIRE(declared.error.empty());
    REQUIRE_FALSE(decode<declare_mining_job>(declared.declaration).mining_job_token.empty());
    REQUIRE(base.work());
    REQUIRE_FALSE(base.work()->fixed_script);
    REQUIRE(base.work()->outputs.front().value() == mining_template().coinbase_value);

    auto idle = allocating();
    auto const ignored = process(idle, jd_peer::declaration, payout_outputs_stale{});
    REQUIRE(ignored.error.empty());
    REQUIRE(ignored.declaration.empty());
    REQUIRE(idle.pending());
    REQUIRE_FALSE(idle.work());
}

TEST_CASE("sv2 JD ignores a superseded reply after the next declaration starts", "[sv2 jd]") {
    auto state = allocating();
    state.supersede();
    auto const restarted = state.start(mining_template(), channel());
    REQUIRE(restarted.error.empty());
    REQUIRE(decode<allocate_mining_job_token>(restarted.declaration).request_id == 2);
    REQUIRE(state.pending());
    REQUIRE_FALSE(state.work());

    auto const late_token = process(state, jd_peer::declaration, allocation(1));
    REQUIRE(late_token.error.empty());
    REQUIRE(late_token.declaration.empty());
    REQUIRE(late_token.mining.empty());
    REQUIRE_FALSE(late_token.progress);
    REQUIRE_FALSE(late_token.accepted);
    REQUIRE(state.pending());
    REQUIRE_FALSE(state.work());

    auto const current_token = process(state, jd_peer::declaration, allocation(2));
    REQUIRE(current_token.error.empty());
    REQUIRE(current_token.progress);
    REQUIRE(decode<declare_mining_job>(current_token.declaration).request_id == 2);
    REQUIRE(state.pending());
    REQUIRE(state.work());
    REQUIRE_FALSE(state.work()->upstream_accepted);
    auto const generation = state.work()->generation;

    // The old token can also arrive once this declaration is already declaring.
    auto const late_token_again = process(state, jd_peer::declaration, allocation(1));
    REQUIRE(late_token_again.error.empty());
    REQUIRE(late_token_again.declaration.empty());
    REQUIRE(late_token_again.mining.empty());
    REQUIRE_FALSE(late_token_again.progress);
    REQUIRE(state.pending());
    REQUIRE(state.work()->generation == generation);
    REQUIRE_FALSE(state.can_push_solution());

    auto const late_declare = process(state, jd_peer::declaration,
        declare_mining_job_success{1, {9, 8}});
    REQUIRE(late_declare.error.empty());
    REQUIRE(late_declare.declaration.empty());
    REQUIRE(late_declare.mining.empty());
    REQUIRE_FALSE(late_declare.progress);
    REQUIRE(state.pending());
    REQUIRE_FALSE(state.can_push_solution());
    REQUIRE(state.work()->generation == generation);

    auto const late_error = process(state, jd_peer::declaration,
        declare_mining_job_error{1, "stale", {}});
    REQUIRE(late_error.error.empty());
    REQUIRE(late_error.declaration.empty());
    REQUIRE(state.pending());
    REQUIRE(state.work()->generation == generation);

    auto const late_missing = process(state, jd_peer::declaration,
        provide_missing_transactions{1, {0}});
    REQUIRE(late_missing.error.empty());
    REQUIRE(late_missing.declaration.empty());
    REQUIRE(late_missing.mining.empty());
    REQUIRE_FALSE(late_missing.progress);
    REQUIRE(state.pending());
    REQUIRE(state.work()->generation == generation);

    auto const late_custom = process(state, jd_peer::mining,
        set_custom_mining_job_success{7, 1, 123});
    REQUIRE(late_custom.error.empty());
    REQUIRE_FALSE(late_custom.accepted);
    REQUIRE(late_custom.mining.empty());
    REQUIRE(state.pending());
    REQUIRE_FALSE(state.work()->upstream_accepted);

    auto const installed = process(state, jd_peer::declaration,
        declare_mining_job_success{2, {9, 8}});
    REQUIRE(installed.error.empty());
    REQUIRE(installed.progress);
    REQUIRE_FALSE(installed.mining.empty());
    REQUIRE(state.can_push_solution());
    REQUIRE(state.work()->generation == generation);
    REQUIRE_FALSE(state.work()->upstream_accepted);
}

TEST_CASE("sv2 JD still rejects a request id that is neither current nor retired", "[sv2 jd]") {
    auto allocating_state = allocating();
    allocating_state.supersede();
    REQUIRE(allocating_state.start(mining_template(), channel()).error.empty());
    require_failure(allocating_state, process(allocating_state, jd_peer::declaration, allocation(99)));

    auto declaring_state = declaring();
    declaring_state.supersede();
    REQUIRE(declaring_state.start(mining_template(), channel()).error.empty());
    REQUIRE(process(declaring_state, jd_peer::declaration, allocation(2)).error.empty());
    require_failure(declaring_state, process(declaring_state, jd_peer::declaration,
        declare_mining_job_success{99, {9}}));
}

TEST_CASE("sv2 JD full-template sends a missing-transaction response above 1 MiB and under the frame maximum", "[sv2 jd]") {
    auto tmpl = template_with_script(jd_message_max_payload);
    auto const raw = transaction_bytes(tmpl.selection.txs.front());
    constexpr size_t response_overhead = 4 + 2 + 3;
    REQUIRE(raw.size() > jd_message_max_payload);
    REQUIRE(response_overhead + raw.size() > jd_message_max_payload);
    REQUIRE(response_overhead + raw.size() <= jd_frame_max_payload);
    REQUIRE(raw.size() <= tmpl.size_limit);

    jd_declaration state("worker", true);
    auto const started = state.start(std::move(tmpl), channel());
    REQUIRE(started.error.empty());
    REQUIRE(started.progress);
    auto const declared = process(state, jd_peer::declaration, allocation());
    REQUIRE(declared.error.empty());
    REQUIRE(declared.progress);
    auto const job = decode<declare_mining_job>(declared.declaration);
    REQUIRE(job.request_id == 1);
    REQUIRE(job.wtxid_list.size() == 1);
    REQUIRE(job.serialized_size() <= jd_frame_max_payload);

    auto const missing = process(state, jd_peer::declaration, provide_missing_transactions{1, {0}});
    REQUIRE(missing.error.empty());
    REQUIRE(missing.mining.empty());
    REQUIRE_FALSE(missing.progress);
    REQUIRE(state.pending());
    REQUIRE(state.work());
    REQUIRE(missing.declaration.size() > message_header::size + jd_message_max_payload);
    auto const response = decode<provide_missing_transactions_success>(missing.declaration);
    REQUIRE(response.request_id == 1);
    REQUIRE(response.serialized_size() > jd_message_max_payload);
    REQUIRE(response.serialized_size() <= jd_frame_max_payload);
    REQUIRE(response.transaction_list == std::vector<data_chunk>{raw});

    auto const installed = process(state, jd_peer::declaration, declare_mining_job_success{1, {9}});
    REQUIRE(installed.error.empty());
    REQUIRE(installed.progress);
    REQUIRE(state.can_push_solution());
}

TEST_CASE("sv2 JD full-template declines a list above SEQ0_64K and a reply above the frame maximum", "[sv2 jd]") {
    auto too_many = mining_template();
    too_many.selection.txs.resize(65536, too_many.selection.txs.front());
    jd_declaration counted("worker", true);
    auto const counted_result = counted.start(std::move(too_many), channel());
    require_failure(counted, counted_result);
    REQUIRE(counted_result.error.find("SEQ0_64K limit of 65535") != std::string::npos);

    // Payload is the request id, the count, a B0_16M length, and the
    // transaction (9 bytes plus the transaction). A script of (U24 max - 8)
    // already makes that payload exceed one frame. The template still fits
    // its own consensus size limit, so the refusal happens when the reply is
    // built: the U24 frame is the protocol's ceiling, which ABLA can outgrow.
    auto tmpl = template_with_script(jd_frame_max_payload - 8);
    auto const raw = transaction_bytes(tmpl.selection.txs.front());
    constexpr size_t response_overhead = 4 + 2 + 3;
    REQUIRE(response_overhead + raw.size() > jd_frame_max_payload);
    REQUIRE(raw.size() <= tmpl.size_limit);

    jd_declaration state("worker", true);
    REQUIRE(state.start(std::move(tmpl), channel()).error.empty());
    auto const declared = process(state, jd_peer::declaration, allocation());
    REQUIRE(declared.error.empty());
    REQUIRE(declared.progress);
    auto const missing = process(state, jd_peer::declaration, provide_missing_transactions{1, {0}});
    require_failure(state, missing);
    REQUIRE(missing.error.find("ProvideMissingTransactions.Success exceeds the SV2 frame maximum")
        != std::string::npos);
    REQUIRE(state.has_template());
}
