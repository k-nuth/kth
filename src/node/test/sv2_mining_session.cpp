// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <kth/domain/chain/compact.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/extensions.hpp>
#include <kth/node/sv2/mining_messages.hpp>
#include <kth/node/sv2/mining_session.hpp>
#include <kth/node/sv2/template_provider.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

template <typename Message>
constexpr uint16_t extension() {
    return framed_extension_type<Message>();
}

setup_connection setup(uint32_t flags = 0) {
    setup_connection message;
    message.protocol = setup_connection::protocol_mining;
    message.min_version = 2;
    message.max_version = 2;
    message.flags = flags;
    return message;
}

hash_digest easy_target() {
    hash_digest target;
    target.fill(0xff);
    return target;
}

open_standard_mining_channel standard(uint32_t request = 0) {
    return {request, "worker", 0.f, easy_target()};
}

open_extended_mining_channel extended(uint32_t request = 0) {
    return {request, "proxy", 0.f, easy_target(), 8};
}

blockchain::mining_template work() {
    return {0x20000000, null_hash, 1, 0x207fffff, 1, 1,
        50 * uint64_t{100000000}, 1000000, 100, {}};
}

mining_session::result process(mining_session& session, uint16_t extension_type,
    uint8_t type, data_chunk const& bytes) {
    return session.process({extension_type, type, static_cast<uint32_t>(bytes.size())}, bytes);
}

template <typename Message>
mining_session::result process(mining_session& session, Message const& message) {
    auto result = process(session, extension<Message>(), Message::message_type, to_data_chunk(message));
    // Supply a template the way the listener does after an open request. Tests
    // of a temporarily unavailable template use the raw overload above.
    if (result.wants_template) {
        auto const jobs = session.apply(work());
        if (jobs.close) return jobs;
        result.response.insert(result.response.end(), jobs.response.begin(), jobs.response.end());
    }
    return result;
}

struct packet {
    message_header header;
    data_chunk payload;
};

std::vector<packet> packets(data_chunk const& bytes) {
    std::vector<packet> out;
    byte_reader reader(bytes);
    while ( ! reader.is_exhausted()) {
        auto const header = message_header::from_data(reader);
        REQUIRE(header.has_value());
        auto const payload = reader.read_bytes(header->msg_length);
        REQUIRE(payload.has_value());
        out.push_back({*header, data_chunk(payload->begin(), payload->end())});
    }
    return out;
}

template <typename Message>
Message parse(packet const& value) {
    REQUIRE(value.header.extension_type == extension<Message>());
    REQUIRE(value.header.msg_type == Message::message_type);
    byte_reader reader(value.payload);
    auto const message = Message::from_data(reader);
    REQUIRE(message.has_value());
    REQUIRE(reader.is_exhausted());
    return *message;
}

template <typename Message>
Message single(mining_session::result const& value) {
    auto const frames = packets(value.response);
    if constexpr (std::is_same_v<Message, open_standard_mining_channel_success>
        || std::is_same_v<Message, open_extended_mining_channel_success>) {
        REQUIRE(frames.size() == 3);
    } else {
        REQUIRE(frames.size() == 1);
    }
    return parse<Message>(frames.front());
}

mining_session ready(uint32_t flags = 0) {
    mining_session session(1, domain::chain::script(data_chunk{0x51}, false));
    REQUIRE_FALSE(process(session, setup(flags)).close);
    return session;
}

void require_closed(mining_session::result const& value) {
    REQUIRE(value.close);
    REQUIRE(value.response.empty());
    REQUIRE_FALSE(value.solution);
}

void require_silent(mining_session::result const& value) {
    REQUIRE_FALSE(value.close);
    REQUIRE(value.response.empty());
    REQUIRE_FALSE(value.solution);
}

// Every truncated field must be refused, including a length prefix whose
// bytes arrive before its data. A short writer must report failure.
template <typename Message>
void check_wire(Message const& message) {
    auto const bytes = to_data_chunk(message);
    REQUIRE(bytes.size() == message.serialized_size());
    byte_reader reader(bytes);
    auto const decoded = Message::from_data(reader);
    REQUIRE(decoded.has_value());
    REQUIRE(reader.is_exhausted());
    REQUIRE(to_data_chunk(*decoded) == bytes);
    for (size_t size = 0; size < bytes.size(); ++size) {
        CAPTURE(Message::message_type, size);
        byte_reader short_reader(byte_span(bytes).first(size));
        REQUIRE_FALSE(Message::from_data(short_reader).has_value());
        data_chunk short_bytes(size);
        byte_writer writer(short_bytes);
        REQUIRE_FALSE(message.to_data(writer).has_value());
    }
}

} // namespace

TEST_CASE("sv2 mining messages preserve their fields and refuse every truncated field", "[sv2 mining]") {
    auto const target = easy_target();
    check_wire(standard(0x12345678));
    check_wire(open_standard_mining_channel_success{1, 2, target, {3, 4}, 5});
    check_wire(open_mining_channel_error{1, "too-many-channels"});
    check_wire(extended(9));
    check_wire(open_extended_mining_channel_success{1, 2, target, 8, {3, 4}, 5});
    check_wire(new_mining_job{1, 2, std::nullopt, 3, target});
    check_wire(new_mining_job{1, 2, 4, 3, target});
    check_wire(update_channel{1, 123.5f, target});
    check_wire(update_channel_error{1, "invalid-channel-id"});
    check_wire(close_channel{1, "finished"});
    check_wire(set_extranonce_prefix{1, {2, 3}});
    check_wire(submit_shares_standard{1, 2, 3, 4, 5, 6});
    check_wire(submit_shares_extended{1, 2, 3, 4, 5, 6, {7, 8}});
    check_wire(submit_shares_success{1, 2, 3, 0x0102030405060708ULL});
    check_wire(submit_shares_error{1, 2, "stale-share"});
    check_wire(new_extended_mining_job{1, 2, std::nullopt, 3, true,
        {target}, {4, 5}, {6, 7}});
    check_wire(new_extended_mining_job{1, 2, 4, 3, false, {}, {}, {}});
    check_wire(mining_set_new_prev_hash{1, 2, target, 3, 4});
    check_wire(set_target{1, target});
}

TEST_CASE("sv2 mining wire layout uses little endian fields and bounded extranonces", "[sv2 mining]") {
    open_standard_mining_channel message{0x04030201, "a", 1.5f, {}};
    data_chunk expected{1, 2, 3, 4, 1, 'a', 0, 0, 0xc0, 0x3f};
    expected.resize(expected.size() + hash_size, 0);
    REQUIRE(to_data_chunk(message) == expected);

    submit_shares_extended share{1, 2, 3, 4, 5, 6, {7, 8}};
    REQUIRE(to_data_chunk(share) == data_chunk{
        1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0,
        5, 0, 0, 0, 6, 0, 0, 0, 2, 7, 8});

    auto const accepts_padding = [&](uint8_t byte, bool expected_value) {
        new_extended_mining_job job;
        auto bytes = to_data_chunk(job);
        bytes[13] = byte;
        auto const decoded = from_data_chunk<new_extended_mining_job>(bytes);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->version_rolling_allowed == expected_value);
    };
    accepts_padding(0xfe, false);
    accepts_padding(0xff, true);

    new_mining_job job;
    auto bytes = to_data_chunk(job);
    bytes[8] = 2;
    REQUIRE_FALSE(from_data_chunk<new_mining_job>(bytes).has_value());
    new_extended_mining_job extended_job;
    bytes = to_data_chunk(extended_job);
    bytes[8] = 2;
    REQUIRE_FALSE(from_data_chunk<new_extended_mining_job>(bytes).has_value());

    // B0_32 must stay bounded even when all bytes behind the length arrive.
    share.extranonce.resize(32, 7);
    check_wire(share);
    bytes = to_data_chunk(share);
    bytes[24] = 33;
    bytes.push_back(7);
    REQUIRE_FALSE(from_data_chunk<submit_shares_extended>(bytes).has_value());

    auto refuses_encoding = [](auto const& oversized) {
        data_chunk buffer(oversized.serialized_size());
        byte_writer writer(buffer);
        REQUIRE_FALSE(oversized.to_data(writer).has_value());
    };
    share.extranonce.push_back(7);
    refuses_encoding(share);
    message.user_identity.resize(256, 'a');
    refuses_encoding(message);
    extended_job.merkle_path.resize(256);
    refuses_encoding(extended_job);
    extended_job.merkle_path.clear();
    extended_job.coinbase_tx_prefix.resize(65536);
    refuses_encoding(extended_job);
    extended_job.coinbase_tx_prefix.clear();
    extended_job.coinbase_tx_suffix.resize(65536);
    refuses_encoding(extended_job);
}

TEST_CASE("sv2 mining setup negotiates version and required features", "[sv2 mining]") {
    for (uint32_t const flags : {0u, mining_flag_requires_standard_jobs,
            mining_flag_requires_version_rolling, mining_supported_client_flags}) {
        mining_session session(1, domain::chain::script{});
        auto message = setup(flags);
        message.min_version = 1;
        message.max_version = 3;
        auto const result = process(session, message);
        REQUIRE_FALSE(result.close);
        auto const success = single<setup_connection_success>(result);
        REQUIRE(success.used_version == 2);
        REQUIRE(success.flags == 0);
        REQUIRE(session.ready());
        REQUIRE_FALSE(session.needs_template());
    }

    for (auto const versions : {std::pair<uint16_t, uint16_t>{1, 1},
            std::pair<uint16_t, uint16_t>{3, 4}, std::pair<uint16_t, uint16_t>{3, 1}}) {
        mining_session session(1, domain::chain::script{});
        auto message = setup();
        message.min_version = versions.first;
        message.max_version = versions.second;
        auto const result = process(session, message);
        REQUIRE(result.close);
        auto const error = single<setup_connection_error>(result);
        REQUIRE(error.error_code == "protocol-version-mismatch");
        REQUIRE(error.flags == 0);
        require_closed(process(session, setup()));
    }

    mining_session session(1, domain::chain::script{});
    auto const result = process(session, setup(mining_supported_client_flags | 0x80000002u));
    REQUIRE(result.close);
    auto const error = single<setup_connection_error>(result);
    REQUIRE(error.error_code == "unsupported-feature-flags");
    REQUIRE(error.flags == 0x80000002u);

    mining_session wrong_version(1, domain::chain::script{});
    auto message = setup(mining_supported_client_flags | 0x80000002u);
    message.min_version = 3;
    message.max_version = 4;
    auto const refused = process(wrong_version, message);
    REQUIRE(refused.close);
    auto const version_error = single<setup_connection_error>(refused);
    REQUIRE(version_error.error_code == "protocol-version-mismatch");
    REQUIRE(version_error.flags == 0x80000002u);
}

TEST_CASE("sv2 mining keeps setup framing and message directions strict", "[sv2 mining]") {
    for (uint16_t const ext : {uint16_t{1}, uint16_t{0x8001}, message_header::channel_msg_flag}) {
        mining_session session(1, domain::chain::script{});
        require_closed(process(session, ext, setup_connection::message_type, to_data_chunk(setup())));
    }
    {
        mining_session session(1, domain::chain::script{});
        require_closed(process(session, standard()));
    }
    {
        mining_session session(1, domain::chain::script{});
        require_closed(process(session, 0, setup_connection::message_type, {}));
    }
    {
        mining_session session(1, domain::chain::script{});
        auto message = setup();
        message.protocol = setup_connection::protocol_job_declaration;
        require_closed(process(session, message));
    }
    {
        mining_session session(1, domain::chain::script{});
        require_closed(session.apply(work()));
    }
    {
        auto session = ready();
        require_closed(session.process({0, 0xff, 1}, {}));
        require_closed(process(session, setup()));
    }
    {
        auto session = ready();
        require_closed(session.process({0, 0xff, template_provider_session::max_payload_size + 1}, {}));
    }
    {
        auto session = ready();
        require_silent(process(session, 1, 0xff, {0, 1, 2}));
        require_silent(process(session, 0x8001, 0xff, {}));
        REQUIRE_FALSE(process(session, standard()).close);
    }

    auto rejects = [](auto const& message) {
        auto const bytes = to_data_chunk(message);
        for (int malformed = 0; malformed < 3; ++malformed) {
            auto session = ready();
            auto bad = bytes;
            if (malformed == 0) bad.pop_back();
            if (malformed == 1) bad.push_back(0);
            auto const ext = static_cast<uint16_t>(malformed == 2
                ? extension<std::remove_cvref_t<decltype(message)>>() ^ message_header::channel_msg_flag
                : extension<std::remove_cvref_t<decltype(message)>>());
            require_closed(process(session, ext, message.message_type, bad));
        }
    };
    rejects(standard());
    rejects(extended());
    rejects(update_channel{});
    rejects(close_channel{1, "done"});
    rejects(submit_shares_standard{});
    rejects(submit_shares_extended{});

    for (uint8_t const type : {uint8_t{0x01}, uint8_t{0x11}, uint8_t{0x14},
            uint8_t{0x15}, uint8_t{0x17}, uint8_t{0x19}, uint8_t{0x1c}, uint8_t{0x1d},
            uint8_t{0x1e}, uint8_t{0x1f}, uint8_t{0x20}, uint8_t{0x21}, uint8_t{0x22}, uint8_t{0xff}}) {
        auto session = ready();
        require_closed(process(session, 0, type, {}));
    }
    auto session = ready();
    require_closed(process(session, close_channel{1, std::string{"bad\0reason", 10}}));
}

TEST_CASE("sv2 mining opens closes and bounds independent channels", "[sv2 mining]") {
    auto session = ready();
    auto const first = process(session, standard());
    REQUIRE(first.wants_template);
    auto const opened = single<open_standard_mining_channel_success>(first);
    REQUIRE(opened.target == easy_target());
    REQUIRE(opened.channel_id != opened.group_channel_id);
    REQUIRE_FALSE(session.needs_template());
    auto const other = single<open_extended_mining_channel_success>(process(session, extended(1)));
    REQUIRE(other.channel_id != opened.channel_id);
    REQUIRE(other.extranonce_prefix != opened.extranonce_prefix);
    REQUIRE(other.extranonce_size >= 8);
    REQUIRE(other.extranonce_size <= 32);
    REQUIRE(other.group_channel_id != other.channel_id);

    REQUIRE(single<open_mining_channel_error>(process(session, standard())).error_code == "duplicate-request-id");
    auto too_large = extended(2);
    too_large.min_extranonce_size = 33;
    REQUIRE(single<open_mining_channel_error>(process(session, too_large)).error_code == "min-extranonce-size-too-large");

    for (uint32_t id = 3; id < node::mining::max_mining_channels + 1; ++id) {
        REQUIRE_FALSE(process(session, standard(id)).close);
    }
    REQUIRE(single<open_mining_channel_error>(process(session, standard(100))).error_code == "too-many-channels");
    require_silent(process(session, close_channel{other.group_channel_id, "done"}));
    REQUIRE_FALSE(process(session, standard(101)).close);
    require_silent(process(session, close_channel{opened.channel_id, "done"}));
    require_silent(process(session, close_channel{opened.channel_id, "already-closed"}));

    auto standard_only = ready(mining_flag_requires_standard_jobs);
    REQUIRE(single<open_mining_channel_error>(process(standard_only, extended())).error_code == "requires-standard-jobs");
    REQUIRE_FALSE(process(standard_only, standard(1)).close);
}

TEST_CASE("sv2 mining awaits a first channel only until one opens", "[sv2 mining]") {
    mining_session fresh(1, domain::chain::script{});
    REQUIRE_FALSE(fresh.awaiting_channel());
    auto session = ready();
    REQUIRE(session.awaiting_channel());
    auto too_large = extended();
    too_large.min_extranonce_size = 33;
    REQUIRE(single<open_mining_channel_error>(process(session, too_large)).error_code == "min-extranonce-size-too-large");
    REQUIRE(session.awaiting_channel());
    auto const opened = single<open_standard_mining_channel_success>(process(session, standard(1)));
    REQUIRE_FALSE(session.awaiting_channel());
    require_silent(process(session, close_channel{opened.channel_id, "done"}));
    REQUIRE_FALSE(session.awaiting_channel());

    // A channel whose success waits for a template has still been opened.
    auto pending = ready();
    require_silent(process(pending, 0, open_standard_mining_channel::message_type,
        to_data_chunk(standard())));
    REQUIRE_FALSE(pending.awaiting_channel());
}

TEST_CASE("sv2 mining refuses invalid rates and updates the target within the device bound", "[sv2 mining]") {
    for (float const rate : {-0.f, -1.f, std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        auto session = ready();
        auto message = standard();
        message.nominal_hash_rate = rate;
        require_closed(process(session, message));
        auto proxy = ready();
        auto extended_message = extended();
        extended_message.nominal_hash_rate = rate;
        require_closed(process(proxy, extended_message));
        auto update = ready();
        require_closed(process(update, update_channel{1, rate, easy_target()}));
    }
    auto session = ready();
    REQUIRE(single<update_channel_error>(process(session, update_channel{1, 0, easy_target()})).error_code == "invalid-channel-id");
    auto const opened = single<open_standard_mining_channel_success>(process(session, standard()));
    require_silent(process(session, update_channel{opened.channel_id, 0, easy_target()}));
    auto bound = easy_target();
    bound.back() = 0x7f;
    auto const target = single<set_target>(process(session, update_channel{opened.channel_id, 0, bound}));
    REQUIRE(target.channel_id == opened.channel_id);
    REQUIRE(target.target == bound);
}

TEST_CASE("sv2 mining SetTarget applies to the future job already being mined", "[sv2 mining]") {
    auto session = ready();
    auto const frames = packets(process(session, standard()).response);
    REQUIRE(frames.size() == 3);
    auto const opened = parse<open_standard_mining_channel_success>(frames[0]);
    auto const job = parse<new_mining_job>(frames[1]);
    auto const prev = parse<mining_set_new_prev_hash>(frames[2]);
    REQUIRE_FALSE(job.ntime_start);
    submit_shares_standard share{opened.channel_id, 1, job.job_id, 0, prev.ntime_start, job.version};
    domain::chain::header const header(job.version, prev.prev_hash, job.merkle_root,
        prev.ntime_start, prev.nbits, share.nonce);
    auto const digest = to_uint256(domain::chain::hash(header));
    REQUIRE(digest > 0);

    // Harder: a share that meets the opening target but not the new one is refused.
    auto const below = to_hash_digest(digest - 1);
    REQUIRE(single<set_target>(process(session, update_channel{opened.channel_id, 0, below})).target == below);
    REQUIRE(single<submit_shares_error>(process(session, share)).error_code == "difficulty-too-low");

    // Easier: the same share meets the new target but not the previous one.
    auto const exact = to_hash_digest(digest);
    REQUIRE(single<set_target>(process(session, update_channel{opened.channel_id, 0, exact})).target == exact);
    ++share.sequence_number;
    auto const accepted = single<submit_shares_success>(process(session, share));
    REQUIRE(accepted.last_sequence_number == 2);
    REQUIRE(accepted.new_submits_accepted_count == 1);
}

TEST_CASE("sv2 mining sends a future job before activation on every channel and stales old work", "[sv2 mining]") {
    auto session = ready();
    auto const first = single<open_standard_mining_channel_success>(process(session, standard()));
    auto const second = single<open_extended_mining_channel_success>(process(session, extended(1)));
    auto const result = session.apply(work());
    REQUIRE_FALSE(result.close);
    REQUIRE_FALSE(session.needs_template());
    auto frames = packets(result.response);
    REQUIRE(frames.size() == 4);
    auto const standard_job = parse<new_mining_job>(frames[0]);
    auto const standard_prev = parse<mining_set_new_prev_hash>(frames[1]);
    auto const extended_job = parse<new_extended_mining_job>(frames[2]);
    auto const extended_prev = parse<mining_set_new_prev_hash>(frames[3]);
    REQUIRE_FALSE(standard_job.ntime_start);
    REQUIRE_FALSE(extended_job.ntime_start);
    REQUIRE(extended_job.version_rolling_allowed);
    REQUIRE(standard_job.channel_id == first.channel_id);
    REQUIRE(standard_prev.job_id == standard_job.job_id);
    REQUIRE(extended_job.channel_id == second.channel_id);
    REQUIRE(extended_prev.job_id == extended_job.job_id);
    REQUIRE(standard_prev.prev_hash == work().previous_block_hash);
    REQUIRE(standard_prev.ntime_start >= work().min_time);
    REQUIRE(standard_prev.nbits == work().bits);

    // An additional channel gets its own first job from the cached template.
    frames = packets(process(session, standard(2)).response);
    REQUIRE(frames.size() == 3);
    auto const additional = parse<open_standard_mining_channel_success>(frames[0]);
    auto const additional_job = parse<new_mining_job>(frames[1]);
    auto const additional_prev = parse<mining_set_new_prev_hash>(frames[2]);
    REQUIRE(additional_job.channel_id == additional.channel_id);
    REQUIRE_FALSE(additional_job.ntime_start);
    REQUIRE(additional_prev.job_id == additional_job.job_id);

    session.note_tip(work().previous_block_hash);
    REQUIRE_FALSE(session.needs_template());
    hash_digest next = null_hash;
    next[0] = 1;
    session.note_tip(next);
    REQUIRE(session.needs_template());
    submit_shares_standard share{first.channel_id, 1, standard_job.job_id,
        0, standard_prev.ntime_start, standard_job.version};
    REQUIRE(single<submit_shares_error>(process(session, share)).error_code == "stale-share");
    auto next_work = work();
    next_work.previous_block_hash = next;
    next_work.height = 2;
    frames = packets(session.apply(next_work).response);
    REQUIRE(frames.size() == 6);
    for (size_t index : {size_t{1}, size_t{3}, size_t{5}}) {
        REQUIRE(parse<mining_set_new_prev_hash>(frames[index]).prev_hash == next);
    }
    REQUIRE(single<submit_shares_error>(process(session, share)).error_code == "stale-share");

    require_silent(process(session, close_channel{second.channel_id, "done"}));
    REQUIRE(packets(session.apply(next_work).response).size() == 4);
}

TEST_CASE("sv2 mining answers standard and extended share results without closing other channels", "[sv2 mining]") {
    auto session = ready();
    auto const first = single<open_standard_mining_channel_success>(process(session, standard()));
    auto const second = single<open_extended_mining_channel_success>(process(session, extended(1)));
    auto const frames = packets(session.apply(work()).response);
    auto const job = parse<new_mining_job>(frames[0]);
    auto const prev = parse<mining_set_new_prev_hash>(frames[1]);
    auto const ext_job = parse<new_extended_mining_job>(frames[2]);
    auto const ext_prev = parse<mining_set_new_prev_hash>(frames[3]);

    submit_shares_standard share{first.channel_id, 7, job.job_id, 0, prev.ntime_start, job.version};
    auto const success = single<submit_shares_success>(process(session, share));
    REQUIRE(success.channel_id == first.channel_id);
    REQUIRE(success.last_sequence_number == 7);
    REQUIRE(success.new_submits_accepted_count == 1);
    auto const duplicate = single<submit_shares_error>(process(session, share));
    REQUIRE(duplicate.sequence_number == 7);
    REQUIRE(duplicate.error_code == "duplicate-share");

    submit_shares_extended ext_share{second.channel_id, 8, ext_job.job_id, 0,
        ext_prev.ntime_start, ext_job.version, data_chunk(second.extranonce_size, 0)};
    REQUIRE(single<submit_shares_success>(process(session, ext_share)).last_sequence_number == 8);
    ext_share.extranonce.pop_back();
    REQUIRE(single<submit_shares_error>(process(session, ext_share)).error_code == "invalid-extranonce-size");
    REQUIRE(session.ready());
}

TEST_CASE("sv2 mining renews exhausted duplicate history without waiting for a new tip", "[sv2 mining]") {
    auto session = ready();
    auto frames = packets(process(session, standard()).response);
    auto const opened = parse<open_standard_mining_channel_success>(frames[0]);
    auto const old_job = parse<new_mining_job>(frames[1]);
    auto const old_prev = parse<mining_set_new_prev_hash>(frames[2]);
    frames = packets(process(session, extended(1)).response);
    auto const other = parse<open_extended_mining_channel_success>(frames[0]);
    auto const other_job = parse<new_extended_mining_job>(frames[1]);
    auto const other_prev = parse<mining_set_new_prev_hash>(frames[2]);

    submit_shares_standard share{opened.channel_id, 0, old_job.job_id,
        0, old_prev.ntime_start, old_job.version};
    for (uint32_t nonce = 0; nonce + 1 < node::mining::max_remembered_shares; ++nonce) {
        share.nonce = nonce;
        share.sequence_number = nonce;
        REQUIRE(single<submit_shares_success>(process(session, share)).new_submits_accepted_count == 1);
    }
    ++share.sequence_number;
    auto const target = domain::chain::compact::from_compact(old_prev.nbits);
    REQUIRE(target.has_value());
    domain::chain::header solved;
    bool found = false;
    for (uint32_t nonce = node::mining::max_remembered_shares - 1; nonce < 10000; ++nonce) {
        solved = domain::chain::header(old_job.version, old_prev.prev_hash,
            old_job.merkle_root, old_prev.ntime_start, old_prev.nbits, nonce);
        if (to_uint256(domain::chain::hash(solved)) <= target->big()) {
            share.nonce = nonce;
            found = true;
            break;
        }
    }
    REQUIRE(found);
    auto const renewed = process(session, share);
    REQUIRE_FALSE(renewed.close);
    REQUIRE(renewed.solution);
    REQUIRE(renewed.solution->header() == solved);
    REQUIRE(renewed.solution->hash() == domain::chain::hash(solved));
    frames = packets(renewed.response);
    REQUIRE(frames.size() == 3);
    REQUIRE(parse<submit_shares_success>(frames[0]).new_submits_accepted_count == 1);
    auto const replacement = parse<new_mining_job>(frames[1]);
    auto const activation = parse<mining_set_new_prev_hash>(frames[2]);
    REQUIRE(replacement.channel_id == opened.channel_id);
    REQUIRE(replacement.job_id != old_job.job_id);
    REQUIRE(replacement.merkle_root != old_job.merkle_root);
    REQUIRE_FALSE(replacement.ntime_start);
    REQUIRE(activation.job_id == replacement.job_id);
    REQUIRE(activation.prev_hash == old_prev.prev_hash);

    // The replaced job is still work on this tip. A repeat is a duplicate,
    // a plain share finds its history full, and another block is still taken.
    REQUIRE(single<submit_shares_error>(process(session, share)).error_code == "duplicate-share");
    auto late = share;
    bool late_block = false;
    bool late_share = false;
    for (uint32_t nonce = share.nonce + 1; nonce < 20000 && ! (late_block && late_share); ++nonce) {
        late.nonce = nonce;
        domain::chain::header const header(old_job.version, old_prev.prev_hash,
            old_job.merkle_root, old_prev.ntime_start, old_prev.nbits, nonce);
        bool const block = to_uint256(domain::chain::hash(header)) <= target->big();
        if (block && ! late_block) {
            auto const taken = process(session, late);
            REQUIRE(taken.solution);
            REQUIRE(taken.solution->header() == header);
            REQUIRE(single<submit_shares_success>(taken).new_submits_accepted_count == 1);
            late_block = true;
        } else if ( ! block && ! late_share) {
            REQUIRE(single<submit_shares_error>(process(session, late)).error_code == "share-limit");
            late_share = true;
        }
    }
    REQUIRE(late_block);
    REQUIRE(late_share);

    share.job_id = replacement.job_id;
    share.nonce = 0;
    share.ntime = activation.ntime_start;
    share.version = replacement.version;
    REQUIRE(single<submit_shares_success>(process(session, share)).new_submits_accepted_count == 1);
    submit_shares_extended unaffected{other.channel_id, 1, other_job.job_id, 0,
        other_prev.ntime_start, other_job.version, data_chunk(other.extranonce_size, 0)};
    REQUIRE(single<submit_shares_success>(process(session, unaffected)).new_submits_accepted_count == 1);
}

TEST_CASE("sv2 mining closes rather than leaving channels without a representable job", "[sv2 mining]") {
    SECTION("template cannot be represented") {
        auto session = ready();
        REQUIRE_FALSE(process(session, standard()).close);
        auto invalid = work();
        invalid.bits = 0x23010000;
        require_closed(session.apply(invalid));
    }
    SECTION("new extended channel cannot fit the cached template") {
        auto session = ready();
        REQUIRE_FALSE(process(session, standard()).close);
        auto small = work();
        // A 100-byte standard coinbase plus header/count fits; the maximum
        // extended extranonce needs a larger coinbase.
        small.size_limit = 185;
        REQUIRE_FALSE(session.apply(small).close);
        auto message = extended(1);
        message.min_extranonce_size = 32;
        require_closed(process(session, message));
    }
}

TEST_CASE("sv2 mining holds channel success until its first job is available", "[sv2 mining]") {
    auto session = ready();
    auto const first = process(session, 0, open_standard_mining_channel::message_type,
        to_data_chunk(standard()));
    auto const second = process(session, 0, open_extended_mining_channel::message_type,
        to_data_chunk(extended(1)));
    require_silent(first);
    require_silent(second);
    REQUIRE(first.wants_template);
    REQUIRE(second.wants_template);
    REQUIRE(session.needs_template());
    auto const frames = packets(session.apply(work()).response);
    REQUIRE(frames.size() == 6);
    auto const opened = parse<open_standard_mining_channel_success>(frames[0]);
    auto const job = parse<new_mining_job>(frames[1]);
    REQUIRE(job.channel_id == opened.channel_id);
    REQUIRE_FALSE(job.ntime_start);
    REQUIRE(parse<mining_set_new_prev_hash>(frames[2]).job_id == job.job_id);
    auto const ext_opened = parse<open_extended_mining_channel_success>(frames[3]);
    auto const ext_job = parse<new_extended_mining_job>(frames[4]);
    REQUIRE(ext_job.channel_id == ext_opened.channel_id);
    REQUIRE_FALSE(ext_job.ntime_start);
    REQUIRE(parse<mining_set_new_prev_hash>(frames[5]).job_id == ext_job.job_id);

    // Even a peer that guesses the internal id before success cannot retarget
    // a pending channel and make its first job disagree with Open.Success.
    auto premature = ready();
    require_silent(process(premature, 0, open_standard_mining_channel::message_type,
        to_data_chunk(standard())));
    require_closed(process(premature, update_channel{1, 0, easy_target()}));

    for (uint32_t const id : {1u, 0x80000001u}) {
        auto closed = ready();
        require_silent(process(closed, 0, open_standard_mining_channel::message_type,
            to_data_chunk(standard())));
        require_silent(process(closed, close_channel{id, "done"}));
        require_silent(closed.apply(work()));
        REQUIRE_FALSE(closed.needs_template());
        auto const reopened = single<open_standard_mining_channel_success>(process(closed, standard(1)));
        REQUIRE(reopened.channel_id != 1);
    }
}

TEST_CASE("sv2 mining grants worker tracking only to a RequestExtensions right after setup", "[sv2 mining]") {
    // Extension 0x0001, section 4: Success lists what is supported, and Error
    // answers a request for nothing this server supports. It requires none.
    auto session = ready();
    auto const granted = single<request_extensions_success>(process(session,
        request_extensions{5, {0x0003, worker_hashrate_tracking_extension}}));
    REQUIRE(granted.request_id == 5);
    REQUIRE(granted.supported_extensions == std::vector<uint16_t>{worker_hashrate_tracking_extension});
    // Only the first message after setup is a negotiation. Later ones are
    // discarded like any other extension frame.
    require_silent(process(session, request_extensions{6, {worker_hashrate_tracking_extension}}));
    REQUIRE(session.ready());

    for (auto const& requested : {std::vector<uint16_t>{0x0003}, std::vector<uint16_t>{}}) {
        CAPTURE(requested.size());
        auto refusing = ready();
        auto const refused = single<request_extensions_error>(process(refusing, request_extensions{7, requested}));
        REQUIRE(refused.request_id == 7);
        REQUIRE(refused.unsupported_extensions == requested);
        REQUIRE(refused.required_extensions.empty());
        REQUIRE(refusing.ready());
    }

    auto late = ready();
    REQUIRE_FALSE(process(late, standard()).close);
    require_silent(process(late, request_extensions{8, {worker_hashrate_tracking_extension}}));
    auto flagged = ready();
    require_silent(process(flagged, static_cast<uint16_t>(extensions_negotiation_type | message_header::channel_msg_flag),
        request_extensions::message_type, to_data_chunk(request_extensions{9, {worker_hashrate_tracking_extension}})));
    auto malformed = ready();
    require_closed(process(malformed, extensions_negotiation_type, request_extensions::message_type, {0x01}));
}

TEST_CASE("sv2 mining reads the worker TLV once granted and ignores other fields", "[sv2 mining]") {
    auto session = ready();
    (void)single<request_extensions_success>(process(session,
        request_extensions{1, {worker_hashrate_tracking_extension}}));
    auto const frames = packets(process(session, extended(1)).response);
    REQUIRE(frames.size() == 3);
    auto const opened = parse<open_extended_mining_channel_success>(frames[0]);
    auto const job = parse<new_extended_mining_job>(frames[1]);
    auto const prev = parse<mining_set_new_prev_hash>(frames[2]);
    uint32_t sequence = 0;
    auto const submit = [&](data_chunk const& trailing) {
        ++sequence;
        submit_shares_extended const share{opened.channel_id, sequence, job.job_id, sequence,
            prev.ntime_start, job.version, data_chunk(opened.extranonce_size, 0)};
        auto bytes = to_data_chunk(share);
        bytes.insert(bytes.end(), trailing.begin(), trailing.end());
        return process(session, extension<submit_shares_extended>(), submit_shares_extended::message_type, bytes);
    };
    auto const fields = [](std::vector<tlv_field> const& list) {
        data_chunk bytes;
        for (auto const& field : list) {
            auto const encoded = to_data_chunk(field);
            bytes.insert(bytes.end(), encoded.begin(), encoded.end());
        }
        return bytes;
    };
    // The share is submitted before its sequence number is compared.
    auto const accepted = [&](mining_session::result const& result) {
        REQUIRE(single<submit_shares_success>(result).last_sequence_number == sequence);
    };

    // Extension 0x0002 section 1.3: other fields, including unknown fields
    // of this extension, are ignored, and a share without the field is taken.
    accepted(submit(fields({tlv_field{0x0003, 0x01, {1, 2}}, user_identity_tlv("rig-7"),
        tlv_field{worker_hashrate_tracking_extension, 0x02, {9}}})));
    accepted(submit({}));
    accepted(submit(fields({user_identity_tlv(std::string(32, 'w'))})));

    // sv2-spec 3.4.3: a field over its 32-byte maximum rejects the message.
    tlv_field const long_name{worker_hashrate_tracking_extension, user_identity_field, data_chunk(33, 'w')};
    auto const refused = single<submit_shares_error>(submit(fields({long_name})));
    REQUIRE(refused.sequence_number == sequence);
    REQUIRE(refused.error_code == error_user_identity_too_long);
    REQUIRE(session.ready());

    // A field cut short is a malformed frame.
    require_closed(submit(data_chunk{0x02, 0x00, 0x01}));
}

TEST_CASE("sv2 mining ignores whole TLV fields after an extended share and closes on malformed bytes", "[sv2 mining]") {
    // The user_identity field 0x0002|0x01 with "rig-7", written out byte by byte.
    data_chunk const field{0x02, 0x00, 0x01, 0x05, 0x00, 'r', 'i', 'g', '-', '7'};
    // Bytes that are not whole TLV fields: one stray byte, a header cut short,
    // and a length that runs past the payload.
    std::vector<data_chunk> const malformed{{0x00}, {0x02, 0x00, 0x01},
        {0x02, 0x00, 0x01, 0x05, 0x00, 'r', 'i'}};
    struct shares {
        submit_shares_standard standard;
        submit_shares_extended extended;
    };
    auto const open_channels = [](mining_session& session, int negotiation) {
        if (negotiation == 1) (void)process(session, request_extensions{1, {0x0003}});
        if (negotiation == 2) (void)process(session, request_extensions{1, {worker_hashrate_tracking_extension}});
        auto frames = packets(process(session, standard()).response);
        auto const standard_opened = parse<open_standard_mining_channel_success>(frames[0]);
        auto const standard_job = parse<new_mining_job>(frames[1]);
        auto const standard_prev = parse<mining_set_new_prev_hash>(frames[2]);
        frames = packets(process(session, extended(1)).response);
        auto const opened = parse<open_extended_mining_channel_success>(frames[0]);
        auto const job = parse<new_extended_mining_job>(frames[1]);
        auto const prev = parse<mining_set_new_prev_hash>(frames[2]);
        return shares{{standard_opened.channel_id, 1, standard_job.job_id, 0,
            standard_prev.ntime_start, standard_job.version},
            {opened.channel_id, 2, job.job_id, 0, prev.ntime_start, job.version,
                data_chunk(opened.extranonce_size, 0)}};
    };
    auto const followed = [](auto const& message, data_chunk const& trailing) {
        auto bytes = to_data_chunk(message);
        bytes.insert(bytes.end(), trailing.begin(), trailing.end());
        return bytes;
    };
    // No request, a refused one, and a granted one.
    for (int const negotiation : {0, 1, 2}) {
        CAPTURE(negotiation);
        // Extension 0x0002 section 1.3: a server ignores TLV fields it did not
        // negotiate. The share is accepted with or without the grant.
        auto session = ready();
        auto const sent = open_channels(session, negotiation);
        auto const accepted = process(session, extension<submit_shares_extended>(),
            submit_shares_extended::message_type, followed(sent.extended, field));
        REQUIRE(single<submit_shares_success>(accepted).last_sequence_number == 2);
        // The extension covers only SubmitSharesExtended.
        require_closed(process(session, extension<submit_shares_standard>(),
            submit_shares_standard::message_type, followed(sent.standard, field)));
        for (auto const& trailing : malformed) {
            CAPTURE(trailing.size());
            auto strict = ready();
            auto const fresh = open_channels(strict, negotiation);
            require_closed(process(strict, extension<submit_shares_extended>(),
                submit_shares_extended::message_type, followed(fresh.extended, trailing)));
        }
    }
}
