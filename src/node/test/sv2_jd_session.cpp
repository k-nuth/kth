// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <kth/domain/version.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/extensions.hpp>
#include <kth/node/sv2/jd_session.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

std::string knuth_vendor() {
    return std::string(kth::client_name) + " " + std::string(kth::version);
}

template <typename Message>
constexpr uint16_t extension() {
    return framed_extension_type<Message>();
}

jd_session::result process(jd_session& session, uint16_t extension_type,
    uint8_t type, data_chunk const& payload) {
    return session.process({extension_type, type, static_cast<uint32_t>(payload.size())}, payload);
}

template <typename Message>
jd_session::result process(jd_session& session, Message const& message) {
    return process(session, extension<Message>(), Message::message_type, to_data_chunk(message));
}

template <typename Message>
Message single(data_chunk const& bytes) {
    byte_reader reader(bytes);
    auto const header = message_header::from_data(reader);
    REQUIRE(header.has_value());
    REQUIRE(header->extension_type == extension<Message>());
    REQUIRE(header->msg_type == Message::message_type);
    REQUIRE(header->msg_length == reader.remaining_size());
    auto const message = Message::from_data(reader);
    REQUIRE(message.has_value());
    REQUIRE(reader.is_exhausted());
    return *message;
}

hash_digest target() {
    hash_digest out{};
    out.fill(0xff);
    return out;
}

open_extended_mining_channel_success opened() {
    return {1, 7, target(), jd_session::min_extranonce_size, {1, 2}, 9};
}

jd_session waiting(jd_peer peer = jd_peer::mining, bool full_template = true,
    bool worker_names = false) {
    jd_session session(peer, "pool.example", 8442, "worker", full_template, worker_names);
    REQUIRE_FALSE(session.start().empty());
    return session;
}

jd_session opening() {
    auto session = waiting();
    auto const response = process(session, setup_connection_success{2, 0});
    REQUIRE_FALSE(response.close);
    REQUIRE_FALSE(response.ready);
    REQUIRE_FALSE(response.response.empty());
    return session;
}

jd_session ready() {
    auto session = opening();
    auto const response = process(session, opened());
    REQUIRE_FALSE(response.close);
    REQUIRE(response.ready);
    REQUIRE(session.ready());
    REQUIRE(session.channel().has_value());
    return session;
}

std::vector<uint16_t> worker_tracking() {
    return {worker_hashrate_tracking_extension};
}

// A session for an upstream the operator runs, after SetupConnection.Success,
// with RequestExtensions unanswered.
jd_session asking() {
    auto session = waiting(jd_peer::mining, true, true);
    auto const response = process(session, setup_connection_success{2, 0});
    REQUIRE_FALSE(response.close);
    REQUIRE_FALSE(response.ready);
    auto const request = single<request_extensions>(response.response);
    REQUIRE(request.request_id == 1);
    REQUIRE(request.requested_extensions == worker_tracking());
    REQUIRE(session.awaiting_extensions());
    return session;
}

// The channel request that follows the pool's answer: a grant of
// Worker-Specific Hashrate Tracking or its refusal.
open_extended_mining_channel answer(jd_session& session, bool grant) {
    auto const response = grant
        ? process(session, request_extensions_success{1, worker_tracking()})
        : process(session, request_extensions_error{1, worker_tracking(), {}});
    REQUIRE_FALSE(response.close);
    REQUIRE_FALSE(response.ready);
    REQUIRE_FALSE(session.awaiting_extensions());
    REQUIRE(session.worker_identities() == grant);
    return single<open_extended_mining_channel>(response.response);
}

// A marked session whose pool granted 0x0002, with its channel open.
jd_session ready_granted() {
    auto session = asking();
    (void)answer(session, true);
    auto const response = process(session, opened());
    REQUIRE_FALSE(response.close);
    REQUIRE(response.ready);
    REQUIRE(session.channel().has_value());
    return session;
}

void require_closed(jd_session& session, jd_session::result const& result) {
    REQUIRE(result.close);
    REQUIRE_FALSE(result.ready);
    REQUIRE(result.response.empty());
    REQUIRE_FALSE(result.error.empty());
    REQUIRE_FALSE(session.ready());
    REQUIRE_FALSE(session.channel().has_value());
    auto const again = process(session, setup_connection_success{2, 0});
    REQUIRE(again.close);
    REQUIRE(again.error == result.error);
}

void require_silent(jd_session::result const& result, bool is_ready = true) {
    REQUIRE_FALSE(result.close);
    REQUIRE(result.ready == is_ready);
    REQUIRE(result.response.empty());
    REQUIRE(result.error.empty());
}

template <typename Message, typename Factory>
void refuse_incomplete(Message const& message, Factory factory) {
    auto const bytes = to_data_chunk(message);
    for (size_t size = 0; size < bytes.size(); ++size) {
        CAPTURE(Message::message_type, size);
        auto session = factory();
        data_chunk const short_bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
        require_closed(session, process(session, extension<Message>(), Message::message_type, short_bytes));
    }
    auto session = factory();
    auto trailing = bytes;
    trailing.push_back(0);
    require_closed(session, process(session, extension<Message>(), Message::message_type, trailing));
}

} // namespace

TEST_CASE("sv2 jd setup selects the requested mode and opens one extended upstream channel", "[sv2 jd]") {
    for (bool const full_template : {false, true}) {
        CAPTURE(full_template);
        jd_session session(jd_peer::declaration, "jds.example", 8443, "worker", full_template);
        auto const setup = single<setup_connection>(session.start());
        REQUIRE(setup.protocol == setup_connection::protocol_job_declaration);
        REQUIRE(setup.min_version == 2);
        REQUIRE(setup.max_version == 2);
        REQUIRE(setup.flags == (full_template ? 1u : 0u));
        REQUIRE(setup.endpoint_host == "jds.example");
        REQUIRE(setup.endpoint_port == 8443);
        REQUIRE(setup.vendor == knuth_vendor());
        require_silent(process(session, setup_connection_success{2, 0}));
        REQUIRE(session.ready());
        REQUIRE_FALSE(session.channel());
    }
    for (uint32_t const flags : {0u, mining_flag_requires_extended_channels}) {
        CAPTURE(flags);
        jd_session session(jd_peer::mining, "pool.example", 8442, "worker", false);
        auto const setup = single<setup_connection>(session.start());
        REQUIRE(setup.protocol == setup_connection::protocol_mining);
        REQUIRE(setup.min_version == 2);
        REQUIRE(setup.max_version == 2);
        REQUIRE(setup.flags == (mining_flag_requires_work_selection | mining_flag_requires_version_rolling));
        REQUIRE(setup.endpoint_host == "pool.example");
        REQUIRE(setup.endpoint_port == 8442);
        auto const result = process(session, setup_connection_success{2, flags});
        REQUIRE_FALSE(result.close);
        REQUIRE_FALSE(result.ready);
        REQUIRE_FALSE(session.ready());
        auto const request = single<open_extended_mining_channel>(result.response);
        REQUIRE(request.request_id == 1);
        REQUIRE(request.user_identity == "worker");
        REQUIRE(request.nominal_hash_rate == 0.f);
        REQUIRE(request.max_target == target());
        REQUIRE(request.min_extranonce_size == jd_session::min_extranonce_size);
        require_silent(process(session, opened()));
        REQUIRE(session.channel()->channel_id == 7);
        REQUIRE(session.channel()->group_channel_id == 9);
        REQUIRE(session.channel()->target == target());
        REQUIRE(session.channel()->extranonce_size == jd_session::min_extranonce_size);
        REQUIRE(session.channel()->extranonce_prefix == data_chunk{1, 2});
    }
}

TEST_CASE("sv2 jd setup names the Knuth version and the software package", "[sv2 jd]") {
    // sv2-spec 3.6.1: vendor MUST carry the developer and its version, and
    // hardware_version SHOULD name the package in use.
    REQUIRE_FALSE(kth::version.empty());
    for (auto const peer : {jd_peer::mining, jd_peer::declaration}) {
        CAPTURE(peer == jd_peer::mining);
        jd_session session(peer, "pool.example", 8442, "worker", true);
        auto const setup = single<setup_connection>(session.start());
        REQUIRE(setup.vendor == knuth_vendor());
        REQUIRE(setup.vendor.find(kth::version) != std::string::npos);
        REQUIRE_FALSE(setup.hardware_version.empty());
        REQUIRE(setup.hardware_version.starts_with(kth::client_name));
        REQUIRE(setup.device_id.empty());
    }
}

TEST_CASE("sv2 jd session startup is bounded and single use", "[sv2 jd]") {
    struct settings {
        std::string host;
        uint16_t port;
        std::string identity;
    };
    for (auto const& value : std::vector<settings>{{"", 1, "x"}, {std::string(256, 'x'), 1, "x"},
            {"x", 0, "x"}, {"x", 1, ""}, {"x", 1, std::string(256, 'x')}}) {
        jd_session session(jd_peer::mining, value.host, value.port, value.identity, false);
        REQUIRE(session.start().empty());
        require_closed(session, process(session, setup_connection_success{2, 0}));
    }
    jd_session unstarted(jd_peer::mining, "x", 1, "x", false);
    require_closed(unstarted, process(unstarted, setup_connection_success{2, 0}));
    auto session = waiting();
    REQUIRE(session.start().empty());
    require_closed(session, process(session, setup_connection_success{2, 0}));
    jd_session largest(jd_peer::declaration, std::string(255, 'x'), 65535, std::string(255, 'y'), false);
    REQUIRE_FALSE(largest.start().empty());
}

TEST_CASE("sv2 jd setup refuses wrong versions flags directions and malformed replies", "[sv2 jd]") {
    for (auto const peer : {jd_peer::mining, jd_peer::declaration}) {
        for (uint16_t const version : {uint16_t{0}, uint16_t{1}, uint16_t{3}}) {
            auto session = waiting(peer);
            auto const result = process(session, setup_connection_success{version, 0});
            require_closed(session, result);
            REQUIRE(result.incompatible);
        }
        for (uint32_t const flags : {mining_flag_requires_fixed_version, uint32_t{4}, uint32_t{0xffffffff}}) {
            auto session = waiting(peer);
            auto const result = process(session, setup_connection_success{2, flags});
            require_closed(session, result);
            REQUIRE(result.incompatible);
        }
        auto rejected = waiting(peer);
        auto const refusal = process(rejected, setup_connection_error{2, "unsupported-feature-flags"});
        require_closed(rejected, refusal);
        REQUIRE(refusal.incompatible);
        auto wrong_direction = waiting(peer);
        auto const misplaced = process(wrong_direction, setup_connection{});
        require_closed(wrong_direction, misplaced);
        REQUIRE_FALSE(misplaced.incompatible);
        auto channel_flag = waiting(peer);
        require_closed(channel_flag, process(channel_flag, message_header::channel_msg_flag,
            setup_connection_success::message_type, to_data_chunk(setup_connection_success{2, 0})));
        refuse_incomplete(setup_connection_success{2, 0}, [peer] { return waiting(peer); });
        refuse_incomplete(setup_connection_error{0, "refused"}, [peer] { return waiting(peer); });
    }
    auto jd = waiting(jd_peer::declaration);
    require_closed(jd, process(jd, setup_connection_success{2, mining_flag_requires_extended_channels}));
}

TEST_CASE("sv2 jd malformed setup replies remain transient failures", "[sv2 jd]") {
    for (auto const peer : {jd_peer::mining, jd_peer::declaration}) {
        for (auto const& reply : {std::pair{setup_connection_success::message_type,
                to_data_chunk(setup_connection_success{2, 0})},
                std::pair{setup_connection_error::message_type,
                to_data_chunk(setup_connection_error{0, "refused"})}}) {
            for (size_t size = 0; size < reply.second.size(); ++size) {
                CAPTURE(peer == jd_peer::mining, reply.first, size);
                auto session = waiting(peer);
                data_chunk const bytes(reply.second.begin(), reply.second.begin()
                    + static_cast<std::ptrdiff_t>(size));
                auto const result = process(session, 0, reply.first, bytes);
                require_closed(session, result);
                REQUIRE_FALSE(result.incompatible);
            }
            auto session = waiting(peer);
            auto trailing = reply.second;
            trailing.push_back(0);
            auto const result = process(session, 0, reply.first, trailing);
            require_closed(session, result);
            REQUIRE_FALSE(result.incompatible);
        }
    }
}

TEST_CASE("sv2 jd extended channel replies must match and leave bounded extranonce space", "[sv2 jd]") {
    for (unsigned variant = 0; variant < 6; ++variant) {
        CAPTURE(variant);
        auto success = opened();
        if (variant == 0) success.request_id = 2;
        if (variant == 1) success.target.fill(0);
        if (variant == 2) success.extranonce_size = jd_session::min_extranonce_size - 1;
        if (variant == 3) success.extranonce_size = 33;
        if (variant == 4) success.extranonce_prefix.resize(33 - success.extranonce_size);
        if (variant == 5) success.group_channel_id = success.channel_id;
        auto session = opening();
        require_closed(session, process(session, success));
    }
    for (uint32_t const id : {1u, 2u}) {
        auto session = opening();
        require_closed(session, process(session, open_mining_channel_error{id, "refused"}));
    }
    auto wrong_type = opening();
    require_closed(wrong_type, process(wrong_type, setup_connection_success{2, 0}));
    auto wrong_flag = opening();
    require_closed(wrong_flag, process(wrong_flag, message_header::channel_msg_flag,
        open_extended_mining_channel_success::message_type, to_data_chunk(opened())));
    refuse_incomplete(opened(), opening);
    refuse_incomplete(open_mining_channel_error{1, "refused"}, opening);
    auto maximum = opened();
    maximum.extranonce_size = 32;
    maximum.extranonce_prefix.clear();
    auto session = opening();
    require_silent(process(session, maximum));
}

TEST_CASE("sv2 jd upstream request fits eight bytes and accepts larger grants", "[sv2 jd]") {
    auto session = waiting();
    auto const result = process(session, setup_connection_success{2, 0});
    auto const request = single<open_extended_mining_channel>(result.response);
    REQUIRE(request.min_extranonce_size == 8);
    for (uint16_t size = 8; size <= 32; ++size) {
        CAPTURE(size);
        auto opened_session = opening();
        auto success = opened();
        success.extranonce_size = size;
        success.extranonce_prefix = {0xa1, 0xb2, 0xc3, 0xd4};
        success.extranonce_prefix.resize(std::min<size_t>(4, 32 - size));
        require_silent(process(opened_session, success));
        REQUIRE(opened_session.channel()->extranonce_size == size);
        REQUIRE(opened_session.channel()->extranonce_prefix == success.extranonce_prefix);
    }
    auto too_small = opening();
    auto success = opened();
    success.extranonce_size = request.min_extranonce_size - 1;
    auto const refused = process(too_small, success);
    require_closed(too_small, refused);
    REQUIRE(refused.error == "invalid upstream extended channel");
}

TEST_CASE("sv2 jd names full-template when a JDS refuses coinbase-only setup", "[sv2 jd]") {
    // SetupConnection.Error carries the flags behind the refusal. DECLARE_TX_DATA
    // (bit 0) from a JDS means it accepts only full-template declarations.
    auto coinbase_only = waiting(jd_peer::declaration, false);
    auto const refused = process(coinbase_only, setup_connection_error{1, "unsupported-feature-flags"});
    require_closed(coinbase_only, refused);
    REQUIRE(refused.error == "upstream refused setup (unsupported-feature-flags); "
        "this JDS requires full-template (sv2.jd.mode = full-template)");

    // The hint is only for the JDS, and only when this node did not offer the flag.
    for (auto const& [peer, full] : {std::pair{jd_peer::declaration, true}, std::pair{jd_peer::mining, false}}) {
        auto setup = waiting(peer, full);
        auto const plain = process(setup, setup_connection_error{1, "unsupported-feature-flags"});
        require_closed(setup, plain);
        REQUIRE(plain.error == "upstream refused setup (unsupported-feature-flags)");
    }
}

TEST_CASE("sv2 jd refusal diagnostics retain bounded printable upstream error codes", "[sv2 jd]") {
    std::string const controls{'\0', '\t', '\n', '\r', '\x1b', '\x7f'};
    auto const cases = std::vector<std::pair<std::string, std::string>>{
        {"min-extranonce-size-too-large", "min-extranonce-size-too-large"},
        {"unsupported-feature-flags", "unsupported-feature-flags"},
        {" ~", " ~"}, {"", ""},
        {controls + "\xc3\xa9", "????????"},
        {std::string(255, 'x'), std::string(64, 'x')}};
    for (auto const& [code, printable] : cases) {
        CAPTURE(code.size());
        for (auto const peer : {jd_peer::mining, jd_peer::declaration}) {
            auto setup = waiting(peer);
            auto const refused = process(setup, setup_connection_error{0, code});
            require_closed(setup, refused);
            REQUIRE(refused.error == "upstream refused setup (" + printable + ")");
            REQUIRE(std::all_of(refused.error.begin(), refused.error.end(), [](unsigned char byte) {
                return byte >= 0x20 && byte <= 0x7e;
            }));
        }
        auto channel = opening();
        auto const refused = process(channel, open_mining_channel_error{1, code});
        require_closed(channel, refused);
        REQUIRE(refused.error == "upstream refused extended channel (" + printable + ")");
        REQUIRE(std::all_of(refused.error.begin(), refused.error.end(), [](unsigned char byte) {
            return byte >= 0x20 && byte <= 0x7e;
        }));
    }
}

TEST_CASE("sv2 jd upstream jobs do not replace selected work and channel updates remain bounded", "[sv2 jd]") {
    auto session = ready();
    auto updated = target();
    updated.back() = 0x7f;
    for (uint32_t const id : {7u, 9u}) {
        require_silent(process(session, set_target{id, updated}));
        REQUIRE(session.channel()->target == updated);
        require_silent(process(session, new_extended_mining_job{id, 11, std::nullopt, 0x20000000,
            true, {}, {1}, {2}}));
        require_silent(process(session, mining_set_new_prev_hash{id, 11, target(), 1, 0x207fffff}));
        REQUIRE(session.channel()->extranonce_prefix == data_chunk{1, 2});
    }
    require_silent(process(session, set_extranonce_prefix{9, data_chunk(32, 3)}));
    REQUIRE(session.channel()->extranonce_prefix == data_chunk{1, 2});
    auto const prefix_size = 32 - jd_session::min_extranonce_size;
    require_silent(process(session, set_extranonce_prefix{7, data_chunk(prefix_size, 3)}));
    REQUIRE(session.channel()->extranonce_prefix == data_chunk(prefix_size, 3));
    require_closed(session, process(session, set_extranonce_prefix{7, data_chunk(prefix_size + 1, 3)}));
}

TEST_CASE("sv2 jd ready upstream refuses malformed misplaced and unsolicited base messages", "[sv2 jd]") {
    auto const job = new_extended_mining_job{7, 11, std::nullopt, 0x20000000, true, {}, {1}, {2}};
    auto const previous = mining_set_new_prev_hash{7, 11, target(), 1, 0x207fffff};
    refuse_incomplete(set_target{7, target()}, ready);
    refuse_incomplete(set_extranonce_prefix{7, {1}}, ready);
    refuse_incomplete(job, ready);
    refuse_incomplete(previous, ready);
    refuse_incomplete(close_channel{7, "closed"}, ready);
    auto const bad_channel = uint32_t{8};
    auto const messages = std::vector<std::pair<uint8_t, data_chunk>>{
        {set_target::message_type, to_data_chunk(set_target{bad_channel, target()})},
        {set_extranonce_prefix::message_type, to_data_chunk(set_extranonce_prefix{bad_channel, {1}})},
        {new_extended_mining_job::message_type, to_data_chunk(new_extended_mining_job{bad_channel,
            11, std::nullopt, 0x20000000, true, {}, {1}, {2}})},
        {mining_set_new_prev_hash::message_type, to_data_chunk(mining_set_new_prev_hash{bad_channel,
            11, target(), 1, 0x207fffff})},
        {close_channel::message_type, to_data_chunk(close_channel{bad_channel, "closed"})}};
    for (auto const& [type, payload] : messages) {
        auto session = ready();
        require_closed(session, process(session, message_header::channel_msg_flag, type, payload));
    }
    auto zero_target = ready();
    require_closed(zero_target, process(zero_target, set_target{7, {}}));
    auto missing_flag = ready();
    require_closed(missing_flag, process(missing_flag, 0, set_target::message_type, to_data_chunk(set_target{7, target()})));
    auto unsolicited = ready();
    require_closed(unsolicited, process(unsolicited, submit_shares_success{7, 0, 1, 1}));
    auto duplicate_open = ready();
    require_closed(duplicate_open, process(duplicate_open, opened()));
    auto jd = waiting(jd_peer::declaration);
    require_silent(process(jd, setup_connection_success{2, 0}));
    require_closed(jd, process(jd, 0, 0x51, {}));
}

TEST_CASE("sv2 jd ChannelEndpointChanged keeps the channel and refuses another or a misplaced one", "[sv2 jd]") {
    for (uint32_t const id : {7u, 9u}) {
        auto session = ready();
        require_silent(process(session, channel_endpoint_changed{id}));
        REQUIRE(session.channel()->channel_id == 7);
        REQUIRE(session.channel()->extranonce_prefix == data_chunk{1, 2});
    }
    refuse_incomplete(channel_endpoint_changed{7}, ready);
    auto another = ready();
    require_closed(another, process(another, channel_endpoint_changed{8}));
    auto missing_flag = ready();
    require_closed(missing_flag, process(missing_flag, 0, channel_endpoint_changed::message_type,
        to_data_chunk(channel_endpoint_changed{7})));
    // Before the channel is open there is nothing it could name.
    auto opening_session = opening();
    require_closed(opening_session, process(opening_session, channel_endpoint_changed{7}));
}

TEST_CASE("sv2 jd channel closes request solo fallback for the channel or its group", "[sv2 jd]") {
    for (uint32_t const id : {7u, 9u}) {
        auto session = ready();
        auto const result = process(session, close_channel{id, "maintenance"});
        require_closed(session, result);
        REQUIRE(result.error == "upstream closed extended channel");
    }
}

TEST_CASE("sv2 jd a refused channel update keeps the channel and other update errors close it", "[sv2 jd]") {
    // sv2-spec 5.3.8: the pool keeps the channel as it was, so it stays open
    // and still follows the pool's later messages.
    auto session = ready();
    require_silent(process(session, update_channel_error{7, "max-target-out-of-range"}));
    REQUIRE(session.channel()->channel_id == 7);
    auto updated = target();
    updated.back() = 0x7f;
    require_silent(process(session, set_target{7, updated}));
    REQUIRE(session.channel()->target == updated);

    // Only channel 7 is ever updated: its group 9 and other ids are not replies.
    for (uint32_t const id : {8u, 9u}) {
        CAPTURE(id);
        auto other = ready();
        auto const result = process(other, update_channel_error{id, "invalid-channel-id"});
        require_closed(other, result);
        REQUIRE(result.error == "invalid upstream channel update error");
    }
    refuse_incomplete(update_channel_error{7, "max-target-out-of-range"}, ready);
    auto missing_flag = ready();
    require_closed(missing_flag, process(missing_flag, 0, update_channel_error::message_type,
        to_data_chunk(update_channel_error{7, "max-target-out-of-range"})));
}

TEST_CASE("sv2 jd extensions are discarded only within the frame length bound", "[sv2 jd]") {
    auto setup = waiting();
    require_silent(process(setup, 0x4000, 0xff, {1, 2, 3}), false);
    auto channel = opening();
    require_silent(process(channel, 0xc000, 0x14, {1}), false);
    auto active = ready();
    require_silent(process(active, 0xc000, 0x21, {1}));
    auto jd = waiting(jd_peer::declaration);
    require_silent(process(jd, setup_connection_success{2, 0}));
    require_silent(process(jd, 0x4000, 0x51, {}));
    auto wrong_length = waiting();
    require_closed(wrong_length, wrong_length.process({0x4000, 0xff, 2}, data_chunk{1}));
    auto oversized = waiting();
    data_chunk const large(jd_session::max_payload_size + 1, 0);
    require_closed(oversized, process(oversized, 0x4000, 0xff, large));
    auto undefined = ready();
    require_closed(undefined, process(undefined, message_header::channel_msg_flag, 0xff, {}));
}

TEST_CASE("sv2 jd SetGroupChannel regroups this extended channel and ignores the rest", "[sv2 jd]") {
    auto session = ready();
    require_silent(process(session, set_group_channel{42, {1, 7}}));
    REQUIRE(session.ready());
    REQUIRE(session.channel()->channel_id == 7);
    REQUIRE(session.channel()->group_channel_id == 42);
    REQUIRE(session.channel()->extranonce_prefix == data_chunk{1, 2});

    require_silent(process(session, set_group_channel{99, {1, 2, 8}}));
    REQUIRE(session.channel()->group_channel_id == 42);
    require_silent(process(session, set_group_channel{100, {}}));
    REQUIRE(session.channel()->group_channel_id == 42);

    refuse_incomplete(set_group_channel{42, {7}}, ready);
    auto flagged = ready();
    require_closed(flagged, process(flagged, message_header::channel_msg_flag,
        set_group_channel::message_type, to_data_chunk(set_group_channel{42, {7}})));
}

TEST_CASE("sv2 jd Mining setup asks for worker tracking and opens the channel on any answer", "[sv2 jd]") {
    // Extension 0x0001, section 4: RequestExtensions follows SetupConnection.Success
    // and its answer comes before the channel request. Only a listed 0x0002 grants it.
    struct reply {
        bool success;
        std::vector<uint16_t> extensions;
        bool granted;
    };
    for (auto const& [success, extensions, granted] : std::vector<reply>{
            {true, {worker_hashrate_tracking_extension}, true},
            {true, {0x0003, worker_hashrate_tracking_extension}, true},
            {true, {0x0003}, false},
            {true, {}, false},
            {false, {worker_hashrate_tracking_extension}, false}}) {
        CAPTURE(success, extensions.size(), granted);
        auto session = asking();
        REQUIRE_FALSE(session.ready());
        auto const response = success
            ? process(session, request_extensions_success{1, extensions})
            : process(session, request_extensions_error{1, extensions, {}});
        REQUIRE_FALSE(response.close);
        REQUIRE_FALSE(response.ready);
        auto const request = single<open_extended_mining_channel>(response.response);
        REQUIRE(request.request_id == 1);
        REQUIRE(request.user_identity == "worker");
        REQUIRE(session.worker_identities() == granted);
        require_silent(process(session, opened()));
        REQUIRE(session.worker_identities() == granted);
    }
    // The Job Declaration connection never asks.
    auto jd = waiting(jd_peer::declaration);
    require_silent(process(jd, setup_connection_success{2, 0}));
    REQUIRE_FALSE(jd.awaiting_extensions());
    REQUIRE_FALSE(jd.worker_identities());
}

TEST_CASE("sv2 jd extension negotiation refuses misplaced, mismatched and required answers", "[sv2 jd]") {
    // A required extension this node lacks cannot be supplied on a retry.
    auto required = asking();
    auto const refused = process(required, request_extensions_error{1, worker_tracking(), {0x0005}});
    require_closed(required, refused);
    REQUIRE(refused.incompatible);
    REQUIRE(refused.error == "upstream requires an unsupported extension");

    auto wrong_id = asking();
    require_closed(wrong_id, process(wrong_id, request_extensions_success{2, worker_tracking()}));
    auto wrong_error_id = asking();
    require_closed(wrong_error_id, process(wrong_error_id, request_extensions_error{0, {}, {}}));
    // A base message before the answer is out of order, the channel reply too.
    auto early_channel = asking();
    require_closed(early_channel, process(early_channel, opened()));
    auto early_target = asking();
    require_closed(early_target, process(early_target, set_target{7, target()}));
    auto flagged = asking();
    require_closed(flagged, process(flagged,
        static_cast<uint16_t>(extensions_negotiation_type | message_header::channel_msg_flag),
        request_extensions_success::message_type, to_data_chunk(request_extensions_success{1, worker_tracking()})));
    auto client_message = asking();
    require_closed(client_message, process(client_message, request_extensions{1, worker_tracking()}));
    refuse_incomplete(request_extensions_success{1, worker_tracking()}, asking);
    refuse_incomplete(request_extensions_error{1, worker_tracking(), {}}, asking);

    // Another extension's frame is not the answer and does not end the wait.
    auto other = asking();
    require_silent(process(other, 0x4000, 0x00, {1}), false);
    REQUIRE(other.awaiting_extensions());
    REQUIRE(answer(other, true).request_id == 1);
}

TEST_CASE("sv2 jd a session told to skip extensions opens the channel straight after setup", "[sv2 jd]") {
    jd_session session(jd_peer::mining, "pool.example", 8442, "worker", true, true);
    session.skip_extensions();
    REQUIRE_FALSE(session.start().empty());
    auto const result = process(session, setup_connection_success{2, 0});
    REQUIRE_FALSE(result.close);
    REQUIRE_FALSE(session.awaiting_extensions());
    REQUIRE(single<open_extended_mining_channel>(result.response).user_identity == "worker");
    require_silent(process(session, opened()));
    REQUIRE_FALSE(session.worker_identities());
    // It never asked, so a moved channel has nothing to negotiate again.
    require_silent(process(session, channel_endpoint_changed{7}));
    // An unsolicited answer is discarded like any extension frame.
    require_silent(process(session, request_extensions_success{1, worker_tracking()}));
    REQUIRE_FALSE(session.worker_identities());
}

TEST_CASE("sv2 jd ChannelEndpointChanged clears worker tracking until the pool grants it again", "[sv2 jd]") {
    auto session = ready_granted();
    REQUIRE(session.worker_identities());
    // Nothing is outstanding, so an unsolicited answer changes nothing.
    require_silent(process(session, request_extensions_error{1, worker_tracking(), {}}));
    REQUIRE(session.worker_identities());

    auto const first = process(session, channel_endpoint_changed{7});
    REQUIRE(single<request_extensions>(first.response).request_id == 2);
    REQUIRE_FALSE(session.worker_identities());
    auto const second = process(session, channel_endpoint_changed{7});
    REQUIRE(single<request_extensions>(second.response).request_id == 3);
    // The answer to the request a later change replaced does not count.
    require_silent(process(session, request_extensions_success{2, worker_tracking()}));
    REQUIRE_FALSE(session.worker_identities());
    require_silent(process(session, request_extensions_success{3, worker_tracking()}));
    REQUIRE(session.worker_identities());
    REQUIRE(session.channel()->channel_id == 7);

    // A refusal leaves it off and mining goes on; a required extension ends the attempt.
    auto refused = ready_granted();
    (void)process(refused, channel_endpoint_changed{7});
    require_silent(process(refused, request_extensions_error{2, worker_tracking(), {}}));
    REQUIRE_FALSE(refused.worker_identities());
    REQUIRE(refused.ready());
    auto required = ready_granted();
    (void)process(required, channel_endpoint_changed{7});
    auto const closed = process(required, request_extensions_error{2, worker_tracking(), {0x0005}});
    require_closed(required, closed);
    REQUIRE(closed.incompatible);
}

TEST_CASE("sv2 jd an upstream that is not the operator's own never asks for worker names", "[sv2 jd]") {
    // The miner's side decides. An unmarked session opens its channel straight
    // after setup, and a grant it never asked for changes nothing.
    auto session = waiting();
    auto const result = process(session, setup_connection_success{2, 0});
    REQUIRE_FALSE(session.awaiting_extensions());
    REQUIRE(single<open_extended_mining_channel>(result.response).user_identity == "worker");
    require_silent(process(session, request_extensions_success{1, worker_tracking()}), false);
    require_silent(process(session, opened()));
    require_silent(process(session, request_extensions_success{1, worker_tracking()}));
    require_silent(process(session, request_extensions_error{1, worker_tracking(), {}}));
    REQUIRE_FALSE(session.worker_identities());
    require_silent(process(session, channel_endpoint_changed{7}));
    REQUIRE_FALSE(session.worker_identities());
}

TEST_CASE("sv2 jd a pool that requires worker names is incompatible with an unmarked upstream", "[sv2 jd]") {
    // Extension 0x0001, section 4: the pool disconnects a client without the
    // extensions it requires, and an unmarked entry never sends the names.
    for (bool const opened_channel : {false, true}) {
        CAPTURE(opened_channel);
        auto session = opened_channel ? ready() : opening();
        auto const refused = process(session, request_extensions_error{0, {}, worker_tracking()});
        require_closed(session, refused);
        REQUIRE(refused.incompatible);
        REQUIRE(refused.error == "upstream requires an extension this connection did not negotiate");
    }
    // A marked pair that connected again without asking is retried, and the
    // next pair asks.
    jd_session skipped(jd_peer::mining, "pool.example", 8442, "worker", true, true);
    skipped.skip_extensions();
    REQUIRE_FALSE(skipped.start().empty());
    (void)process(skipped, setup_connection_success{2, 0});
    auto const transient = process(skipped, request_extensions_error{0, {}, worker_tracking()});
    require_closed(skipped, transient);
    REQUIRE_FALSE(transient.incompatible);
    // The Job Declaration connection does not take part in this.
    auto jd = waiting(jd_peer::declaration);
    require_silent(process(jd, setup_connection_success{2, 0}));
    require_silent(process(jd, request_extensions_error{0, {}, worker_tracking()}));
}
