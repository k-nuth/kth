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
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>

#include <kth/domain/chain/compact.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/domain/wallet/cashaddr.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/mining_messages.hpp>
#include <kth/node/sv2/template_provider.hpp>

using namespace kth;
using namespace kth::node::sv2;
using namespace std::chrono_literals;

namespace {

setup_connection setup() {
    setup_connection message;
    message.protocol = setup_connection::protocol_mining;
    message.min_version = 2;
    message.max_version = 2;
    return message;
}

template <typename Message>
constexpr uint16_t channel_extension() {
    if constexpr (requires { Message::channel_message; }) {
        return Message::channel_message ? message_header::channel_msg_flag : 0;
    } else {
        return 0;
    }
}

template <typename Message>
data_chunk frame(Message const& message) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{channel_extension<Message>(), Message::message_type,
        static_cast<uint32_t>(payload.size())});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

template <typename Message>
Message response(data_chunk const& bytes) {
    byte_reader source(bytes);
    auto const header = message_header::from_data(source);
    REQUIRE(header.has_value());
    REQUIRE(header->extension_type == channel_extension<Message>());
    REQUIRE(header->msg_type == Message::message_type);
    REQUIRE(header->msg_length == source.remaining_size());
    auto const message = Message::from_data(source);
    REQUIRE(message.has_value());
    REQUIRE(source.is_exhausted());
    return *message;
}

// A P2PKH cashaddr on the given network: version byte 0 and a 20-byte hash,
// packed into 5-bit groups. (The cashaddr spec's checksum vector has no
// version byte, so it is not an address.)
std::string payout_address(std::string_view prefix = "bchreg") {
    data_chunk bytes(1 + 20, 0x11);
    bytes[0] = 0x00;
    data_chunk groups;
    uint32_t accumulator = 0;
    unsigned bits = 0;
    for (auto const byte : bytes) {
        accumulator = (accumulator << 8) | byte;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            groups.push_back(uint8_t((accumulator >> bits) & 31));
        }
    }
    if (bits > 0) {
        groups.push_back(uint8_t((accumulator << (5 - bits)) & 31));
    }
    return domain::wallet::cashaddr::encode(std::string{prefix}, groups);
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
    test::chain_fixture chain{"sv2_mining", 0};
    ::asio::io_context io;
    std::shared_ptr<template_provider> provider;
    std::shared_ptr<completion> completed = std::make_shared<completion>();
    std::vector<std::shared_ptr<::asio::ip::tcp::socket>> clients;
    bool running = false;

    explicit listener_fixture(node::sv2_settings const& settings)
    {
        REQUIRE(chain.created());
        REQUIRE(chain.start());
        provider = std::make_shared<template_provider>(settings, io.get_executor(),
            chain.chain(), domain::config::network::regtest);
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
    bool wait_until(Predicate predicate, std::chrono::milliseconds limit = 5s) {
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

    std::shared_ptr<operation> begin_write(::asio::ip::tcp::socket& client, byte_span bytes) {
        auto state = std::make_shared<operation>();
        state->bytes.assign(bytes.begin(), bytes.end());
        ::asio::async_write(client, ::asio::buffer(state->bytes),
            [state](std::error_code error, size_t size) {
                state->error = error;
                state->size = size;
                state->done = true;
            });
        return state;
    }

    void write(::asio::ip::tcp::socket& client, byte_span bytes) {
        auto const state = begin_write(client, bytes);
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
    settings.noise = false;
    settings.port = 0;
    settings.mining.enabled = true;
    settings.mining.address = payout_address();
    return settings;
}

struct noise_listener_fixture : listener_fixture {
    node::sv2_settings noise_settings;
    hash_digest authority{};

    noise_listener_fixture()
        : listener_fixture(loopback_settings())
        , noise_settings(loopback_settings())
    {
        noise_settings.noise = true;
        noise_settings.static_key_file = (chain.dir() / "noise-static").string();
        noise_settings.authority_key_file = (chain.dir() / "noise-authority").string();
        auto const now = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        std::string error;
        auto const credentials = load_noise_credentials(noise_settings, now, error);
        INFO(error);
        REQUIRE(credentials.has_value());
        authority = credentials->authority_public_key;
        configure();
    }

    void configure() {
        provider = std::make_shared<template_provider>(noise_settings, io.get_executor(),
            chain.chain(), domain::config::network::regtest);
    }

    noise_transport handshake(::asio::ip::tcp::socket& client) {
        auto const now = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        noise_initiator initiator(noise_keypair::generate(), authority, now);
        auto const first = initiator.start();
        REQUIRE(first.has_value());
        write(client, byte_span(*first).first(13));
        write(client, byte_span(*first).subspan(13));
        auto const second = read(client, noise_responder::flight_size);
        REQUIRE_FALSE(second->error);
        REQUIRE(initiator.finish(second->bytes));
        REQUIRE(initiator.keys().has_value());
        return noise_transport(*initiator.keys(), true);
    }

    template <typename Message>
    void send(::asio::ip::tcp::socket& client, noise_transport& transport, Message const& message) {
        auto const bytes = transport.encrypt_frame(frame(message));
        REQUIRE(bytes.has_value());
        write(client, byte_span(*bytes).first(7));
        write(client, byte_span(*bytes).subspan(7));
    }

    template <typename Message>
    Message receive(::asio::ip::tcp::socket& client, noise_transport& transport) {
        // Reading a byte at a time also proves that the decryptor retains an
        // incomplete header or tag without treating it as a plaintext frame.
        auto const deadline = std::chrono::steady_clock::now() + 5s;
        for (size_t count = 0; count < 65536; ++count) {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            auto const next = read(client, 1);
            REQUIRE_FALSE(next->error);
            auto const frames = transport.feed(next->bytes);
            REQUIRE(frames.has_value());
            if ( ! frames->empty()) {
                REQUIRE(frames->size() == 1);
                return response<Message>(frames->front());
            }
        }
        FAIL("Noise frame exceeded the bounded test read");
        return {};
    }
};

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

hash_digest easiest_target() {
    hash_digest target;
    target.fill(0xff);
    return target;
}

struct standard_work {
    open_standard_mining_channel_success channel;
    new_mining_job job;
    mining_set_new_prev_hash prev;
};

struct extended_work {
    open_extended_mining_channel_success channel;
    new_extended_mining_job job;
    mining_set_new_prev_hash prev;
};

standard_work receive_standard(listener_fixture& fixture, ::asio::ip::tcp::socket& client,
    open_standard_mining_channel_success channel) {
    auto job = fixture.receive<new_mining_job>(client);
    auto prev = fixture.receive<mining_set_new_prev_hash>(client);
    REQUIRE(job.channel_id == channel.channel_id);
    REQUIRE(prev.channel_id == channel.channel_id);
    REQUIRE(prev.job_id == job.job_id);
    REQUIRE_FALSE(job.ntime_start.has_value());
    return {std::move(channel), std::move(job), std::move(prev)};
}

standard_work open_standard(listener_fixture& fixture, ::asio::ip::tcp::socket& client,
    uint32_t request_id) {
    fixture.write(client, frame(open_standard_mining_channel{request_id, "miner", 0.f,
        easiest_target()}));
    auto channel = fixture.receive<open_standard_mining_channel_success>(client);
    REQUIRE(channel.request_id == request_id);
    REQUIRE(channel.target == easiest_target());
    return receive_standard(fixture, client, std::move(channel));
}

extended_work receive_extended(listener_fixture& fixture, ::asio::ip::tcp::socket& client,
    open_extended_mining_channel_success channel) {
    auto job = fixture.receive<new_extended_mining_job>(client);
    auto prev = fixture.receive<mining_set_new_prev_hash>(client);
    REQUIRE(job.channel_id == channel.channel_id);
    REQUIRE(prev.channel_id == channel.channel_id);
    REQUIRE(prev.job_id == job.job_id);
    REQUIRE_FALSE(job.ntime_start.has_value());
    return {std::move(channel), std::move(job), std::move(prev)};
}

extended_work open_extended(listener_fixture& fixture, ::asio::ip::tcp::socket& client,
    uint32_t request_id) {
    fixture.write(client, frame(open_extended_mining_channel{request_id, "proxy", 0.f,
        easiest_target(), 8}));
    auto channel = fixture.receive<open_extended_mining_channel_success>(client);
    REQUIRE(channel.request_id == request_id);
    REQUIRE(channel.target == easiest_target());
    REQUIRE(channel.extranonce_size >= 8);
    REQUIRE(channel.extranonce_size <= 32);
    return receive_extended(fixture, client, std::move(channel));
}

domain::chain::transaction coinbase(extended_work const& work, data_chunk const& extranonce) {
    REQUIRE(extranonce.size() == work.channel.extranonce_size);
    auto bytes = build_chunk({work.job.coinbase_tx_prefix, work.channel.extranonce_prefix,
        extranonce, work.job.coinbase_tx_suffix});
    byte_reader reader(bytes);
    auto tx = domain::chain::transaction::from_data(reader, true);
    REQUIRE(tx.has_value());
    REQUIRE(reader.is_exhausted());
    return std::move(*tx);
}

hash_digest merkle_root(extended_work const& work, data_chunk const& extranonce) {
    auto root = coinbase(work, extranonce).hash();
    for (auto const& sibling : work.job.merkle_path) {
        root = bitcoin_hash(build_chunk({root, sibling}));
    }
    return root;
}

domain::chain::header solve(uint32_t version, hash_digest const& root,
    mining_set_new_prev_hash const& prev) {
    auto const target = domain::chain::compact::from_compact(prev.nbits);
    REQUIRE(target.has_value());
    for (uint32_t nonce = 0; nonce < 10000; ++nonce) {
        domain::chain::header header(version, prev.prev_hash, root, prev.ntime_start,
            prev.nbits, nonce);
        if (to_uint256(domain::chain::hash(header)) <= target->big()) return header;
    }
    FAIL("no regtest nonce met the network target");
    return {};
}

void require_setup(listener_fixture& fixture, ::asio::ip::tcp::socket& client) {
    auto const bytes = frame(setup());
    fixture.write(client, byte_span(bytes).first(3));
    fixture.write(client, byte_span(bytes).subspan(3));
    auto const accepted = fixture.receive<setup_connection_success>(client);
    REQUIRE(accepted.used_version == 2);
    REQUIRE(accepted.flags == 0);
}

} // namespace

TEST_CASE("sv2 mining setup refusals keep the shared listener serving", "[sv2 mining]") {
    auto settings = loopback_settings();
    auto message = setup();
    std::string expected;
    SECTION("mining disabled") {
        settings.mining.enabled = false;
        expected = "unsupported-protocol";
    }
    SECTION("version 2 outside range") {
        message.min_version = 3;
        message.max_version = 4;
        expected = "protocol-version-mismatch";
    }
    SECTION("work selection unavailable") {
        message.flags = mining_flag_requires_work_selection;
        expected = "unsupported-feature-flags";
    }
    SECTION("unknown flags") {
        message.flags = 0x80000000u;
        expected = "unsupported-feature-flags";
    }
    listener_fixture fixture(settings);
    auto const port = fixture.start();
    auto client = fixture.connect(port);
    auto const bytes = frame(message);
    fixture.write(*client, byte_span(bytes).first(bytes.size() - 1));
    fixture.write(*client, byte_span(bytes).last(1));
    fixture.finish_writing(*client);
    auto const error = fixture.receive<setup_connection_error>(*client);
    REQUIRE(error.error_code == expected);
    REQUIRE(error.flags == message.flags);
    fixture.require_closed(*client);

    // Both protocols share this listener; rejecting one setup must not stop it.
    auto observer = fixture.connect(port);
    auto tdp = setup();
    tdp.protocol = setup_connection::protocol_template_distribution;
    fixture.write(*observer, frame(tdp));
    fixture.finish_writing(*observer);
    REQUIRE(fixture.receive<setup_connection_success>(*observer).used_version == 2);
    fixture.require_closed(*observer);
    fixture.stop();
}

TEST_CASE("sv2 mining refuses an absent or wrong-network payout before listening", "[sv2 mining]") {
    auto settings = loopback_settings();
    SECTION("missing payout") { settings.mining.address.clear(); }
    SECTION("wrong network") { settings.mining.address = payout_address("bitcoincash"); }
    SECTION("malformed payout") { settings.mining.address = "bchreg:invalid"; }
    listener_fixture fixture(settings);
    fixture.run();
    auto const state = fixture.listening();
    REQUIRE(state->error == std::make_error_code(std::errc::invalid_argument));
    REQUIRE(state->port == 0);
    REQUIRE(fixture.wait_until([&] { return fixture.completed->done; }));
    REQUIRE_FALSE(fixture.completed->error);
}

TEST_CASE("sv2 mining configuration refuses node startup before opening the chain", "[sv2 mining]") {
    test::chain_fixture directory{"sv2_mining_startup"};
    REQUIRE(directory.created());
    node::configuration config(domain::config::network::regtest);
    config.database.directory = directory.dir();
    config.network.threads = 1;
    config.network.inbound_port = 0;
    config.network.inbound_connections = 0;
    config.network.outbound_connections = 0;
    config.network.hosts_file = directory.dir() / "hosts.dat";
    config.network.banlist_file = directory.dir() / "banlist.dat";
    config.network.peers_file = directory.dir() / "peers.dat";
    config.sv2 = loopback_settings();
    SECTION("listener disabled") { config.sv2.enabled = false; }
    SECTION("payout missing") { config.sv2.mining.address.clear(); }
    SECTION("payout on another network") { config.sv2.mining.address = payout_address("bitcoincash"); }

    node::full_node full_node(config);
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, full_node.start(), ::asio::use_future);
    io.run_for(5s);
    REQUIRE(result.wait_for(0s) == std::future_status::ready);
    REQUIRE(result.get() == error::operation_failed);
    REQUIRE(full_node.stopped());
    REQUIRE(full_node.chain().stopped());
}

TEST_CASE("sv2 mining refuses malformed channel messages split across reads", "[sv2 mining]") {
    auto invalid = frame(open_standard_mining_channel{1, "miner", 0.f, easiest_target()});
    SECTION("open channel with the channel bit set") {
        invalid[1] = 0x80;
    }
    SECTION("open channel with a trailing field") {
        invalid.push_back(0);
        ++invalid[3];
    }
    SECTION("negative zero hashrate") {
        invalid = frame(open_extended_mining_channel{1, "proxy", -0.f, easiest_target(), 8});
    }
    SECTION("extended share with an oversized extranonce") {
        invalid = frame(submit_shares_extended{1, 1, 1, 0, 0, 0, data_chunk(32, 7)});
        invalid[message_header::size + 24] = 33;
        invalid.push_back(7);
        ++invalid[3];
    }
    listener_fixture fixture(loopback_settings());
    auto client = fixture.connect(fixture.start());
    require_setup(fixture, *client);
    fixture.write(*client, byte_span(invalid).first(invalid.size() - 1));
    fixture.write(*client, byte_span(invalid).last(1));
    fixture.finish_writing(*client);
    fixture.require_closed(*client);
    fixture.stop();
}

TEST_CASE("sv2 mining channels use the chain template and close independently", "[sv2 mining]") {
    listener_fixture fixture(loopback_settings());
    auto const trunk = seed_chain(fixture, 101);
    auto const view = fixture.chain.chain().chain_view();
    REQUIRE(view);
    auto const& previous = trunk.front().transactions().front();
    auto const tx = test::spend_p2pkh(previous, 0, previous.outputs().front().value() - 1000,
        view->state->enabled_flags());
    REQUIRE(fixture.execute(fixture.chain.chain().organize(
        std::make_shared<domain::message::transaction const>(tx))) == error::success);
    auto client = fixture.connect(fixture.start());
    require_setup(fixture, *client);
    auto const standard = open_standard(fixture, *client, 1);
    auto const extended = open_extended(fixture, *client, 2);
    REQUIRE(standard.channel.channel_id != extended.channel.channel_id);
    REQUIRE(standard.channel.extranonce_prefix != extended.channel.extranonce_prefix);
    REQUIRE(extended.prev.prev_hash == trunk.back().hash());
    REQUIRE(standard.prev.prev_hash == extended.prev.prev_hash);
    REQUIRE(extended.prev.nbits == test::regtest_bits);
    REQUIRE(extended.job.version_rolling_allowed);

    data_chunk const extranonce(extended.channel.extranonce_size, 0x55);
    auto const coinbase_tx = coinbase(extended, extranonce);
    REQUIRE(coinbase_tx.outputs().size() == 1);
    REQUIRE(coinbase_tx.outputs().front().value() == 50 * uint64_t{100000000} + 1000);
    auto const address = domain::wallet::payment_address::parse_from(payout_address(),
        domain::config::network::regtest);
    REQUIRE(address.has_value());
    domain::chain::script const payout(domain::chain::script::to_pay_public_key_hash_pattern(address->hash20()));
    REQUIRE(coinbase_tx.outputs().front().script() == payout);
    REQUIRE(coinbase_tx.inputs().size() == 1);
    auto const prefix = test::coinbase_script(102, 0).first_operation().to_data();
    auto const& script = coinbase_tx.inputs().front().script().bytes();
    REQUIRE(script.size() == prefix.size() + 4 + extended.channel.extranonce_prefix.size() + extranonce.size());
    REQUIRE(std::equal(prefix.begin(), prefix.end(), script.begin()));
    byte_reader generation(byte_span(script).subspan(prefix.size(), 4));
    REQUIRE(generation.read_little_endian<uint32_t>() == extended.job.job_id);
    REQUIRE(std::equal(extended.channel.extranonce_prefix.begin(), extended.channel.extranonce_prefix.end(),
        script.begin() + static_cast<std::ptrdiff_t>(prefix.size() + 4)));
    REQUIRE(std::equal(extranonce.begin(), extranonce.end(), script.end() - static_cast<std::ptrdiff_t>(extranonce.size())));
    REQUIRE(extended.job.merkle_path.size() == 1);
    REQUIRE(extended.job.merkle_path.front() == tx.hash());
    domain::chain::block const candidate({}, {coinbase_tx, tx});
    REQUIRE(merkle_root(extended, extranonce) == candidate.generate_merkle_root());

    fixture.write(*client, frame(close_channel{standard.channel.channel_id, "finished"}));
    fixture.write(*client, frame(update_channel{standard.channel.channel_id, 0.f, easiest_target()}));
    REQUIRE(fixture.receive<update_channel_error>(*client).error_code == "invalid-channel-id");
    auto lower = easiest_target();
    lower.back() = 0x3f;
    fixture.write(*client, frame(update_channel{extended.channel.channel_id, 0.f, lower}));
    auto const target = fixture.receive<set_target>(*client);
    REQUIRE(target.channel_id == extended.channel.channel_id);
    REQUIRE(target.target == lower);
    fixture.write(*client, frame(close_channel{extended.channel.channel_id, "finished"}));
    fixture.write(*client, frame(update_channel{extended.channel.channel_id, 0.f, easiest_target()}));
    fixture.finish_writing(*client);
    REQUIRE(fixture.receive<update_channel_error>(*client).error_code == "invalid-channel-id");
    fixture.require_closed(*client);
    fixture.stop();
}

TEST_CASE("sv2 mining network shares connect blocks and advance every channel", "[sv2 mining]") {
    bool extended_solution = false;
    SECTION("standard share") {}
    SECTION("extended share") { extended_solution = true; }
    listener_fixture fixture(loopback_settings());
    (void)seed_chain(fixture, 5);
    submission_pipeline pipeline(fixture);
    auto const port = fixture.start();
    auto miner = fixture.connect(port);
    require_setup(fixture, *miner);
    auto const standard = open_standard(fixture, *miner, 1);
    auto const extended = open_extended(fixture, *miner, 2);
    auto observer = fixture.connect(port);
    require_setup(fixture, *observer);
    auto const observed = open_standard(fixture, *observer, 1);

    auto slow = fixture.connect(port);
    require_setup(fixture, *slow);
    (void)open_extended(fixture, *slow, 1);
    // Leave the replies unread until backpressure reaches an input write.
    // Bound both the traffic and each wait; one small unread job would not
    // exercise a writer stalled by a miner that stopped receiving.
    auto const update = frame(update_channel{0xffffffffu, 0.f, easiest_target()});
    data_chunk flood;
    for (size_t count = 0; count < 1024; ++count) {
        flood.insert(flood.end(), update.begin(), update.end());
    }
    std::shared_ptr<operation> blocked;
    size_t sent = 0;
    // A write counts as stalled only after half a second without progress: a
    // loaded machine can take longer than that to drain a write that is not
    // stalled at all.
    while (sent + flood.size() <= 8 * 1024 * 1024) {
        blocked = fixture.begin_write(*slow, flood);
        sent += flood.size();
        if ( ! fixture.wait_until([&] { return blocked->done; }, 500ms)) break;
        REQUIRE_FALSE(blocked->error);
    }
    REQUIRE(blocked);
    REQUIRE_FALSE(blocked->done);

    // Neither an idle client nor one stalled in a partial payload can hold up
    // another connection or the block connection path.
    auto silent = fixture.connect(port);
    auto partial = fixture.connect(port);
    auto const bytes = frame(setup());
    fixture.write(*partial, byte_span(bytes).first(message_header::size + 1));

    data_chunk const extranonce(extended.channel.extranonce_size, 0x37);
    auto const solved = extended_solution
        ? solve(extended.job.version, merkle_root(extended, extranonce), extended.prev)
        : solve(standard.job.version, standard.job.merkle_root, standard.prev);
    auto const channel_id = extended_solution ? extended.channel.channel_id : standard.channel.channel_id;
    auto const job_id = extended_solution ? extended.job.job_id : standard.job.job_id;
    if (extended_solution) {
        fixture.write(*miner, frame(submit_shares_extended{channel_id, 42, job_id,
            solved.nonce(), solved.timestamp(), solved.version(), extranonce}));
    } else {
        fixture.write(*miner, frame(submit_shares_standard{channel_id, 42, job_id,
            solved.nonce(), solved.timestamp(), solved.version()}));
    }
    auto const accepted = fixture.receive<submit_shares_success>(*miner);
    REQUIRE(accepted.channel_id == channel_id);
    REQUIRE(accepted.last_sequence_number == 42);
    REQUIRE(accepted.new_submits_accepted_count == 1);
    auto const next_standard = receive_standard(fixture, *miner, standard.channel);
    auto const next_extended = receive_extended(fixture, *miner, extended.channel);
    auto const next_observed = receive_standard(fixture, *observer, observed.channel);
    auto const block_hash = domain::chain::hash(solved);
    REQUIRE(next_standard.prev.prev_hash == block_hash);
    REQUIRE(next_extended.prev.prev_hash == block_hash);
    REQUIRE(next_observed.prev.prev_hash == block_hash);
    REQUIRE(next_standard.job.job_id != standard.job.job_id);
    REQUIRE(next_extended.job.job_id != extended.job.job_id);
    auto const view = fixture.chain.chain().chain_view();
    REQUIRE(view);
    REQUIRE(view->connected_tip_height == 6);
    REQUIRE(view->tip_hash == block_hash);
    REQUIRE_FALSE(blocked->done);

    fixture.write(*miner, frame(submit_shares_standard{standard.channel.channel_id, 43,
        standard.job.job_id, 0, standard.prev.ntime_start, standard.job.version}));
    fixture.write(*miner, frame(submit_shares_extended{extended.channel.channel_id, 44,
        extended.job.job_id, 0, extended.prev.ntime_start, extended.job.version, extranonce}));
    fixture.finish_writing(*miner);
    auto const stale_standard = fixture.receive<submit_shares_error>(*miner);
    auto const stale_extended = fixture.receive<submit_shares_error>(*miner);
    REQUIRE(stale_standard.sequence_number == 43);
    REQUIRE(stale_standard.error_code == "stale-share");
    REQUIRE(stale_extended.sequence_number == 44);
    REQUIRE(stale_extended.error_code == "stale-share");
    fixture.require_closed(*miner);
    fixture.stop();
    std::error_code ignored;
    slow->close(ignored);
    REQUIRE(fixture.wait_until([&] { return blocked->done; }));
    fixture.require_closed(*silent);
    fixture.require_closed(*partial);
}

TEST_CASE("sv2 mining keeps an open channel through a chain transition", "[sv2 mining]") {
    listener_fixture fixture(loopback_settings());
    auto const trunk = seed_chain(fixture, 5);
    auto client = fixture.connect(fixture.start());
    require_setup(fixture, *client);
    auto& chain = fixture.chain.chain();
    REQUIRE(chain.begin_transition());
    fixture.write(*client, frame(open_extended_mining_channel{1, "proxy", 0.f,
        easiest_target(), 8}));
    // An answer to the next message proves the open was processed while
    // template capture was closed, rather than relying on a scheduling delay.
    fixture.write(*client, frame(update_channel{0xffffffffu, 0.f, easiest_target()}));
    auto const barrier = fixture.receive<update_channel_error>(*client);
    REQUIRE(barrier.channel_id == 0xffffffffu);
    REQUIRE(barrier.error_code == "invalid-channel-id");
    chain.end_transition();
    auto const opened = fixture.receive<open_extended_mining_channel_success>(*client);
    auto const job = receive_extended(fixture, *client, opened);
    REQUIRE(job.prev.prev_hash == trunk.back().hash());
    fixture.finish_writing(*client);
    fixture.require_closed(*client);
    fixture.stop();
}

TEST_CASE("sv2 mining shares the authenticated Noise transport", "[sv2 mining]") {
    noise_listener_fixture fixture;
    (void)seed_chain(fixture, 5);
    auto const port = fixture.start();
    auto partial = fixture.connect(port);
    fixture.write(*partial, data_chunk{1, 2, 3});
    auto client = fixture.connect(port);
    auto transport = fixture.handshake(*client);
    fixture.send(*client, transport, setup());
    REQUIRE(fixture.receive<setup_connection_success>(*client, transport).used_version == 2);
    fixture.send(*client, transport, open_standard_mining_channel{1, "miner", 0.f,
        easiest_target()});
    auto const channel = fixture.receive<open_standard_mining_channel_success>(*client, transport);
    auto const job = fixture.receive<new_mining_job>(*client, transport);
    auto const prev = fixture.receive<mining_set_new_prev_hash>(*client, transport);
    REQUIRE(channel.channel_id == job.channel_id);
    REQUIRE(prev.channel_id == channel.channel_id);
    REQUIRE(prev.job_id == job.job_id);
    REQUIRE_FALSE(job.ntime_start.has_value());

    // Authenticated framing has the same strict channel flag rule. The last
    // tag arrives in a separate read; it must not make this payload acceptable.
    auto invalid = frame(submit_shares_standard{channel.channel_id, 1, job.job_id,
        0, prev.ntime_start, job.version});
    invalid[1] = 0;
    auto const encrypted = transport.encrypt_frame(invalid);
    REQUIRE(encrypted.has_value());
    fixture.write(*client, byte_span(*encrypted).first(encrypted->size() - 1));
    fixture.write(*client, byte_span(*encrypted).last(1));
    fixture.finish_writing(*client);
    fixture.require_closed(*client);
    fixture.stop();
    fixture.require_closed(*partial);
}

