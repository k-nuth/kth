// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "sync_harness.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>

#include <kth/domain/message/transaction.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/template_provider.hpp>

using namespace kth;
using namespace kth::node::sv2;
using namespace std::chrono_literals;

namespace {

setup_connection setup() {
    setup_connection message;
    message.protocol = setup_connection::protocol_template_distribution;
    message.min_version = 2;
    message.max_version = 2;
    return message;
}

template <typename Message>
data_chunk frame(Message const& message, uint16_t extension_type = 0) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{extension_type, Message::message_type,
        static_cast<uint32_t>(payload.size())});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

template_provider_session::result process(template_provider_session& session,
    uint16_t extension_type, uint8_t message_type, data_chunk const& payload) {
    return session.process({extension_type, message_type, static_cast<uint32_t>(payload.size())},
        payload);
}

template <typename Message>
template_provider_session::result process(template_provider_session& session, Message const& message) {
    return process(session, 0, Message::message_type, to_data_chunk(message));
}

template <typename Message>
Message response(data_chunk const& bytes) {
    byte_reader source(bytes);
    auto const header = message_header::from_data(source);
    REQUIRE(header.has_value());
    REQUIRE(header->extension_type == 0);
    REQUIRE(header->msg_type == Message::message_type);
    REQUIRE(header->msg_length == source.remaining_size());
    auto const message = Message::from_data(source);
    REQUIRE(message.has_value());
    REQUIRE(source.is_exhausted());
    return *message;
}

// A session that has completed setup and, if asked, sent its constraints.
template_provider_session ready_session(bool with_constraints = true) {
    template_provider_session session;
    REQUIRE_FALSE(process(session, setup()).close);
    if (with_constraints) {
        REQUIRE_FALSE(process(session, coinbase_output_constraints{100, 2}).close);
    }
    return session;
}

void require_silent(template_provider_session::result const& result) {
    REQUIRE_FALSE(result.close);
    REQUIRE(result.response.empty());
}

void require_closed(template_provider_session::result const& result) {
    REQUIRE(result.close);
    REQUIRE(result.response.empty());
}

struct operation {
    bool done = false;
    std::error_code error;
    size_t size = 0;
    data_chunk bytes;
};

struct startup {
    bool done = false;
    std::error_code error;
    uint16_t port = 0;
};

struct completion {
    bool done = false;
    std::exception_ptr error;
};

// Parameters, not lambda captures: they live in the coroutine frame.
::asio::awaitable<void> await_listening(std::shared_ptr<template_provider> provider,
    std::shared_ptr<startup> state) {
    try {
        state->port = co_await provider->wait_until_listening();
    } catch (std::system_error const& e) {
        state->error = e.code();
    }
    state->done = true;
}

// One io_context on the test thread. Every wait runs it in short slices up to
// a deadline, so a server that never answers fails the test instead of hanging.
struct listener_fixture {
    test::chain_fixture chain{"sv2_listener", 0};
    ::asio::io_context io;
    std::shared_ptr<template_provider> provider;
    std::shared_ptr<completion> completed = std::make_shared<completion>();
    std::vector<std::shared_ptr<::asio::ip::tcp::socket>> clients;
    bool running = false;

    explicit listener_fixture(node::sv2_settings const& settings)
    {
        REQUIRE(chain.created());
        REQUIRE(chain.start());
        provider = std::make_shared<template_provider>(settings, io.get_executor(), chain.chain());
    }

    ~listener_fixture() {
        provider->stop();
        for (auto const& client : clients) {
            std::error_code ignored;
            client->close(ignored);
        }
        if (running) (void)wait_until([&] { return completed->done; });
        io.stop();
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::seconds limit = 5s) {
        auto const deadline = std::chrono::steady_clock::now() + limit;
        while ( ! predicate() && std::chrono::steady_clock::now() < deadline) {
            io.restart();
            io.run_for(5ms);
        }
        return predicate();
    }

    template <typename T>
    T execute(::asio::awaitable<T> task) {
        auto result = ::asio::co_spawn(io, std::move(task), ::asio::use_future);
        REQUIRE(wait_until([&] { return result.wait_for(0s) == std::future_status::ready; }, 60s));
        return result.get();
    }

    void run() {
        running = true;
        ::asio::co_spawn(io, provider->run(),
            [state = completed, keep_alive = provider](std::exception_ptr error) {
                state->error = error;
                state->done = true;
            });
    }

    std::shared_ptr<startup> listening() {
        auto state = std::make_shared<startup>();
        ::asio::co_spawn(io, await_listening(provider, state), ::asio::detached);
        REQUIRE(wait_until([&] { return state->done; }));
        return state;
    }

    // The port the provider reports, never one assumed by the test.
    uint16_t start() {
        run();
        auto const state = listening();
        REQUIRE_FALSE(state->error);
        REQUIRE(state->port != 0);
        return state->port;
    }

    std::shared_ptr<::asio::ip::tcp::socket> connect(uint16_t port) {
        auto client = std::make_shared<::asio::ip::tcp::socket>(io);
        clients.push_back(client);
        auto state = std::make_shared<operation>();
        client->async_connect({::asio::ip::address_v4::loopback(), port},
            [state](std::error_code error) {
                state->error = error;
                state->done = true;
            });
        REQUIRE(wait_until([&] { return state->done; }));
        REQUIRE_FALSE(state->error);
        return client;
    }

    void write(::asio::ip::tcp::socket& client, byte_span bytes) {
        auto state = std::make_shared<operation>();
        state->bytes.assign(bytes.begin(), bytes.end());
        ::asio::async_write(client, ::asio::buffer(state->bytes),
            [state](std::error_code error, size_t size) {
                state->error = error;
                state->size = size;
                state->done = true;
            });
        REQUIRE(wait_until([&] { return state->done; }));
        REQUIRE_FALSE(state->error);
        REQUIRE(state->size == bytes.size());
    }

    std::shared_ptr<operation> read(::asio::ip::tcp::socket& client, size_t size) {
        auto state = std::make_shared<operation>();
        state->bytes.resize(size);
        ::asio::async_read(client, ::asio::buffer(state->bytes),
            [state](std::error_code error, size_t count) {
                state->error = error;
                state->size = count;
                state->done = true;
            });
        REQUIRE(wait_until([&] { return state->done; }));
        return state;
    }

    template <typename Message>
    Message receive(::asio::ip::tcp::socket& client) {
        auto const header_bytes = read(client, message_header::size);
        REQUIRE_FALSE(header_bytes->error);
        auto const header = from_data_chunk<message_header>(header_bytes->bytes);
        REQUIRE(header.has_value());
        REQUIRE(header->msg_length <= template_provider_session::max_payload_size);
        auto const payload = read(client, header->msg_length);
        REQUIRE_FALSE(payload->error);
        auto bytes = header_bytes->bytes;
        bytes.insert(bytes.end(), payload->bytes.begin(), payload->bytes.end());
        return response<Message>(bytes);
    }

    // Nothing more is sent, so a server that drops a frame sees EOF and closes;
    // a missing answer then fails the read instead of leaving it waiting.
    void finish_writing(::asio::ip::tcp::socket& client) {
        std::error_code error;
        client.shutdown(::asio::ip::tcp::socket::shutdown_send, error);
        REQUIRE_FALSE(error);
    }

    void require_closed(::asio::ip::tcp::socket& client) {
        auto const result = read(client, 1);
        REQUIRE(result->size == 0);
        REQUIRE(result->error == ::asio::error::eof);
    }

    void stop() {
        provider->stop();
        REQUIRE(wait_until([&] { return completed->done; }));
        REQUIRE_FALSE(completed->error);
    }
};

node::sv2_settings loopback_settings() {
    node::sv2_settings settings;
    settings.enabled = true;
    settings.port = 0;
    return settings;
}

std::vector<domain::chain::block> seed_chain(listener_fixture& fixture, uint32_t height) {
    auto const base = uint32_t(zulu_time()) - (height + 10) * test::block_spacing;
    std::vector<domain::chain::block> blocks;
    auto previous = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= height; ++h) {
        blocks.push_back(test::mine_block(previous, h, base + h * test::block_spacing, 0, {}, 0));
        previous = blocks.back().hash();
    }
    REQUIRE(fixture.chain.organizer().add_headers(test::headers_of(blocks)).headers_added == height);
    test::persist_headers(fixture.chain, blocks, 1);
    test::connect_bodies(fixture.chain, blocks, 1);
    return blocks;
}

// The same orchestrator that connects RPC submissions, with no external peers.
// Its lifetime extends through the completion even if an assertion unwinds.
struct submission_pipeline {
    struct node_state {
        kth::network::settings config;
        node::p2p_node network;

        static kth::network::settings settings(listener_fixture& fixture) {
            kth::network::settings config;
            config.identifier = 0xdab5bffa;
            config.protocol_maximum = 70015;
            config.inbound_port = 0;
            config.inbound_connections = 0;
            config.outbound_connections = 0;
            config.threads = 1;
            config.hosts_file = fixture.chain.dir() / "hosts.dat";
            config.banlist_file = fixture.chain.dir() / "banlist.dat";
            config.peers_file = fixture.chain.dir() / "peers.dat";
            return config;
        }

        explicit node_state(listener_fixture& fixture)
            : config(settings(fixture)), network(config)
        {}
    };

    listener_fixture& fixture;
    std::shared_ptr<node_state> state;
    std::shared_ptr<completion> completed = std::make_shared<completion>();

    explicit submission_pipeline(listener_fixture& listener)
        : fixture(listener), state(std::make_shared<node_state>(fixture)) {
        REQUIRE(fixture.execute(state->network.start()) == error::success);
        ::asio::co_spawn(fixture.io,
            node::sync::sync_orchestrator(fixture.chain.chain(), fixture.chain.organizer(),
                state->network, domain::config::network::regtest,
                [](std::string const& reason) { FAIL("sync failed: " << reason); }),
            [completed = completed, keep_alive = state](std::exception_ptr error) {
                completed->error = error;
                completed->done = true;
            });
    }

    ~submission_pipeline() {
        state->network.stop();
        CHECK(fixture.wait_until([&] { return completed->done; }, 30s));
        CHECK_FALSE(completed->error);
    }
};

struct template_pair {
    new_template work;
    set_new_prev_hash prev;
};

template_pair receive_template(listener_fixture& fixture, ::asio::ip::tcp::socket& client) {
    auto work = fixture.receive<new_template>(client);
    auto prev = fixture.receive<set_new_prev_hash>(client);
    REQUIRE(work.template_id == prev.template_id);
    return {std::move(work), std::move(prev)};
}

template_pair template_messages(template_provider_session::result const& result) {
    REQUIRE_FALSE(result.close);
    byte_reader reader(result.response);
    auto const first_header = message_header::from_data(reader);
    REQUIRE(first_header.has_value());
    REQUIRE(first_header->msg_type == new_template::message_type);
    auto const first_payload = reader.read_bytes(first_header->msg_length);
    REQUIRE(first_payload.has_value());
    byte_reader first_reader(*first_payload);
    auto work = new_template::from_data(first_reader);
    REQUIRE(work.has_value());
    REQUIRE(first_reader.is_exhausted());
    auto const second_header = message_header::from_data(reader);
    REQUIRE(second_header.has_value());
    REQUIRE(second_header->msg_type == set_new_prev_hash::message_type);
    auto prev = set_new_prev_hash::from_data(reader);
    REQUIRE(prev.has_value());
    REQUIRE(reader.is_exhausted());
    REQUIRE(work->template_id == prev->template_id);
    return {std::move(*work), std::move(*prev)};
}

blockchain::mining_template empty_template() {
    return {0x20000000, null_hash, 1, test::regtest_bits, 1, 1,
        50 * uint64_t{100000000}, 1000, 100, {}};
}

template_pair request_template(listener_fixture& fixture, ::asio::ip::tcp::socket& client,
    coinbase_output_constraints constraints = {120, 3}) {
    fixture.write(client, frame(constraints));
    return receive_template(fixture, client);
}

// Use the fields received on the wire, including the actual coinbase prefix.
domain::chain::block solution_block(template_pair const& job,
    std::vector<data_chunk> const& raw_transactions, bool valid_work = true) {
    auto script = job.work.coinbase_prefix;
    script.resize(100, 0);
    domain::chain::input::list inputs;
    inputs.emplace_back(domain::chain::output_point{null_hash, domain::chain::point::null_index},
        domain::chain::script(std::move(script), false), job.work.coinbase_tx_input_sequence);
    domain::chain::output::list outputs;
    outputs.emplace_back(job.work.coinbase_tx_value_remaining,
        test::miner_script(), domain::chain::token_data_opt{});
    domain::chain::transaction::list transactions;
    transactions.emplace_back(job.work.coinbase_tx_version, job.work.coinbase_tx_locktime,
        std::move(inputs), std::move(outputs));
    for (auto const& raw : raw_transactions) {
        byte_reader reader(raw);
        auto tx = domain::chain::transaction::from_data(reader, true);
        REQUIRE(tx.has_value());
        REQUIRE(reader.is_exhausted());
        transactions.push_back(std::move(*tx));
    }
    domain::chain::block candidate({}, std::move(transactions));
    auto const merkle = candidate.generate_merkle_root();
    auto branch_root = candidate.transactions().front().hash();
    for (auto const& sibling : job.work.merkle_path) {
        branch_root = bitcoin_hash(build_chunk({branch_root, sibling}));
    }
    REQUIRE(branch_root == merkle);
    // Regtest's target succeeds about every other hash. A broken target fails
    // after a small, explicit bound instead of spending a nonce space on CI.
    for (uint32_t nonce = 0; nonce < 10000; ++nonce) {
        domain::chain::header header{job.work.version, job.prev.prev_hash, merkle,
            job.prev.header_timestamp, job.prev.n_bits, nonce};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), false) == valid_work) {
            return domain::chain::block(std::move(header), candidate.transactions());
        }
    }
    FAIL("no regtest nonce matched the requested proof of work");
    return {};
}

submit_solution solution_message(template_pair const& job, domain::chain::block const& block) {
    auto const& coinbase = block.transactions().front();
    data_chunk bytes(coinbase.serialized_size(true));
    byte_writer writer(bytes);
    REQUIRE(coinbase.to_data(writer, true));
    return {job.work.template_id, block.header().version(), block.header().timestamp(),
        block.header().nonce(), std::move(bytes)};
}

} // namespace

// Session rules, without sockets.

TEST_CASE("sv2 session negotiates version 2 and keeps the latest constraints", "[sv2 template_provider]") {
    template_provider_session session;
    REQUIRE_FALSE(session.constraints().has_value());
    auto message = setup();
    message.min_version = 1;
    message.max_version = 3;
    auto const negotiated = process(session, message);
    REQUIRE_FALSE(negotiated.close);
    auto const success = response<setup_connection_success>(negotiated.response);
    REQUIRE(success.used_version == 2);
    REQUIRE(success.flags == 0);

    for (auto const constraints : {coinbase_output_constraints{100, 2}, coinbase_output_constraints{200, 4}}) {
        require_silent(process(session, constraints));
        REQUIRE(session.constraints().has_value());
        REQUIRE(session.constraints()->coinbase_output_max_additional_size == constraints.coinbase_output_max_additional_size);
        REQUIRE(session.constraints()->coinbase_output_max_additional_sigops == constraints.coinbase_output_max_additional_sigops);
    }

    auto other = ready_session(false);
    REQUIRE_FALSE(other.constraints().has_value());
    require_silent(process(other, coinbase_output_constraints{300, 8}));
    REQUIRE(other.constraints()->coinbase_output_max_additional_size == 300);
    REQUIRE(session.constraints()->coinbase_output_max_additional_size == 200);
}

TEST_CASE("sv2 session answers a setup it cannot serve with the matching error and closes", "[sv2 template_provider]") {
    struct rejected_setup {
        uint8_t protocol;
        uint16_t min_version;
        uint16_t max_version;
        uint32_t flags;
        char const* error;
    };
    for (auto const& rejected : {
            rejected_setup{setup_connection::protocol_mining, 2, 2, 0, "unsupported-protocol"},
            rejected_setup{setup_connection::protocol_job_declaration, 2, 2, 0, "unsupported-protocol"},
            rejected_setup{setup_connection::protocol_template_distribution, 1, 1, 0, "protocol-version-mismatch"},
            rejected_setup{setup_connection::protocol_template_distribution, 3, 4, 0, "protocol-version-mismatch"},
            rejected_setup{setup_connection::protocol_template_distribution, 3, 1, 0, "protocol-version-mismatch"},
            rejected_setup{setup_connection::protocol_template_distribution, 2, 2, 0x80000001u, "unsupported-feature-flags"}}) {
        CAPTURE(rejected.protocol, rejected.min_version, rejected.max_version, rejected.flags);
        template_provider_session session;
        auto message = setup();
        message.protocol = rejected.protocol;
        message.min_version = rejected.min_version;
        message.max_version = rejected.max_version;
        message.flags = rejected.flags;
        auto const result = process(session, message);
        REQUIRE(result.close);
        auto const error = response<setup_connection_error>(result.response);
        REQUIRE(error.error_code == rejected.error);
        REQUIRE(error.flags == rejected.flags);

        // A closed session accepts nothing more, not even a valid setup.
        require_closed(process(session, setup()));
    }
}

TEST_CASE("sv2 session requires setup first and constraints next", "[sv2 template_provider]") {
    SECTION("before setup") {
        auto const setup_bytes = to_data_chunk(setup());
        for (uint16_t const extension : {uint16_t{1}, uint16_t{0x8001}, message_header::channel_msg_flag}) {
            CAPTURE(extension);
            template_provider_session session;
            require_closed(process(session, extension, setup_connection::message_type, setup_bytes));
        }
        template_provider_session constraints_first;
        require_closed(process(constraints_first, coinbase_output_constraints{100, 2}));
        template_provider_session empty_setup;
        require_closed(process(empty_setup, 0, setup_connection::message_type, {}));
    }
    SECTION("after setup, before constraints") {
        auto request = ready_session(false);
        require_closed(process(request, request_transaction_data{7}));
        auto solution = ready_session(false);
        require_closed(process(solution, submit_solution{}));
    }
}

TEST_CASE("sv2 session answers requests for templates it never sent", "[sv2 template_provider]") {
    auto session = ready_session();
    auto const result = process(session, request_transaction_data{7});
    REQUIRE_FALSE(result.close);
    auto const error = response<request_transaction_data_error>(result.response);
    REQUIRE(error.template_id == 7);
    REQUIRE(error.error_code == "template-id-not-found");

    submit_solution solution;
    solution.template_id = 7;
    solution.coinbase_tx = data_chunk(60, 0x01);
    require_silent(process(session, solution));
    require_silent(process(session, coinbase_output_constraints{200, 4}));
}

TEST_CASE("sv2 session discards messages of unknown extensions", "[sv2 template_provider]") {
    auto session = ready_session(false);
    data_chunk const payload{0xde, 0xad, 0xbe, 0xef};
    for (uint16_t const extension : {uint16_t{1}, uint16_t{0x7fff}, uint16_t{0x8001}}) {
        CAPTURE(extension);
        require_silent(process(session, extension, coinbase_output_constraints::message_type, payload));
        require_silent(process(session, extension, 0x7f, {}));
    }
    // Still before constraints: the discarded frames did not count as messages.
    require_silent(process(session, coinbase_output_constraints{100, 2}));

    // On extension 0 the channel_msg bit is never set by this protocol.
    require_closed(process(session, message_header::channel_msg_flag,
        coinbase_output_constraints::message_type, to_data_chunk(coinbase_output_constraints{100, 2})));
}

TEST_CASE("sv2 session closes on malformed, server-only and undefined messages", "[sv2 template_provider]") {
    auto rejects = [](uint8_t type, data_chunk const& payload, bool with_constraints) {
        auto session = ready_session(with_constraints);
        require_closed(process(session, 0, type, payload));
        require_closed(process(session, coinbase_output_constraints{100, 2}));
    };
    auto malformed = [&](auto const& message, bool with_constraints) {
        CAPTURE(message.message_type);
        auto bytes = to_data_chunk(message);
        bytes.pop_back();
        rejects(message.message_type, bytes, with_constraints);
        bytes = to_data_chunk(message);
        bytes.push_back(0);
        rejects(message.message_type, bytes, with_constraints);
    };

    {
        template_provider_session session;
        auto bytes = to_data_chunk(setup());
        bytes.pop_back();
        require_closed(process(session, 0, setup_connection::message_type, bytes));
    }
    malformed(coinbase_output_constraints{100, 2}, false);
    malformed(request_transaction_data{7}, true);
    malformed(submit_solution{}, true);

    // Messages only a server sends, a second setup, and an undefined type.
    rejects(setup_connection::message_type, to_data_chunk(setup()), true);
    rejects(setup_connection_success::message_type, to_data_chunk(setup_connection_success{2, 0}), true);
    rejects(new_template::message_type, to_data_chunk(new_template{}), true);
    rejects(set_new_prev_hash::message_type, to_data_chunk(set_new_prev_hash{}), true);
    rejects(request_transaction_data_success::message_type, to_data_chunk(request_transaction_data_success{}), true);
    rejects(request_transaction_data_error::message_type, to_data_chunk(request_transaction_data_error{}), true);
    rejects(0x7f, {}, true);

    // A header whose length disagrees with the payload, or exceeds the bound.
    auto session = ready_session();
    auto const bytes = to_data_chunk(request_transaction_data{7});
    require_closed(session.process({0, request_transaction_data::message_type,
        static_cast<uint32_t>(bytes.size() + 1)}, bytes));
    template_provider_session oversized;
    require_closed(oversized.process({0, 0, template_provider_session::max_payload_size + 1}, {}));
}

// The listener, over loopback sockets on an OS-assigned port.

TEST_CASE("sv2 templates encode the BIP34 height and distinguish evicted from stale ids", "[sv2 template_provider]") {
    auto session = ready_session();
    auto value = empty_template();
    uint64_t first_id = 0;
    for (uint32_t const height : {1, 16, 17, 127, 128}) {
        value.height = height;
        auto const job = template_messages(session.set_template(value));
        if (first_id == 0) first_id = job.work.template_id;
        REQUIRE(job.work.coinbase_prefix == test::coinbase_script(height, 0).first_operation().to_data());
        REQUIRE(job.work.merkle_path.empty());
        REQUIRE(job.work.future_template);
        // The regtest target is 0x7fffff followed by 29 zero bytes, serialized
        // as a little-endian U256 by TDP.
        hash_digest target{};
        target[29] = 0xff;
        target[30] = 0xff;
        target[31] = 0x7f;
        REQUIRE(job.prev.target == target);
    }
    for (size_t i = 0; i < template_provider_session::max_templates; ++i) {
        (void)template_messages(session.set_template(value));
    }
    REQUIRE(response<request_transaction_data_error>(process(session,
        request_transaction_data{first_id}).response).error_code == "template-id-not-found");
    hash_digest next_tip{};
    next_tip[0] = 1;
    session.set_tip(next_tip);
    REQUIRE(response<request_transaction_data_error>(process(session,
        request_transaction_data{first_id}).response).error_code == "stale-template-id");
    REQUIRE(response<request_transaction_data_error>(process(session,
        request_transaction_data{0}).response).error_code == "template-id-not-found");
    REQUIRE(response<request_transaction_data_error>(process(session,
        request_transaction_data{std::numeric_limits<uint64_t>::max()}).response).error_code == "template-id-not-found");
}

TEST_CASE("sv2 does not advertise templates outside coinbase or TDP limits", "[sv2 template_provider]") {
    auto value = empty_template();
    SECTION("setup must be complete") {
        template_provider_session session;
        require_closed(session.set_template(value));
        return;
    }
    SECTION("constraints must be present") {
        auto session = ready_session(false);
        require_closed(session.set_template(value));
        return;
    }
    SECTION("coinbase bytes cannot fit") { value.size_limit = 347; }
    SECTION("coinbase SigChecks cannot fit") { value.sigchecks_limit = 1; }
    SECTION("target overflows") { value.bits = 0x23010000; }
    SECTION("height prefix exceeds eight bytes") {
        value.height = static_cast<size_t>(std::numeric_limits<int64_t>::max());
    }
    SECTION("transaction count exceeds U16") {
        auto tx = std::make_shared<domain::message::transaction const>(domain::chain::transaction{});
        value.selection.txs.assign(0x10000, tx);
    }
    SECTION("transaction data exceeds one U24 frame") {
        domain::chain::output::list outputs;
        outputs.emplace_back(0, domain::chain::script(data_chunk(1024, 0), false), domain::chain::token_data_opt{});
        auto tx = std::make_shared<domain::message::transaction const>(
            domain::chain::transaction(1, 0, {}, std::move(outputs)));
        value.selection.txs.assign(20000, tx);
    }
    auto session = ready_session();
    require_closed(session.set_template(std::move(value)));
}

TEST_CASE("sv2 listener reports its port and serves a client whose frames arrive in pieces", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto client = fixture.connect(fixture.start());

    auto const setup_bytes = frame(setup());
    fixture.write(*client, byte_span(setup_bytes).first(3));
    fixture.write(*client, byte_span(setup_bytes).subspan(3));
    auto const success = fixture.receive<setup_connection_success>(*client);
    REQUIRE(success.used_version == 2);
    REQUIRE(success.flags == 0);

    // A request is answered only after constraints were accepted, so the reply
    // also shows the constraints frame was read whole and kept.
    auto const constraints = frame(coinbase_output_constraints{120, 3});
    fixture.write(*client, byte_span(constraints).first(constraints.size() - 1));
    fixture.write(*client, byte_span(constraints).last(1));
    (void)receive_template(fixture, *client);
    fixture.write(*client, frame(request_transaction_data{9}));
    auto const error = fixture.receive<request_transaction_data_error>(*client);
    REQUIRE(error.template_id == 9);

    fixture.stop();
    fixture.require_closed(*client);
}

TEST_CASE("sv2 listener keeps a connection that sends an unknown extension message", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto client = fixture.connect(fixture.start());
    fixture.write(*client, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*client);

    fixture.write(*client, frame(coinbase_output_constraints{120, 3}, 0x0001));
    fixture.write(*client, frame(coinbase_output_constraints{120, 3}));
    (void)receive_template(fixture, *client);
    fixture.write(*client, frame(request_transaction_data{5}));
    REQUIRE(fixture.receive<request_transaction_data_error>(*client).template_id == 5);
    fixture.finish_writing(*client);
    fixture.require_closed(*client);
    fixture.stop();
}

TEST_CASE("sv2 listener answers a refused setup and closes the connection", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto const port = fixture.start();
    auto client = fixture.connect(port);
    auto message = setup();
    message.protocol = setup_connection::protocol_mining;
    fixture.write(*client, frame(message));
    fixture.finish_writing(*client);
    REQUIRE(fixture.receive<setup_connection_error>(*client).error_code == "unsupported-protocol");
    fixture.require_closed(*client);

    // The listener itself keeps serving.
    auto next = fixture.connect(port);
    fixture.write(*next, frame(setup()));
    REQUIRE(fixture.receive<setup_connection_success>(*next).used_version == 2);
    fixture.stop();
}

TEST_CASE("sv2 listener closes a connection on an oversized, partial or channel frame", "[sv2 template_provider]") {
    for (int const invalid : {0, 1, 2, 3}) {
        CAPTURE(invalid);
        listener_fixture fixture(loopback_settings());
        auto client = fixture.connect(fixture.start());
        data_chunk bytes;
        if (invalid == 0) {
            // Closed on the header alone: the payload is never read.
            bytes = to_data_chunk(message_header{0, 0, template_provider_session::max_payload_size + 1});
        } else if (invalid == 1) {
            bytes = frame(setup());
            bytes.resize(3);
        } else {
            fixture.write(*client, frame(setup()));
            (void)fixture.receive<setup_connection_success>(*client);
            if (invalid == 2) {
                bytes = frame(coinbase_output_constraints{120, 3});
                bytes.pop_back();
            } else {
                bytes = frame(coinbase_output_constraints{120, 3}, message_header::channel_msg_flag);
            }
        }
        fixture.write(*client, byte_span(bytes).first(3));
        fixture.write(*client, byte_span(bytes).subspan(3));
        fixture.finish_writing(*client);
        fixture.require_closed(*client);
        fixture.stop();
    }
}

TEST_CASE("sv2 stop ends idle and half-read connections", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto const port = fixture.start();
    auto idle = fixture.connect(port);
    auto in_header = fixture.connect(port);
    auto in_payload = fixture.connect(port);
    auto const bytes = frame(setup());
    fixture.write(*in_header, byte_span(bytes).first(3));
    fixture.write(*in_payload, byte_span(bytes).first(message_header::size + 1));
    // A completed setup shows the listener is serving connections by now.
    auto ready = fixture.connect(port);
    fixture.write(*ready, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*ready);

    fixture.stop();
    fixture.require_closed(*idle);
    fixture.require_closed(*in_header);
    fixture.require_closed(*in_payload);
    fixture.require_closed(*ready);
}

TEST_CASE("sv2 stop before run wakes a startup waiter", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    fixture.provider->stop();
    fixture.provider->stop();
    auto const state = fixture.listening();
    REQUIRE(state->error);
    REQUIRE(state->port == 0);
    fixture.run();
    fixture.stop();
}

TEST_CASE("sv2 listener reaps finished connections before admitting more", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto const port = fixture.start();
    // More clients than the 64 connections admitted at once.
    for (size_t i = 0; i < 66; ++i) {
        CAPTURE(i);
        auto client = fixture.connect(port);
        fixture.write(*client, frame(setup()));
        fixture.finish_writing(*client);
        REQUIRE(fixture.receive<setup_connection_success>(*client).used_version == 2);
        fixture.require_closed(*client);
    }
    fixture.stop();
}

TEST_CASE("sv2 listener reports a bind failure as its startup outcome", "[sv2 template_provider]") {
    auto settings = loopback_settings();
    SECTION("invalid bind address") {
        settings.bind = "not-an-ip-address";
        listener_fixture fixture(settings);
        fixture.run();
        auto const state = fixture.listening();
        REQUIRE(state->error);
        REQUIRE(state->port == 0);
        REQUIRE(fixture.wait_until([&] { return fixture.completed->done; }));
        REQUIRE_FALSE(fixture.completed->error);
    }
    SECTION("port already in use") {
        ::asio::io_context io;
        ::asio::ip::tcp::acceptor occupied(io, {::asio::ip::address_v4::loopback(), 0});
        settings.port = occupied.local_endpoint().port();
        REQUIRE(settings.port != 0);
        listener_fixture fixture(settings);
        fixture.run();
        auto const state = fixture.listening();
        REQUIRE(state->error == ::asio::error::address_in_use);
        REQUIRE(fixture.wait_until([&] { return fixture.completed->done; }));
        REQUIRE_FALSE(fixture.completed->error);
    }
}

TEST_CASE("sv2 constraints send the chain template and its transactions in block order", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto const trunk = seed_chain(fixture, 101);
    auto const view = fixture.chain.chain().chain_view();
    REQUIRE(view);
    auto const& first_coinbase = trunk.front().transactions().front();
    auto const parent = test::spend_p2pkh(first_coinbase, 0,
        first_coinbase.outputs().front().value() - 1000, view->state->enabled_flags());
    auto const child = test::spend_p2pkh(parent, 0,
        parent.outputs().front().value() - 1000, view->state->enabled_flags());
    REQUIRE(fixture.execute(fixture.chain.chain().organize(
        std::make_shared<domain::message::transaction const>(parent))) == error::success);
    REQUIRE(fixture.execute(fixture.chain.chain().organize(
        std::make_shared<domain::message::transaction const>(child))) == error::success);

    auto const expected = fixture.execute(fixture.chain.chain().fetch_mining_template(248 + 120, 3));
    REQUIRE(expected.has_value());
    REQUIRE(expected->selection.txs.size() == 2);
    auto client = fixture.connect(fixture.start());
    fixture.write(*client, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*client);
    auto const job = request_template(fixture, *client);
    REQUIRE(job.work.future_template);
    REQUIRE(job.work.version == expected->version);
    REQUIRE(job.work.coinbase_prefix == test::coinbase_script(uint32_t(expected->height), 0).first_operation().to_data());
    REQUIRE(job.work.coinbase_prefix.size() <= 8);
    REQUIRE(job.work.coinbase_tx_version == 1);
    REQUIRE(job.work.coinbase_tx_input_sequence == 0xffffffff);
    REQUIRE(job.work.coinbase_tx_value_remaining == expected->coinbase_value);
    REQUIRE(job.work.coinbase_tx_outputs_count == 0);
    REQUIRE(job.work.coinbase_tx_outputs.empty());
    REQUIRE(job.work.coinbase_tx_locktime == 0);
    REQUIRE(job.prev.prev_hash == expected->previous_block_hash);
    REQUIRE(job.prev.n_bits == expected->bits);
    REQUIRE(job.prev.header_timestamp >= expected->min_time);

    fixture.write(*client, frame(request_transaction_data{job.work.template_id}));
    auto const data = fixture.receive<request_transaction_data_success>(*client);
    REQUIRE(data.template_id == job.work.template_id);
    REQUIRE(data.excess_data.empty());
    REQUIRE(data.transaction_list.size() == expected->selection.txs.size());
    for (size_t i = 0; i < data.transaction_list.size(); ++i) {
        domain::chain::transaction const& tx = *expected->selection.txs[i];
        data_chunk bytes(tx.serialized_size(true));
        byte_writer writer(bytes);
        REQUIRE(tx.to_data(writer, true));
        REQUIRE(data.transaction_list[i] == bytes);
    }
    // The three-leaf tree checks the odd-leaf duplication as well as the
    // coinbase-at-zero branch; the client reconstructs the same block root.
    (void)solution_block(job, data.transaction_list);

    auto const updated = request_template(fixture, *client, {200, 4});
    REQUIRE(updated.work.template_id > job.work.template_id);
    REQUIRE(updated.prev.prev_hash == job.prev.prev_hash);
    fixture.write(*client, frame(request_transaction_data{job.work.template_id}));
    fixture.finish_writing(*client);
    REQUIRE(fixture.receive<request_transaction_data_success>(*client).transaction_list == data.transaction_list);
    fixture.require_closed(*client);
    fixture.stop();
}

TEST_CASE("sv2 solved templates connect and notify ready clients without a request", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto const trunk = seed_chain(fixture, 5);
    submission_pipeline pipeline(fixture);
    auto const port = fixture.start();
    auto miner = fixture.connect(port);
    fixture.write(*miner, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*miner);
    auto const job = request_template(fixture, *miner);
    auto observer = fixture.connect(port);
    fixture.write(*observer, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*observer);
    auto const observed = request_template(fixture, *observer);

    auto const wrong = solution_block(job, {}, false);
    fixture.write(*miner, frame(solution_message(job, wrong)));
    fixture.write(*miner, frame(request_transaction_data{job.work.template_id}));
    (void)fixture.receive<request_transaction_data_success>(*miner);
    REQUIRE(fixture.chain.chain().chain_view()->tip_hash == trunk.back().hash());

    auto const solved = solution_block(job, {});
    fixture.write(*miner, frame(solution_message(job, solved)));
    auto const next = receive_template(fixture, *miner);
    auto const observer_next = receive_template(fixture, *observer);
    REQUIRE(next.work.template_id > job.work.template_id);
    REQUIRE(observer_next.work.template_id > observed.work.template_id);
    REQUIRE(next.work.future_template);
    REQUIRE(next.prev.prev_hash == solved.hash());
    REQUIRE(observer_next.prev.prev_hash == solved.hash());
    auto const view = fixture.chain.chain().chain_view();
    REQUIRE(view);
    REQUIRE(view->connected_tip_height == 6);
    REQUIRE(view->tip_hash == solved.hash());

    fixture.write(*miner, frame(request_transaction_data{job.work.template_id}));
    auto const stale = fixture.receive<request_transaction_data_error>(*miner);
    REQUIRE(stale.template_id == job.work.template_id);
    REQUIRE(stale.error_code == "stale-template-id");
    // Neither a stale solution nor an id never issued gets a TDP reply. The
    // request after them proves they were consumed and the read loop continued.
    fixture.write(*miner, frame(solution_message(job, solved)));
    auto unknown = solution_message(next, solved);
    unknown.template_id = std::numeric_limits<uint64_t>::max();
    fixture.write(*miner, frame(unknown));
    fixture.write(*miner, frame(request_transaction_data{next.work.template_id}));
    fixture.finish_writing(*miner);
    REQUIRE(fixture.receive<request_transaction_data_success>(*miner).template_id == next.work.template_id);
    REQUIRE(fixture.chain.chain().chain_view()->tip_hash == solved.hash());
    fixture.require_closed(*miner);
    fixture.stop();
}

TEST_CASE("sv2 refuses malformed coinbase solutions even across reads", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto const trunk = seed_chain(fixture, 5);
    auto client = fixture.connect(fixture.start());
    fixture.write(*client, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*client);
    auto const job = request_template(fixture, *client);
    auto const solved = solution_block(job, {});
    for (bool const trailing : {false, true}) {
        CAPTURE(trailing);
        auto solution = solution_message(job, solved);
        if (trailing) {
            solution.coinbase_tx.push_back(0);
        } else {
            solution.coinbase_tx.pop_back();
        }
        auto const bytes = frame(solution);
        fixture.write(*client, byte_span(bytes).first(bytes.size() - 1));
        fixture.write(*client, byte_span(bytes).last(1));
        fixture.write(*client, frame(request_transaction_data{job.work.template_id}));
        REQUIRE(fixture.receive<request_transaction_data_success>(*client).template_id == job.work.template_id);
        REQUIRE(fixture.chain.chain().chain_view()->tip_hash == trunk.back().hash());
    }
    fixture.stop();
}

TEST_CASE("sv2 bounds retained templates and rejects constraints that cannot fit", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto client = fixture.connect(fixture.start());
    fixture.write(*client, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*client);
    auto const first = request_template(fixture, *client);
    auto previous = first.work.template_id;
    for (size_t i = 0; i < 16; ++i) {
        auto const current = request_template(fixture, *client, {uint32_t(120 + i), 3});
        REQUIRE(current.work.template_id > previous);
        previous = current.work.template_id;
    }
    fixture.write(*client, frame(request_transaction_data{first.work.template_id}));
    auto const evicted = fixture.receive<request_transaction_data_error>(*client);
    REQUIRE(evicted.template_id == first.work.template_id);
    REQUIRE(evicted.error_code == "template-id-not-found");

    auto const oversized = frame(coinbase_output_constraints{std::numeric_limits<uint32_t>::max(), 0});
    fixture.write(*client, byte_span(oversized).first(oversized.size() - 1));
    fixture.write(*client, byte_span(oversized).last(1));
    fixture.finish_writing(*client);
    fixture.require_closed(*client);
    fixture.stop();
}

TEST_CASE("sv2 closes a connection when the chain has stopped", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto client = fixture.connect(fixture.start());
    fixture.write(*client, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*client);
    REQUIRE(fixture.chain.chain().stop());
    fixture.write(*client, frame(coinbase_output_constraints{120, 3}));
    fixture.finish_writing(*client);
    fixture.require_closed(*client);
    fixture.stop();
}

TEST_CASE("sv2 keeps a client through a moment without a template and sends one when it can", "[sv2 template_provider]") {
    // A chain mid-transition, like one holding a new header whose block is
    // still connecting, has no template for a moment. The client is kept, and
    // the wake-up that ends the transition brings its work.
    listener_fixture fixture(loopback_settings());
    (void)seed_chain(fixture, 101);
    auto client = fixture.connect(fixture.start());
    fixture.write(*client, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*client);

    auto& chain = fixture.chain.chain();
    REQUIRE(chain.begin_transition());
    fixture.write(*client, frame(coinbase_output_constraints{120, 3}));
    int slices = 0;
    (void)fixture.wait_until([&] { return ++slices > 20; });

    chain.end_transition();
    auto const work = fixture.receive<new_template>(*client);
    auto const activation = fixture.receive<set_new_prev_hash>(*client);
    CHECK(activation.template_id == work.template_id);
    CHECK(work.future_template);
    fixture.stop();
    fixture.require_closed(*client);
}
