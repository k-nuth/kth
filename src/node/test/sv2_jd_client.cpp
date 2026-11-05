// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "sync_harness.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/read.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <kth/domain/chain/compact.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/wallet/cashaddr.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/mining/work.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/jd_client.hpp>
#include <kth/node/sv2/jd_messages.hpp>
#include <kth/node/sv2/mining_messages.hpp>
#include <kth/node/sv2/noise.hpp>
#include <kth/node/sv2/template_provider.hpp>

using namespace kth;
using namespace kth::node::sv2;
using namespace std::chrono_literals;

namespace {

noise_keypair keypair(uint8_t value) {
    hash_digest secret{};
    secret.back() = value;
    auto const key = noise_keypair::create(secret, {});
    REQUIRE(key);
    return *key;
}

template <typename Message>
constexpr uint16_t extension() {
    return framed_extension_type<Message>();
}

template <typename Message>
data_chunk frame(Message const& message) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{extension<Message>(), Message::message_type,
        static_cast<uint32_t>(payload.size())});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

template <typename Message>
Message decode(data_chunk const& bytes) {
    byte_reader source(bytes);
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

struct operation {
    bool done = false;
    std::error_code error;
    size_t size = 0;
    data_chunk bytes;
};

// There are no worker threads. Each pending operation is driven only until a
// fixed deadline, including when an assertion unwinds and cleanup cancels it.
struct io_driver {
    ::asio::io_context io;

    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds limit = 5s) {
        auto const end = std::chrono::steady_clock::now() + limit;
        while ( ! predicate() && std::chrono::steady_clock::now() < end) {
            io.restart();
            io.run_for(1ms);
        }
        return predicate();
    }

    template <typename T>
    void await(std::future<T>& future) {
        REQUIRE(wait_until([&] { return future.wait_for(0s) == std::future_status::ready; }));
    }

    std::shared_ptr<operation> begin_read(::asio::ip::tcp::socket& socket, size_t size,
        bool exact = true) {
        auto state = std::make_shared<operation>();
        state->bytes.resize(size);
        auto complete = [state](std::error_code error, size_t count) {
            state->error = error;
            state->size = count;
            state->bytes.resize(count);
            state->done = true;
        };
        if (exact) ::asio::async_read(socket, ::asio::buffer(state->bytes), complete);
        else socket.async_read_some(::asio::buffer(state->bytes), complete);
        return state;
    }

    data_chunk read(::asio::ip::tcp::socket& socket, size_t size, bool exact = true) {
        auto const state = begin_read(socket, size, exact);
        REQUIRE(wait_until([&] { return state->done; }));
        REQUIRE_FALSE(state->error);
        return state->bytes;
    }

    void write(::asio::ip::tcp::socket& socket, data_chunk bytes) {
        auto state = std::make_shared<operation>();
        state->bytes = std::move(bytes);
        ::asio::async_write(socket, ::asio::buffer(state->bytes),
            [state](std::error_code error, size_t count) {
                state->error = error;
                state->size = count;
                state->done = true;
            });
        REQUIRE(wait_until([&] { return state->done; }));
        REQUIRE_FALSE(state->error);
        REQUIRE(state->size == state->bytes.size());
    }

    void eof(::asio::ip::tcp::socket& socket) {
        auto const state = begin_read(socket, 1);
        REQUIRE(wait_until([&] { return state->done; }));
        REQUIRE(state->size == 0);
        REQUIRE(state->error == ::asio::error::eof);
    }
};

struct fake_upstream {
    io_driver& driver;
    ::asio::ip::tcp::acceptor acceptor;
    ::asio::ip::tcp::socket socket;
    std::shared_ptr<operation> accepted = std::make_shared<operation>();
    noise_keypair authority;
    std::optional<noise_transport> transport;
    std::deque<data_chunk> frames;
    uint16_t port = 0;

    fake_upstream(io_driver& driver, uint8_t authority_secret)
        : driver(driver)
        , acceptor(driver.io)
        , socket(driver.io)
        , authority(keypair(authority_secret))
    {
        std::error_code error;
        ::asio::ip::tcp::endpoint const endpoint(::asio::ip::address_v4::loopback(), 0);
        acceptor.open(endpoint.protocol(), error);
        REQUIRE_FALSE(error);
        acceptor.bind(endpoint, error);
        REQUIRE_FALSE(error);
        acceptor.listen(::asio::ip::tcp::socket::max_listen_connections, error);
        REQUIRE_FALSE(error);
        auto const bound = acceptor.local_endpoint(error);
        REQUIRE_FALSE(error);
        port = bound.port();
        REQUIRE(port != 0);
        acceptor.async_accept(socket, [state = accepted](std::error_code error) {
            state->error = error;
            state->done = true;
        });
    }

    ~fake_upstream() {
        std::error_code ignored;
        acceptor.close(ignored);
        socket.close(ignored);
    }

    void handshake() {
        REQUIRE(driver.wait_until([&] { return accepted->done; }));
        REQUIRE_FALSE(accepted->error);
        auto const first = driver.read(socket, noise_initiator::flight_size);
        auto const now = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        auto const static_key = keypair(3);
        auto const certificate = noise_certificate::sign(authority.secret(), static_key.public_key(),
            now - 60, now + 600);
        REQUIRE(certificate);
        noise_responder responder(keypair(2), static_key, *certificate);
        auto const second = responder.respond(first);
        REQUIRE(second);
        driver.write(socket, *second);
        REQUIRE(responder.keys());
        transport.emplace(*responder.keys(), false);
    }

    template <typename Message>
    Message receive() {
        auto const end = std::chrono::steady_clock::now() + 5s;
        while (frames.empty() && std::chrono::steady_clock::now() < end) {
            auto const bytes = driver.read(socket, 8192, false);
            auto parsed = transport->feed(bytes);
            REQUIRE(parsed);
            for (auto& message : *parsed) frames.push_back(std::move(message));
        }
        REQUIRE_FALSE(frames.empty());
        auto bytes = std::move(frames.front());
        frames.pop_front();
        return decode<Message>(bytes);
    }

    template <typename Message>
    void send(Message const& message) {
        send_plain(frame(message));
    }

    void send_plain(data_chunk plain) {
        auto const bytes = transport->encrypt_frame(plain);
        REQUIRE(bytes);
        // A protocol reply can straddle reads just like its encrypted tags.
        driver.write(socket, data_chunk(bytes->begin(), bytes->end() - 1));
        driver.write(socket, data_chunk(bytes->end() - 1, bytes->end()));
    }

    void finish_writing() {
        std::error_code error;
        socket.shutdown(::asio::ip::tcp::socket::shutdown_send, error);
        REQUIRE_FALSE(error);
    }
};

std::string payout_address() {
    data_chunk bytes(21, 0x11);
    bytes[0] = 0;
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
    if (bits > 0) groups.push_back(uint8_t((accumulator << (5 - bits)) & 31));
    return domain::wallet::cashaddr::encode("bchreg", groups);
}

node::sv2_settings settings(fake_upstream const& pool, fake_upstream const& jds) {
    node::sv2_settings value;
    value.enabled = true;
    value.noise = false;
    value.port = 0;
    value.mining.enabled = true;
    value.mining.address = payout_address();
    value.handshake_timeout_seconds = 3;
    value.jd.enabled = true;
    value.jd.mining_address = "127.0.0.1:" + std::to_string(pool.port);
    value.jd.jds_address = "127.0.0.1:" + std::to_string(jds.port);
    value.jd.mining_authority_public_key = noise_authority_key(pool.authority.public_key());
    value.jd.jds_authority_public_key = noise_authority_key(jds.authority.public_key());
    value.jd.user_identity = "account.worker";
    value.jd.mode = "full-template";
    value.jd.response_timeout_seconds = 3;
    return value;
}

::asio::awaitable<void> run_client(std::shared_ptr<jd_client> client) {
    co_await client->run();
}

::asio::awaitable<void> await_client_ready(std::shared_ptr<jd_client> client) {
    co_await client->wait_until_ready();
}

::asio::awaitable<int> receive_publication(std::shared_ptr<jd_client::publication_channel> channel) {
    co_return co_await channel->async_receive(::asio::use_awaitable);
}

struct client_fixture : io_driver {
    fake_upstream pool{*this, 4};
    fake_upstream jds{*this, 5};
    std::shared_ptr<jd_client> client;
    std::future<void> result;
    std::future<void> ready;

    ~client_fixture() {
        if (client) client->stop();
        std::error_code ignored;
        pool.socket.close(ignored);
        jds.socket.close(ignored);
        if (result.valid()) {
            (void)wait_until([&] { return result.wait_for(0s) == std::future_status::ready; }, 1s);
        }
    }

    void start(node::sv2_settings const& configuration, blockchain::block_chain* chain = nullptr) {
        client = std::make_shared<jd_client>(configuration, io.get_executor(), chain);
        ready = ::asio::co_spawn(io, await_client_ready(client), ::asio::use_future);
        result = ::asio::co_spawn(io, run_client(client), ::asio::use_future);
    }

    void setup() {
        pool.handshake();
        jds.handshake();
        auto const mining = pool.receive<setup_connection>();
        REQUIRE(mining.protocol == setup_connection::protocol_mining);
        REQUIRE(mining.flags == (mining_flag_requires_work_selection | mining_flag_requires_version_rolling));
        REQUIRE(mining.min_version == 2);
        REQUIRE(mining.max_version == 2);
        REQUIRE(mining.endpoint_host == "127.0.0.1");
        REQUIRE(mining.endpoint_port == pool.port);
    }

    open_extended_mining_channel open() {
        pool.send(setup_connection_success{2, mining_flag_requires_extended_channels});
        auto const request = pool.receive<open_extended_mining_channel>();
        REQUIRE(request.user_identity == "account.worker");
        REQUIRE(request.min_extranonce_size == jd_session::min_extranonce_size);
        return request;
    }

    void accept(open_extended_mining_channel const& request) {
        hash_digest target;
        target.fill(0xff);
        pool.send(open_extended_mining_channel_success{request.request_id, 7, target,
            jd_session::min_extranonce_size, data_chunk(8, 0x12), 8});
    }

    void finished(bool success) {
        await(result);
        REQUIRE_NOTHROW(result.get());
        await(ready);
        if (success) REQUIRE_NOTHROW(ready.get());
        else REQUIRE_THROWS_AS(ready.get(), std::system_error);
    }
};

template <typename Message>
Message receive_plain(io_driver& driver, ::asio::ip::tcp::socket& socket) {
    auto bytes = driver.read(socket, message_header::size);
    auto const header = from_data_chunk<message_header>(bytes);
    REQUIRE(header);
    REQUIRE(header->msg_length <= 65536);
    auto const payload = driver.read(socket, header->msg_length);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return decode<Message>(bytes);
}

struct plain_frame {
    message_header header;
    data_chunk bytes;
};

plain_frame read_plain_frame(io_driver& driver, ::asio::ip::tcp::socket& socket) {
    auto bytes = driver.read(socket, message_header::size);
    auto const header = from_data_chunk<message_header>(bytes);
    REQUIRE(header);
    REQUIRE(header->msg_length <= 65536);
    auto const payload = driver.read(socket, header->msg_length);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return {*header, std::move(bytes)};
}

void solo_job(io_driver& driver, uint16_t port, domain::chain::script const& payout) {
    ::asio::ip::tcp::socket miner(driver.io);
    auto connected = std::make_shared<operation>();
    miner.async_connect({::asio::ip::address_v4::loopback(), port},
        [connected](std::error_code error) { connected->error = error; connected->done = true; });
    REQUIRE(driver.wait_until([&] { return connected->done; }));
    REQUIRE_FALSE(connected->error);
    setup_connection setup;
    setup.protocol = setup_connection::protocol_mining;
    setup.min_version = 2;
    setup.max_version = 2;
    hash_digest target;
    target.fill(0xff);
    auto request = frame(setup);
    // With JD enabled a downstream channel rolls JD's fixed reserve.
    auto const channel = frame(open_extended_mining_channel{1, "miner", 0.f, target,
        node::mining::jd_downstream_extranonce_size});
    request.insert(request.end(), channel.begin(), channel.end());
    driver.write(miner, std::move(request));
    std::error_code error;
    miner.shutdown(::asio::ip::tcp::socket::shutdown_send, error);
    REQUIRE_FALSE(error);
    REQUIRE(receive_plain<setup_connection_success>(driver, miner).used_version == 2);
    auto const opened = receive_plain<open_extended_mining_channel_success>(driver, miner);
    auto const job = receive_plain<new_extended_mining_job>(driver, miner);
    auto const previous = receive_plain<mining_set_new_prev_hash>(driver, miner);
    REQUIRE(job.channel_id == opened.channel_id);
    REQUIRE(previous.job_id == job.job_id);
    auto coinbase = job.coinbase_tx_prefix;
    coinbase.insert(coinbase.end(), opened.extranonce_prefix.begin(), opened.extranonce_prefix.end());
    coinbase.insert(coinbase.end(), opened.extranonce_size, 0x44);
    coinbase.insert(coinbase.end(), job.coinbase_tx_suffix.begin(), job.coinbase_tx_suffix.end());
    byte_reader source(coinbase);
    auto const transaction = domain::chain::transaction::from_data(source, true);
    REQUIRE(transaction);
    REQUIRE(source.is_exhausted());
    REQUIRE(transaction->outputs().size() == 1);
    REQUIRE(transaction->outputs().front().script() == payout);
    REQUIRE(transaction->outputs().front().value() == 50 * uint64_t{100000000});
    driver.eof(miner);
}

::asio::awaitable<void> run_provider(std::shared_ptr<template_provider> provider,
    std::shared_ptr<test::chain_fixture> chain) {
    // Retain the chain through cancellation as well as the listener. A failing
    // assertion can otherwise destroy it while socket completions still run.
    (void)chain;
    co_await provider->run();
}

uint64_t const regtest_subsidy = 50 * uint64_t{100000000};

void seed_regtest(test::chain_fixture& chain) {
    auto const block = test::mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - test::block_spacing, 0, {}, 0);
    std::vector<domain::chain::block> const blocks{block};
    REQUIRE(chain.organizer().add_headers(test::headers_of(blocks)).headers_added == 1);
    test::persist_headers(chain, blocks, 1);
    test::connect_bodies(chain, blocks, 1);
}

std::shared_ptr<test::chain_fixture> started_chain(char const* tag) {
    auto chain = std::make_shared<test::chain_fixture>(tag, 0);
    REQUIRE(chain->created());
    REQUIRE(chain->start());
    seed_regtest(*chain);
    return chain;
}

domain::chain::script pool_script() {
    short_hash hash{};
    hash.fill(0x22);
    return domain::chain::script(domain::chain::script::to_pay_public_key_hash_pattern(hash));
}

domain::chain::output::list pool_output_list() {
    return {{0, pool_script(), std::nullopt},
        {0, domain::chain::script(domain::chain::script::to_null_data_pattern(
            data_chunk{0x24})), std::nullopt}};
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

struct downstream_work {
    data_chunk prefix;
    uint16_t rollable = 0;
    new_extended_mining_job job;
    mining_set_new_prev_hash prev;
};

downstream_work read_activated(io_driver& driver, ::asio::ip::tcp::socket& socket,
    data_chunk prefix, open_extended_mining_channel_success const& opened) {
    std::optional<new_extended_mining_job> job;
    for (int frame_count = 0; frame_count < 6; ++frame_count) {
        auto bytes = driver.read(socket, message_header::size);
        auto const header = from_data_chunk<message_header>(bytes);
        REQUIRE(header);
        REQUIRE(header->msg_length <= 65536);
        auto const payload = driver.read(socket, header->msg_length);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        if (header->msg_type == set_extranonce_prefix::message_type) {
            auto const message = decode<set_extranonce_prefix>(bytes);
            REQUIRE(message.channel_id == opened.channel_id);
            prefix = message.extranonce_prefix;
            continue;
        }
        if (header->msg_type == submit_shares_success::message_type) continue;
        if (header->msg_type == new_extended_mining_job::message_type) {
            job = decode<new_extended_mining_job>(bytes);
            REQUIRE(job->channel_id == opened.channel_id);
            REQUIRE_FALSE(job->ntime_start.has_value());
            continue;
        }
        if (header->msg_type == mining_set_new_prev_hash::message_type) {
            auto const prev = decode<mining_set_new_prev_hash>(bytes);
            REQUIRE(job);
            REQUIRE(prev.channel_id == opened.channel_id);
            REQUIRE(prev.job_id == job->job_id);
            return {std::move(prefix), opened.extranonce_size, std::move(*job), prev};
        }
        FAIL("unexpected downstream frame " << static_cast<unsigned>(header->msg_type));
    }
    FAIL("downstream job was not activated");
    return {};
}

domain::chain::transaction coinbase_from(downstream_work const& work) {
    auto raw = work.job.coinbase_tx_prefix;
    raw.insert(raw.end(), work.prefix.begin(), work.prefix.end());
    raw.insert(raw.end(), work.rollable, 0x44);
    raw.insert(raw.end(), work.job.coinbase_tx_suffix.begin(), work.job.coinbase_tx_suffix.end());
    byte_reader source(raw);
    auto tx = domain::chain::transaction::from_data(source, true);
    REQUIRE(tx);
    REQUIRE(source.is_exhausted());
    return std::move(*tx);
}

bool pays(domain::chain::transaction const& tx, domain::chain::script const& script, size_t outputs) {
    return tx.outputs().size() == outputs
        && tx.outputs().front().script() == script
        && tx.outputs().front().value() == regtest_subsidy
        && (outputs == 1 || tx.outputs().back().value() == 0);
}

struct miner_link {
    io_driver& driver;
    ::asio::ip::tcp::socket socket;
    open_extended_mining_channel_success opened;
    downstream_work work;

    miner_link(io_driver& driver, uint16_t port)
        : driver(driver)
        , socket(driver.io) {
        auto connected = std::make_shared<operation>();
        socket.async_connect({::asio::ip::address_v4::loopback(), port},
            [connected](std::error_code error) {
                connected->error = error;
                connected->done = true;
            });
        REQUIRE(driver.wait_until([&] { return connected->done; }));
        REQUIRE_FALSE(connected->error);
        setup_connection setup;
        setup.protocol = setup_connection::protocol_mining;
        setup.min_version = 2;
        setup.max_version = 2;
        hash_digest target;
        target.fill(0xff);
        driver.write(socket, frame(setup));
        REQUIRE(receive_plain<setup_connection_success>(driver, socket).used_version == 2);
        // With JD enabled a downstream channel rolls JD's fixed reserve.
        driver.write(socket, frame(open_extended_mining_channel{1, "miner", 0.f, target,
            node::mining::jd_downstream_extranonce_size}));
        opened = receive_plain<open_extended_mining_channel_success>(driver, socket);
        REQUIRE(opened.request_id == 1);
        REQUIRE(opened.extranonce_size == node::mining::jd_downstream_extranonce_size);
        work = read_activated(driver, socket, opened.extranonce_prefix, opened);
    }

    ~miner_link() {
        std::error_code ignored;
        socket.close(ignored);
    }

    downstream_work next() {
        work = read_activated(driver, socket, work.prefix, opened);
        return work;
    }
};

struct provider_run {
    io_driver& driver;
    std::shared_ptr<template_provider> provider;
    std::future<void> result;
    uint16_t port = 0;

    provider_run(io_driver& driver, node::sv2_settings const& configuration,
        std::shared_ptr<test::chain_fixture> chain)
        : driver(driver) {
        provider = std::make_shared<template_provider>(configuration, driver.io.get_executor(),
            chain->chain(), domain::config::network::regtest);
        result = ::asio::co_spawn(driver.io, run_provider(provider, std::move(chain)), ::asio::use_future);
        auto listening = ::asio::co_spawn(driver.io, provider->wait_until_listening(), ::asio::use_future);
        REQUIRE(driver.wait_until([&] {
            return listening.wait_for(0s) == std::future_status::ready;
        }));
        port = listening.get();
        REQUIRE(port != 0);
    }

    ~provider_run() {
        if (provider) provider->stop();
        if (result.valid()) {
            (void)driver.wait_until([&] {
                return result.wait_for(0s) == std::future_status::ready;
            }, 2s);
        }
    }
};

// The orchestrator connects a share the mining session accepted as a block.
// It has to outlive the provider's stop and stop before the fixture's io.
struct submission_pipeline {
    struct node_state {
        kth::network::settings config;
        node::p2p_node network;

        static kth::network::settings settings(test::chain_fixture& chain) {
            kth::network::settings config;
            config.identifier = 0xdab5bffa;
            config.protocol_maximum = 70015;
            config.inbound_port = 0;
            config.inbound_connections = 0;
            config.outbound_connections = 0;
            config.threads = 1;
            config.hosts_file = chain.dir() / "hosts.dat";
            config.banlist_file = chain.dir() / "banlist.dat";
            config.peers_file = chain.dir() / "peers.dat";
            return config;
        }

        explicit node_state(test::chain_fixture& chain)
            : config(settings(chain))
            , network(config)
        {}
    };

    io_driver& driver;
    std::shared_ptr<node_state> state;
    std::future<void> done;

    submission_pipeline(io_driver& driver, test::chain_fixture& chain)
        : driver(driver)
        , state(std::make_shared<node_state>(chain)) {
        auto started = ::asio::co_spawn(driver.io, state->network.start(), ::asio::use_future);
        REQUIRE(driver.wait_until([&] {
            return started.wait_for(0s) == std::future_status::ready;
        }, 60s));
        REQUIRE(started.get() == error::success);
        done = ::asio::co_spawn(driver.io,
            node::sync::sync_orchestrator(chain.chain(), chain.organizer(), state->network,
                domain::config::network::regtest,
                [](std::string const& reason) { FAIL("sync failed: " << reason); }),
            ::asio::use_future);
    }

    ~submission_pipeline() {
        state->network.stop();
        if (done.valid()) {
            (void)driver.wait_until([&] {
                return done.wait_for(0s) == std::future_status::ready;
            }, 30s);
        }
    }
};

// A second JDS accept speaks Noise with the same authority as fake_upstream::handshake.
noise_transport handshake_responder(io_driver& driver, ::asio::ip::tcp::socket& socket,
    noise_keypair const& authority) {
    auto const first = driver.read(socket, noise_initiator::flight_size);
    auto const now = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    auto const static_key = keypair(3);
    auto const certificate = noise_certificate::sign(authority.secret(), static_key.public_key(),
        now - 60, now + 600);
    REQUIRE(certificate);
    noise_responder responder(keypair(2), static_key, *certificate);
    auto const second = responder.respond(first);
    REQUIRE(second);
    driver.write(socket, *second);
    REQUIRE(responder.keys());
    return noise_transport(*responder.keys(), false);
}

template <typename Message>
Message receive_encrypted(io_driver& driver, ::asio::ip::tcp::socket& socket,
    noise_transport& transport) {
    std::deque<data_chunk> frames;
    auto const end = std::chrono::steady_clock::now() + 5s;
    while (frames.empty() && std::chrono::steady_clock::now() < end) {
        auto const bytes = driver.read(socket, 8192, false);
        auto parsed = transport.feed(bytes);
        REQUIRE(parsed);
        for (auto& message : *parsed) frames.push_back(std::move(message));
    }
    REQUIRE_FALSE(frames.empty());
    return decode<Message>(frames.front());
}

void send_encrypted(io_driver& driver, ::asio::ip::tcp::socket& socket,
    noise_transport& transport, data_chunk plain) {
    auto const bytes = transport.encrypt_frame(plain);
    REQUIRE(bytes);
    // Same split as fake_upstream::send: a reply can straddle reads.
    driver.write(socket, data_chunk(bytes->begin(), bytes->end() - 1));
    driver.write(socket, data_chunk(bytes->end() - 1, bytes->end()));
}

// Full-template also asks for BCH large templates; coinbase-only has no list to page.
std::vector<uint16_t> requested_extensions(bool full_template) {
    if (full_template) return {requested_payout_outputs_extension, large_templates_extension};
    return {requested_payout_outputs_extension};
}

void answer_extensions(client_fixture& fixture, bool grant, bool full_template = true) {
    auto const request = fixture.jds.receive<request_extensions>();
    REQUIRE(request.request_id == 1);
    REQUIRE(request.requested_extensions == requested_extensions(full_template));
    if (grant) {
        fixture.jds.send(request_extensions_success{request.request_id,
            {requested_payout_outputs_extension}});
        return;
    }
    fixture.jds.send(request_extensions_error{request.request_id, {}, {}});
}

void handshake_upstreams(client_fixture& fixture, bool full, hash_digest const* target = nullptr,
    bool grant = false) {
    fixture.pool.handshake();
    fixture.jds.handshake();
    (void)fixture.pool.receive<setup_connection>();
    auto const declaration = fixture.jds.receive<setup_connection>();
    REQUIRE(declaration.protocol == setup_connection::protocol_job_declaration);
    REQUIRE(declaration.flags == (full ? 1u : 0u));
    fixture.jds.send(setup_connection_success{2, 0});
    answer_extensions(fixture, grant, full);
    auto const request = fixture.open();
    if (target == nullptr) {
        fixture.accept(request);
        return;
    }
    fixture.pool.send(open_extended_mining_channel_success{request.request_id, 7, *target,
        jd_session::min_extranonce_size, data_chunk(8, 0x12), 8});
}

struct declared_template {
    declare_mining_job declaration;
    bool full = false;
    set_custom_mining_job custom;
    downstream_work pooled;
};

// `finish` false stops after DeclareMiningJob, before its Success. The pool
// coinbase is already published from the token, so the caller can refuse the
// declaration and watch the next job return to the solo payout.
declared_template declare_local_template(client_fixture& fixture, miner_link& miner,
    bool full, bool finish = true) {
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    REQUIRE(allocation.user_identifier == "account.worker");
    REQUIRE(allocation.request_id != 0);
    // Coinbase-only must not follow the token with DeclareMiningJob. Arm the
    // read before the token succeeds, while the JDS socket is still idle.
    std::shared_ptr<operation> quiet;
    if ( ! full) quiet = fixture.begin_read(fixture.jds.socket, 1);
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    declare_mining_job declaration{};
    if (full) {
        declaration = fixture.jds.receive<declare_mining_job>();
        REQUIRE(declaration.request_id == allocation.request_id);
        REQUIRE(declaration.mining_job_token == data_chunk{1, 2, 3});
        REQUIRE(declaration.excess_data.empty());
        REQUIRE(declaration.wtxid_list.empty());
        if ( ! finish) {
            auto const pooled = miner.next();
            REQUIRE(pooled.prefix.size() == 8 + node::mining::jd_allocation_size);
            REQUIRE(pooled.job.coinbase_tx_prefix == declaration.coinbase_tx_prefix);
            REQUIRE(pooled.job.coinbase_tx_suffix == declaration.coinbase_tx_suffix);
            REQUIRE(pays(coinbase_from(pooled), pool_script(), 2));
            return {std::move(declaration), full, {}, pooled};
        }
        fixture.jds.send(provide_missing_transactions{declaration.request_id, {}});
        auto const supplied = fixture.jds.receive<provide_missing_transactions_success>();
        REQUIRE(supplied.request_id == declaration.request_id);
        REQUIRE(supplied.transaction_list.empty());
        fixture.jds.send(declare_mining_job_success{declaration.request_id, {9, 8, 7}});
    }
    auto const custom = fixture.pool.receive<set_custom_mining_job>();
    if (quiet) {
        CHECK_FALSE(quiet->done);
        // Join the silence read before callers read the JDS socket again.
        std::error_code error;
        fixture.jds.socket.cancel(error);
        REQUIRE_FALSE(error);
        REQUIRE(fixture.wait_until([&] { return quiet->done; }));
    }
    REQUIRE(custom.channel_id == 7);
    REQUIRE(custom.request_id == allocation.request_id);
    REQUIRE(custom.mining_job_token == (full ? data_chunk{9, 8, 7} : data_chunk{1, 2, 3}));
    REQUIRE(custom.coinbase_tx_version == 1);
    REQUIRE(custom.coinbase_tx_input_sequence == 0xffffffffu);
    REQUIRE(custom.coinbase_tx_locktime == 0);
    REQUIRE(custom.merkle_path.empty());
    auto const paid = read_outputs(custom.coinbase_tx_outputs);
    REQUIRE(paid.size() == 2);
    REQUIRE(paid.front().value() == regtest_subsidy);
    REQUIRE(paid.front().script() == pool_script());
    REQUIRE(paid.back().value() == 0);
    auto const pooled = miner.next();
    REQUIRE(pooled.prefix.size() == 8 + node::mining::jd_allocation_size);
    REQUIRE(std::equal(pooled.prefix.begin(), pooled.prefix.begin() + 8,
        data_chunk(8, 0x12).begin()));
    REQUIRE(pooled.job.version == custom.version);
    REQUIRE(pooled.prev.prev_hash == custom.prev_hash);
    REQUIRE(pooled.prev.ntime_start == custom.ntime_start);
    REQUIRE(pooled.prev.nbits == custom.nbits);
    REQUIRE(pooled.job.merkle_path == custom.merkle_path);
    if (full) {
        REQUIRE(pooled.job.coinbase_tx_prefix == declaration.coinbase_tx_prefix);
        REQUIRE(pooled.job.coinbase_tx_suffix == declaration.coinbase_tx_suffix);
    }
    auto const tx = coinbase_from(pooled);
    REQUIRE(tx.inputs().size() == 1);
    auto const& script = tx.inputs().front().script().bytes();
    REQUIRE(script.size() >= custom.coinbase_prefix.size());
    REQUIRE(std::equal(custom.coinbase_prefix.begin(), custom.coinbase_prefix.end(), script.begin()));
    REQUIRE(pays(tx, pool_script(), 2));
    return {std::move(declaration), full, custom, pooled};
}

hash_digest share_hash(downstream_work const& work, data_chunk const& extranonce, uint32_t nonce) {
    auto raw = work.job.coinbase_tx_prefix;
    raw.insert(raw.end(), work.prefix.begin(), work.prefix.end());
    raw.insert(raw.end(), extranonce.begin(), extranonce.end());
    raw.insert(raw.end(), work.job.coinbase_tx_suffix.begin(), work.job.coinbase_tx_suffix.end());
    byte_reader source(raw);
    auto tx = domain::chain::transaction::from_data(source, true);
    REQUIRE(tx);
    REQUIRE(source.is_exhausted());
    auto root = tx->hash();
    for (auto const& sibling : work.job.merkle_path) {
        root = bitcoin_hash(build_chunk({root, sibling}));
    }
    domain::chain::header const header(work.job.version, work.prev.prev_hash, root,
        work.prev.ntime_start, work.prev.nbits, nonce);
    return domain::chain::hash(header);
}

bool meets_network_target(hash_digest const& hash, uint32_t bits) {
    auto const target = domain::chain::compact::from_compact(bits);
    REQUIRE(target);
    return to_uint256(hash) <= target->big();
}

uint32_t find_nonce(downstream_work const& work, data_chunk const& extranonce, bool block) {
    for (uint32_t nonce = 0; nonce < 10000; ++nonce) {
        auto const hash = share_hash(work, extranonce, nonce);
        if (meets_network_target(hash, work.prev.nbits) == block) return nonce;
    }
    FAIL("no regtest nonce matched the requested proof of work");
    return 0;
}

// A one-byte read that must not complete. Cancel it afterwards so the next
// read on this socket starts at a frame boundary. Used when a share must
// still be cached rather than forwarded.
void expect_idle(io_driver& driver, ::asio::ip::tcp::socket& socket) {
    auto quiet = driver.begin_read(socket, 1);
    REQUIRE_FALSE(driver.wait_until([&] { return quiet->done; }, 300ms));
    std::error_code error;
    socket.cancel(error);
    REQUIRE_FALSE(error);
    REQUIRE(driver.wait_until([&] { return quiet->done; }));
}

void require_no_reconnect(client_fixture& fixture) {
    ::asio::ip::tcp::socket retry(fixture.io);
    auto accepted = std::make_shared<operation>();
    fixture.pool.acceptor.async_accept(retry, [accepted](std::error_code error) {
        accepted->error = error;
        accepted->done = true;
    });
    ::asio::ip::tcp::socket jd_retry(fixture.io);
    auto jd_accepted = std::make_shared<operation>();
    fixture.jds.acceptor.async_accept(jd_retry, [jd_accepted](std::error_code error) {
        jd_accepted->error = error;
        jd_accepted->done = true;
    });
    CHECK_FALSE(fixture.wait_until([&] { return accepted->done; }, 50ms));
    CHECK_FALSE(jd_accepted->done);
}

// Swap the default logger for one that keeps what was written, including debug.
struct captured_log {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
    std::shared_ptr<spdlog::logger> installed;
    std::shared_ptr<spdlog::logger> previous;

    captured_log()
        : sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(1024))
        , installed(std::make_shared<spdlog::logger>("captured", sink))
        , previous(spdlog::default_logger()) {
        installed->set_level(spdlog::level::trace);
        spdlog::set_default_logger(installed);
    }

    ~captured_log() { spdlog::set_default_logger(previous); }

    captured_log(captured_log const&) = delete;
    captured_log& operator=(captured_log const&) = delete;

    [[nodiscard]] size_t count(std::string_view needle) const {
        size_t n = 0;
        for (auto const& line : sink->last_formatted()) {
            if (line.find(needle) != std::string::npos) ++n;
        }
        return n;
    }
};

// SubmitShares.Success is 20 bytes. A 4-byte payload is a complete frame whose
// body still fails from_data, so the close comes from the parser rather than
// from the noise layer waiting for bytes that were never declared.
data_chunk truncated_share_success() {
    data_chunk payload(4, 0);
    payload[0] = 7;
    auto bytes = to_data_chunk(message_header{message_header::channel_msg_flag,
        submit_shares_success::message_type, static_cast<uint32_t>(payload.size())});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

// SubmitShares.Error is two U32s and a U8 string, at least 9 bytes. Four bytes
// is a complete frame whose body still fails from_data, so the close comes
// from the parser rather than from the noise layer waiting for bytes that
// were never declared.
data_chunk truncated_share_error() {
    data_chunk payload(4, 0);
    payload[0] = 7;
    auto bytes = to_data_chunk(message_header{message_header::channel_msg_flag,
        submit_shares_error::message_type, static_cast<uint32_t>(payload.size())});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

void require_solo_again(miner_link& miner, downstream_work const& pooled,
    domain::chain::script const& solo) {
    // The selection stays. The scriptSig does not: solo work puts the channel
    // generation and the original extranonce prefix back into the coinbase.
    auto const returned = miner.next();
    REQUIRE(pays(coinbase_from(returned), solo, 1));
    REQUIRE(returned.prefix == miner.opened.extranonce_prefix);
    REQUIRE(returned.prev.prev_hash == pooled.prev.prev_hash);
    REQUIRE(returned.job.merkle_path == pooled.job.merkle_path);
    REQUIRE(returned.job.job_id != pooled.job.job_id);
}

domain::chain::output::list paid_split(bool larger_first) {
    domain::chain::output low{1, domain::chain::script(data_chunk{0x51}, false), std::nullopt};
    domain::chain::output high{regtest_subsidy - 1,
        domain::chain::script(data_chunk{0x52}, false), std::nullopt};
    if (larger_first) return {std::move(high), std::move(low)};
    return {std::move(low), std::move(high)};
}

struct payout_round {
    request_payout_outputs request;
    declare_mining_job declaration;
    set_custom_mining_job custom;
    downstream_work pooled;
};

payout_round complete_payout(client_fixture& fixture, miner_link& miner, bool full,
    bool exact, size_t height) {
    REQUIRE(height > 0);
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    REQUIRE(allocation.user_identifier == "account.worker");
    auto const paid = paid_split( ! exact);
    auto const reserved = output_bytes(paid);
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, reserved});
    auto const request = fixture.jds.receive<request_payout_outputs>();
    REQUIRE(request.request_id == allocation.request_id);
    REQUIRE(request.mining_job_token == data_chunk{1, 2, 3});
    REQUIRE(request.available_payout_value == regtest_subsidy);
    REQUIRE(request.height == height);
    REQUIRE(request.prev_hash == miner.work.prev.prev_hash);
    REQUIRE(request.nbits == miner.work.prev.nbits);
    auto const prefix = node::mining::payout_coinbase_prefix(height, request.request_id,
        8, jd_session::min_extranonce_size);
    REQUIRE(prefix);
    REQUIRE(request.coinbase_prefix == *prefix);

    request_payout_outputs_success success;
    success.request_id = request.request_id;
    success.flags = exact ? exact_outputs_flag : 0;
    success.coinbase_tx_version = exact ? 2u : 0u;
    success.coinbase_tx_input_sequence = exact ? 0xfffffffeu : 0u;
    success.coinbase_tx_locktime = exact ? static_cast<uint32_t>(height - 1) : 0u;
    success.coinbase_tx_outputs = reserved;
    fixture.jds.send(success);

    declare_mining_job declaration{};
    if (full) {
        declaration = fixture.jds.receive<declare_mining_job>();
        REQUIRE(declaration.request_id == request.request_id);
        REQUIRE(declaration.mining_job_token == data_chunk{1, 2, 3});
        data_chunk raw = declaration.coinbase_tx_prefix;
        data_chunk const pool_prefix(8, 0x12);
        raw.insert(raw.end(), pool_prefix.begin(), pool_prefix.end());
        raw.insert(raw.end(), jd_session::min_extranonce_size, uint8_t{0});
        raw.insert(raw.end(), declaration.coinbase_tx_suffix.begin(),
            declaration.coinbase_tx_suffix.end());
        byte_reader source(raw);
        auto tx = domain::chain::transaction::from_data(source, true);
        REQUIRE(tx);
        REQUIRE(source.is_exhausted());
        REQUIRE(tx->outputs() == paid);
        REQUIRE(tx->version() == (exact ? 2u : 1u));
        REQUIRE(tx->locktime() == success.coinbase_tx_locktime);
        REQUIRE(tx->inputs().size() == 1);
        REQUIRE(tx->inputs().front().sequence() == (exact ? 0xfffffffeu : 0xffffffffu));
        auto const& script = tx->inputs().front().script().bytes();
        REQUIRE(script.size() >= prefix->size());
        REQUIRE(std::equal(prefix->begin(), prefix->end(), script.begin()));
        fixture.jds.send(declare_mining_job_success{declaration.request_id, {9, 8, 7}});
    }
    auto const custom = fixture.pool.receive<set_custom_mining_job>();
    REQUIRE(custom.channel_id == 7);
    REQUIRE(custom.request_id == request.request_id);
    REQUIRE(custom.mining_job_token == (full ? data_chunk{9, 8, 7} : data_chunk{1, 2, 3}));
    REQUIRE(custom.coinbase_prefix == *prefix);
    REQUIRE(custom.coinbase_tx_version == (exact ? 2u : 1u));
    REQUIRE(custom.coinbase_tx_input_sequence == (exact ? 0xfffffffeu : 0xffffffffu));
    REQUIRE(custom.coinbase_tx_locktime == success.coinbase_tx_locktime);
    REQUIRE(read_outputs(custom.coinbase_tx_outputs) == paid);
    auto const pooled = miner.next();
    auto const downstream = coinbase_from(pooled);
    REQUIRE(downstream.outputs() == paid);
    REQUIRE(downstream.version() == (exact ? 2u : 1u));
    REQUIRE(downstream.locktime() == success.coinbase_tx_locktime);
    REQUIRE(downstream.inputs().front().sequence() == (exact ? 0xfffffffeu : 0xffffffffu));
    auto const& script = downstream.inputs().front().script().bytes();
    REQUIRE(script.size() >= custom.coinbase_prefix.size());
    REQUIRE(std::equal(custom.coinbase_prefix.begin(), custom.coinbase_prefix.end(), script.begin()));
    if ( ! exact) REQUIRE(downstream.outputs().front().value() != regtest_subsidy);
    return {request, std::move(declaration), custom, pooled};
}

} // namespace

TEST_CASE("sv2 JD client negotiates both modes and consumes either upstream disconnect without reconnecting", "[sv2 jd]") {
    for (bool const full_template : {false, true}) {
        for (bool const pool_drop : {false, true}) {
            CAPTURE(full_template, pool_drop);
            client_fixture fixture;
            auto configuration = settings(fixture.pool, fixture.jds);
            configuration.jd.mode = full_template ? "full-template" : "coinbase-only";
            fixture.start(configuration);
            fixture.setup();
            auto const declaration = fixture.jds.receive<setup_connection>();
            REQUIRE(declaration.protocol == setup_connection::protocol_job_declaration);
            REQUIRE(declaration.flags == (full_template ? 1u : 0u));
            REQUIRE(declaration.endpoint_port == fixture.jds.port);
            fixture.jds.send(setup_connection_success{2, 0});
            answer_extensions(fixture, false, full_template);
            auto const request = fixture.open();
            fixture.accept(request);
            fixture.await(fixture.ready);
            REQUIRE_NOTHROW(fixture.ready.get());
            // An EOF after the one channel request also proves that no second
            // channel request was queued while both upstreams were ready.
            (pool_drop ? fixture.pool : fixture.jds).finish_writing();
            fixture.await(fixture.result);
            REQUIRE_NOTHROW(fixture.result.get());
            fixture.eof(fixture.pool.socket);
            fixture.eof(fixture.jds.socket);

            ::asio::ip::tcp::socket retry(fixture.io);
            auto accepted = std::make_shared<operation>();
            fixture.pool.acceptor.async_accept(retry, [accepted](std::error_code error) {
                accepted->error = error;
                accepted->done = true;
            });
            ::asio::ip::tcp::socket jd_retry(fixture.io);
            auto jd_accepted = std::make_shared<operation>();
            fixture.jds.acceptor.async_accept(jd_retry, [jd_accepted](std::error_code error) {
                jd_accepted->error = error;
                jd_accepted->done = true;
            });
            CHECK_FALSE(fixture.wait_until([&] { return accepted->done; }, 50ms));
            CHECK_FALSE(jd_accepted->done);
        }
    }
}

TEST_CASE("sv2 JD client setup and extended channel refusals terminate both upstreams", "[sv2 jd]") {
    client_fixture fixture;
    fixture.start(settings(fixture.pool, fixture.jds));
    fixture.setup();
    (void)fixture.jds.receive<setup_connection>();
    SECTION("pool setup") {
        fixture.pool.send(setup_connection_error{mining_flag_requires_work_selection, "unsupported-feature-flags"});
        fixture.pool.finish_writing();
    }
    SECTION("JDS setup") {
        fixture.jds.send(setup_connection_error{1, "unsupported-feature-flags"});
        fixture.jds.finish_writing();
    }
    SECTION("pool channel") {
        fixture.jds.send(setup_connection_success{2, 0});
        answer_extensions(fixture, false);
        auto const request = fixture.open();
        fixture.pool.send(open_mining_channel_error{request.request_id, "unknown-user"});
        fixture.pool.finish_writing();
    }
    fixture.finished(false);
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
}

TEST_CASE("sv2 JD client stop before run wakes readiness and malformed endpoint or authority construction fails", "[sv2 jd]") {
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    SECTION("stop before run") {
        fixture.client = std::make_shared<jd_client>(configuration, fixture.io.get_executor());
        fixture.ready = ::asio::co_spawn(fixture.io, await_client_ready(fixture.client), ::asio::use_future);
        fixture.client->stop();
        fixture.await(fixture.ready);
        REQUIRE_THROWS_AS(fixture.ready.get(), std::system_error);
        fixture.result = ::asio::co_spawn(fixture.io, run_client(fixture.client), ::asio::use_future);
        fixture.await(fixture.result);
        REQUIRE_NOTHROW(fixture.result.get());
    }
    SECTION("pool endpoint") {
        configuration.jd.mining_address = "invalid";
        REQUIRE_THROWS_AS(jd_client(configuration, fixture.io.get_executor()), std::invalid_argument);
    }
    SECTION("JDS endpoint") {
        configuration.jd.jds_address = "invalid";
        REQUIRE_THROWS_AS(jd_client(configuration, fixture.io.get_executor()), std::invalid_argument);
    }
    SECTION("pool authority") {
        configuration.jd.mining_authority_public_key = "invalid";
        REQUIRE_THROWS_AS(jd_client(configuration, fixture.io.get_executor()), std::invalid_argument);
    }
    SECTION("JDS authority") {
        configuration.jd.jds_authority_public_key = "invalid";
        REQUIRE_THROWS_AS(jd_client(configuration, fixture.io.get_executor()), std::invalid_argument);
    }
}

TEST_CASE("sv2 JD silent setup or channel closes both upstreams while solo jobs keep their payout", "[sv2 jd]") {
    enum class silence { pool_setup, jds_setup, pool_channel };
    auto stage = silence::pool_setup;
    SECTION("pool setup") {}
    SECTION("JDS setup") { stage = silence::jds_setup; }
    SECTION("pool channel") { stage = silence::pool_channel; }
    auto chain = std::make_shared<test::chain_fixture>("sv2_jd_solo", 0);
    client_fixture fixture;
    REQUIRE(chain->created());
    REQUIRE(chain->start());
    auto const block = test::mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - test::block_spacing, 0, {}, 0);
    std::vector<domain::chain::block> const blocks{block};
    REQUIRE(chain->organizer().add_headers(test::headers_of(blocks)).headers_added == 1);
    test::persist_headers(*chain, blocks, 1);
    test::connect_bodies(*chain, blocks, 1);
    auto const configuration = settings(fixture.pool, fixture.jds);
    auto provider = std::make_shared<template_provider>(configuration, fixture.io.get_executor(),
        chain->chain(), domain::config::network::regtest);
    auto result = ::asio::co_spawn(fixture.io, run_provider(provider, chain), ::asio::use_future);
    auto listening = ::asio::co_spawn(fixture.io, provider->wait_until_listening(), ::asio::use_future);
    struct stop_provider {
        io_driver& driver;
        std::shared_ptr<template_provider> provider;
        std::future<void>& result;
        ~stop_provider() {
            provider->stop();
            if (result.valid()) {
                (void)driver.wait_until([&] { return result.wait_for(0s) == std::future_status::ready; }, 1s);
            }
        }
    } cleanup{fixture, provider, result};
    fixture.await(listening);
    auto const port = listening.get();
    REQUIRE(port != 0);
    fixture.pool.handshake();
    fixture.jds.handshake();
    (void)fixture.pool.receive<setup_connection>();
    (void)fixture.jds.receive<setup_connection>();
    if (stage != silence::jds_setup) {
        fixture.jds.send(setup_connection_success{2, 0});
        // Leave RequestExtensions unanswered. Pool setup's mining deadline was
        // armed at SetupConnection and is not refreshed, so it fires first and
        // no replacement JDS connection is opened. Pool channel refreshes both
        // the channel request and RequestExtensions, so those deadlines are
        // close: if negotiation wins, one replacement is opened and the pool
        // deadline then stops it. The check after both sockets close only
        // requires that replacement to be gone.
        auto const extensions = fixture.jds.receive<request_extensions>();
        REQUIRE(extensions.requested_extensions == requested_extensions(true));
    }
    if (stage != silence::pool_setup) {
        auto const request = fixture.open();
        if (stage != silence::pool_channel) fixture.accept(request);
    }
    std::string error;
    auto const payout = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(payout);
    auto const pool_closed = fixture.begin_read(fixture.pool.socket, 1);
    auto const jds_closed = fixture.begin_read(fixture.jds.socket, 1);
    // The first work is served while a request is still unanswered, and another
    // miner gets the same solo payout after the pair's deadline has fired.
    solo_job(fixture, port, payout->script);
    REQUIRE_FALSE(pool_closed->done);
    REQUIRE_FALSE(jds_closed->done);
    REQUIRE(fixture.wait_until([&] { return pool_closed->done && jds_closed->done; }));
    REQUIRE(pool_closed->size == 0);
    REQUIRE(pool_closed->error == ::asio::error::eof);
    REQUIRE(jds_closed->size == 0);
    REQUIRE(jds_closed->error == ::asio::error::eof);
    // The client has stopped. Either no second JDS connection was opened, or
    // the negotiation deadline opened one and the pool deadline already closed
    // it. Handshake bytes may already be buffered; the socket must not stay up.
    // Which of the two deadlines fired first does not change this.
    ::asio::ip::tcp::socket replacement(fixture.io);
    auto accepted = std::make_shared<operation>();
    fixture.jds.acceptor.async_accept(replacement, [accepted](std::error_code error) {
        accepted->error = error;
        accepted->done = true;
    });
    if ( ! fixture.wait_until([&] { return accepted->done; }, 50ms)) {
        fixture.jds.acceptor.cancel();
        REQUIRE(fixture.wait_until([&] { return accepted->done; }));
    }
    // An accept error means the reconnect is already gone. A socket that did
    // arrive, including one with a handshake already buffered, must end.
    if (accepted->done && ! accepted->error) {
        auto const end = std::chrono::steady_clock::now() + 1s;
        bool ended = false;
        while ( ! ended && std::chrono::steady_clock::now() < end) {
            auto const chunk = fixture.begin_read(replacement, 64, false);
            if ( ! fixture.wait_until([&] { return chunk->done; }, 200ms)) break;
            if (chunk->error) ended = true;
        }
        REQUIRE(ended);
    }
    solo_job(fixture, port, payout->script);
    provider->stop();
    fixture.await(result);
    REQUIRE_NOTHROW(result.get());
}

TEST_CASE("sv2 JD invalid listener configuration reports failure before accepting upstreams", "[sv2 jd]") {
    auto chain = std::make_shared<test::chain_fixture>("sv2_jd_listener", 0);
    client_fixture fixture;
    REQUIRE(chain->created());
    REQUIRE(chain->start());
    auto configuration = settings(fixture.pool, fixture.jds);
    configuration.jd.jds_address.clear();
    auto provider = std::make_shared<template_provider>(configuration, fixture.io.get_executor(),
        chain->chain(), domain::config::network::regtest);
    auto result = ::asio::co_spawn(fixture.io, run_provider(provider, chain), ::asio::use_future);
    auto listening = ::asio::co_spawn(fixture.io, provider->wait_until_listening(), ::asio::use_future);
    fixture.await(listening);
    REQUIRE_THROWS_AS(listening.get(), std::system_error);
    fixture.await(result);
    REQUIRE_NOTHROW(result.get());
    CHECK_FALSE(fixture.pool.accepted->done);
    CHECK_FALSE(fixture.jds.accepted->done);
}

TEST_CASE("sv2 JD provider stop joins publication waiters and both upstreams", "[sv2 jd]") {
    bool disconnected = false;
    SECTION("allocation is unanswered") {}
    SECTION("upstreams have already ended") { disconnected = true; }
    auto chain = started_chain("sv2_jd_stop");
    client_fixture fixture;
    auto const configuration = settings(fixture.pool, fixture.jds);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    miner_link miner(fixture, provider.port);
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    REQUIRE(allocation.request_id != 0);
    if (disconnected) {
        fixture.pool.finish_writing();
        fixture.eof(fixture.pool.socket);
        fixture.eof(fixture.jds.socket);
    }

    // Keep the downstream open: stop must end the publication wait itself.
    provider.provider->stop();
    fixture.await(provider.result);
    REQUIRE_NOTHROW(provider.result.get());
    fixture.eof(miner.socket);
    if ( ! disconnected) {
        fixture.eof(fixture.pool.socket);
        fixture.eof(fixture.jds.socket);
    }
}

TEST_CASE("sv2 JD publications wake every subscriber and coalesce to the latest payout", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_publications");
    client_fixture fixture;
    fixture.start(settings(fixture.pool, fixture.jds), &chain->chain());
    auto first = fixture.client->subscribe_publications();
    auto slow = fixture.client->subscribe_publications();
    auto removed = fixture.client->subscribe_publications();
    for (auto const& channel : {first, slow, removed}) {
        auto initial = ::asio::co_spawn(fixture.io, receive_publication(channel), ::asio::use_future);
        fixture.await(initial);
        REQUIRE_NOTHROW(initial.get());
    }
    REQUIRE_FALSE(fixture.client->notice());
    fixture.client->unsubscribe_publications(removed);
    auto removed_wait = ::asio::co_spawn(fixture.io, receive_publication(removed), ::asio::use_future);
    fixture.await(removed_wait);
    REQUIRE_THROWS_AS(removed_wait.get(), std::system_error);
    auto pool_wait = ::asio::co_spawn(fixture.io, receive_publication(first), ::asio::use_future);

    fixture.setup();
    (void)fixture.jds.receive<setup_connection>();
    fixture.jds.send(setup_connection_success{2, 0});
    answer_extensions(fixture, false);
    auto const request = fixture.open();
    fixture.accept(request);
    fixture.await(fixture.ready);
    REQUIRE_NOTHROW(fixture.ready.get());
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    (void)fixture.jds.receive<declare_mining_job>();
    fixture.await(pool_wait);
    REQUIRE_NOTHROW(pool_wait.get());
    auto const pooled = fixture.client->notice();
    REQUIRE(pooled);
    REQUIRE(pooled->pool);

    // A subscription made after publication has a buffered wake as well.
    auto late = fixture.client->subscribe_publications();
    auto late_initial = ::asio::co_spawn(fixture.io, receive_publication(late), ::asio::use_future);
    fixture.await(late_initial);
    REQUIRE_NOTHROW(late_initial.get());
    REQUIRE(fixture.client->notice()->generation == pooled->generation);
    auto first_fallback = ::asio::co_spawn(fixture.io, receive_publication(first), ::asio::use_future);
    auto late_fallback = ::asio::co_spawn(fixture.io, receive_publication(late), ::asio::use_future);
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
    for (auto* result : {&first_fallback, &late_fallback}) {
        fixture.await(*result);
        REQUIRE_NOTHROW(result->get());
    }
    auto const solo = fixture.client->notice();
    REQUIRE(solo);
    REQUIRE_FALSE(solo->pool);
    REQUIRE(solo->generation > pooled->generation);

    // This reader did not drain the pool wake. One buffered wake still leads
    // to the newer solo publication, without retaining a queue of old work.
    auto latest = ::asio::co_spawn(fixture.io, receive_publication(slow), ::asio::use_future);
    fixture.await(latest);
    REQUIRE_NOTHROW(latest.get());
    REQUIRE(fixture.client->notice()->generation == solo->generation);
    auto quiet = ::asio::co_spawn(fixture.io, receive_publication(slow), ::asio::use_future);
    CHECK_FALSE(fixture.wait_until([&] {
        return quiet.wait_for(0s) == std::future_status::ready;
    }, 50ms));
    auto remained_closed = ::asio::co_spawn(fixture.io, receive_publication(removed), ::asio::use_future);
    fixture.await(remained_closed);
    REQUIRE_THROWS_AS(remained_closed.get(), std::system_error);
    slow->cancel();
    slow->close();
    fixture.client->unsubscribe_publications(first);
    fixture.client->unsubscribe_publications(slow);
    fixture.client->unsubscribe_publications(late);
    fixture.await(quiet);
    REQUIRE_THROWS_AS(quiet.get(), std::system_error);
}

TEST_CASE("sv2 JD client ends both upstreams when chain notifications stop", "[sv2 jd]") {
    bool before_run = false;
    bool queued_tip = false;
    SECTION("subscription is already stopped") { before_run = true; }
    SECTION("waiting for a tip") {}
    SECTION("tip queued at shutdown") { queued_tip = true; }
    auto chain = started_chain("sv2_jd_chain_stop");
    client_fixture fixture;
    if (before_run) REQUIRE(chain->chain().stop());
    fixture.start(settings(fixture.pool, fixture.jds), &chain->chain());
    if (before_run) {
        fixture.finished(false);
        CHECK_FALSE(fixture.pool.accepted->done);
        CHECK_FALSE(fixture.jds.accepted->done);
        return;
    }

    handshake_upstreams(fixture, true);
    fixture.await(fixture.ready);
    REQUIRE_NOTHROW(fixture.ready.get());
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    REQUIRE(allocation.request_id != 0);
    if (queued_tip) {
        REQUIRE(chain->chain().begin_transition());
        chain->chain().end_transition();
    }
    REQUIRE(chain->chain().stop());
    REQUIRE(fixture.wait_until([&] {
        return fixture.result.wait_for(0s) == std::future_status::ready;
    }, 1s));
    REQUIRE_NOTHROW(fixture.result.get());
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
}

TEST_CASE("sv2 JD fallback finishes before a later tip and chain shutdown", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_fallback_tip_stop");
    client_fixture fixture;
    fixture.start(settings(fixture.pool, fixture.jds), &chain->chain());
    handshake_upstreams(fixture, true);
    fixture.await(fixture.ready);
    REQUIRE_NOTHROW(fixture.ready.get());
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    auto const declaration = fixture.jds.receive<declare_mining_job>();
    auto const pooled = fixture.client->notice();
    REQUIRE(pooled);
    REQUIRE(pooled->pool);

    fixture.jds.send(declare_mining_job_error{declaration.request_id, "policy-refused", {}});
    fixture.jds.finish_writing();
    REQUIRE(fixture.wait_until([&] {
        return fixture.result.wait_for(0s) == std::future_status::ready;
    }, 1s));
    REQUIRE_NOTHROW(fixture.result.get());
    auto const solo = fixture.client->notice();
    REQUIRE(solo);
    REQUIRE_FALSE(solo->pool);
    REQUIRE(solo->generation > pooled->generation);
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);

    // Connecting a successor publishes a real tip after the JD attempt ended.
    // It must not revive declarations or replace the terminal solo notice.
    auto const block = test::mine_block(solo->previous, 2, uint32_t(zulu_time()), 0, {}, 0);
    std::vector<domain::chain::block> const blocks{block};
    REQUIRE(chain->organizer().add_headers(test::headers_of(blocks)).headers_added == 1);
    test::persist_headers(*chain, blocks, 2);
    test::connect_bodies(*chain, blocks, 2);
    auto const view = chain->chain().chain_view();
    REQUIRE(view);
    REQUIRE(view->tip_hash == block.hash());
    CHECK_FALSE(fixture.wait_until([&] { return fixture.client->notice() != solo; }, 50ms));

    // run() has joined its receiver, so chain shutdown can outlive the client.
    std::weak_ptr<jd_client> const ended = fixture.client;
    fixture.client.reset();
    REQUIRE(ended.expired());
    REQUIRE(chain->chain().stop());
    fixture.io.restart();
    fixture.io.run_for(50ms);
}

TEST_CASE("sv2 JD invalid configuration refuses full node startup before opening the chain", "[sv2 jd]") {
    client_fixture fixture;
    test::chain_fixture directory{"sv2_jd_startup"};
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
    config.sv2 = settings(fixture.pool, fixture.jds);
    SECTION("listener disabled") { config.sv2.enabled = false; }
    SECTION("mining disabled") { config.sv2.mining.enabled = false; }
    SECTION("missing JDS") { config.sv2.jd.jds_address.clear(); }
    SECTION("missing mode") { config.sv2.jd.mode.clear(); }
    SECTION("invalid key") { config.sv2.jd.mining_authority_public_key = "invalid"; }
    SECTION("missing solo payout") { config.sv2.mining.address.clear(); }
    node::full_node full_node(config);
    auto result = ::asio::co_spawn(fixture.io, full_node.start(), ::asio::use_future);
    fixture.await(result);
    REQUIRE(result.get() == error::operation_failed);
    REQUIRE(full_node.stopped());
    REQUIRE(full_node.chain().stopped());
    CHECK_FALSE(fixture.pool.accepted->done);
    CHECK_FALSE(fixture.jds.accepted->done);
}

TEST_CASE("sv2 JD declares the local template in both modes and pays the pool output", "[sv2 jd]") {
    bool full = true;
    SECTION("full-template") {}
    SECTION("coinbase-only") { full = false; }
    auto chain = started_chain(full ? "sv2_jd_decl_full" : "sv2_jd_decl_coin");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    configuration.jd.mode = full ? "full-template" : "coinbase-only";
    std::string error;
    auto const payout = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(payout);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, full);
    // The token is outstanding while this miner reads. Solo work must already
    // be being served; the allocation does not gate the job.
    miner_link miner(fixture, provider.port);
    REQUIRE(pays(coinbase_from(miner.work), payout->script, 1));
    auto const tip = chain->chain().chain_view();
    REQUIRE(tip);
    REQUIRE(miner.work.prev.prev_hash == tip->tip_hash);
    auto const declared = declare_local_template(fixture, miner, full);
    REQUIRE(declared.pooled.prev.prev_hash == miner.work.prev.prev_hash);
    REQUIRE(declared.pooled.job.merkle_path == miner.work.job.merkle_path);
    REQUIRE(declared.custom.prev_hash == tip->tip_hash);
    fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
        declared.custom.request_id, 77});
    auto quiet = fixture.begin_read(fixture.pool.socket, 1);
    CHECK_FALSE(fixture.wait_until([&] { return quiet->done; }, 300ms));
}

TEST_CASE("sv2 JD forwards a share that meets the upstream target and connects a network solution", "[sv2 jd]") {
    enum class kind { forward, below, block };
    auto mode = kind::forward;
    SECTION("above upstream target") {}
    SECTION("below upstream target") { mode = kind::below; }
    SECTION("network target") { mode = kind::block; }
    char const* tag = "sv2_jd_forward";
    if (mode == kind::below) tag = "sv2_jd_below";
    if (mode == kind::block) tag = "sv2_jd_block";
    auto chain = started_chain(tag);
    client_fixture fixture;
    std::optional<submission_pipeline> pipeline;
    if (mode == kind::block) pipeline.emplace(fixture, *chain);
    auto const configuration = settings(fixture.pool, fixture.jds);
    provider_run provider(fixture, configuration, chain);
    hash_digest tiny{};
    tiny[0] = 1;
    handshake_upstreams(fixture, true, mode == kind::below ? &tiny : nullptr);
    miner_link miner(fixture, provider.port);
    auto const declared = declare_local_template(fixture, miner, true);
    constexpr uint32_t custom_job_id = 77;
    fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
        declared.custom.request_id, custom_job_id});
    data_chunk const extranonce(miner.opened.extranonce_size, 0x5a);
    auto const nonce = find_nonce(declared.pooled, extranonce, mode == kind::block);
    submit_shares_extended const submission{miner.opened.channel_id, 1,
        declared.pooled.job.job_id, nonce, declared.pooled.prev.ntime_start,
        declared.pooled.job.version, extranonce};
    if (mode == kind::below) {
        REQUIRE(to_uint256(share_hash(declared.pooled, extranonce, nonce)) > to_uint256(tiny));
        auto pool_quiet = fixture.begin_read(fixture.pool.socket, 1);
        auto jds_quiet = fixture.begin_read(fixture.jds.socket, 1);
        fixture.write(miner.socket, frame(submission));
        auto const accepted = receive_plain<submit_shares_success>(fixture, miner.socket);
        REQUIRE(accepted.channel_id == miner.opened.channel_id);
        REQUIRE(accepted.last_sequence_number == 1);
        CHECK_FALSE(fixture.wait_until([&] {
            return pool_quiet->done || jds_quiet->done;
        }, 500ms));
        return;
    }
    fixture.write(miner.socket, frame(submission));
    std::optional<push_solution> pushed;
    if (mode == kind::block) pushed = fixture.jds.receive<push_solution>();
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.channel_id == 7);
    REQUIRE(forwarded.sequence_number == 0);
    REQUIRE(forwarded.job_id == custom_job_id);
    REQUIRE(forwarded.nonce == nonce);
    REQUIRE(forwarded.ntime == declared.pooled.prev.ntime_start);
    REQUIRE(forwarded.version == declared.pooled.job.version);
    data_chunk full = declared.pooled.prefix;
    full.insert(full.end(), extranonce.begin(), extranonce.end());
    // The pool's 8-byte prefix and its whole grant.
    REQUIRE(full.size() == 8 + jd_session::min_extranonce_size);
    REQUIRE(forwarded.extranonce == data_chunk(full.begin() + 8, full.end()));
    if (pushed) {
        REQUIRE(pushed->extranonce == full);
        REQUIRE(pushed->prev_hash == declared.pooled.prev.prev_hash);
        REQUIRE(pushed->nonce == nonce);
        REQUIRE(pushed->ntime == declared.pooled.prev.ntime_start);
        REQUIRE(pushed->nbits == declared.pooled.prev.nbits);
        REQUIRE(pushed->version == declared.pooled.job.version);
        auto const solved = share_hash(declared.pooled, extranonce, nonce);
        REQUIRE(fixture.wait_until([&] {
            auto const view = chain->chain().chain_view();
            return view && view->connected_tip_height == 2 && view->tip_hash == solved;
        }, 10s));
        auto const next = fixture.jds.receive<allocate_mining_job_token>();
        REQUIRE(next.request_id != declared.custom.request_id);
        REQUIRE(next.user_identifier == "account.worker");
        return;
    }
    fixture.pool.send(submit_shares_success{forwarded.channel_id,
        forwarded.sequence_number, 1, 1});
    auto quiet = fixture.begin_read(fixture.pool.socket, 1);
    CHECK_FALSE(fixture.wait_until([&] { return quiet->done; }, 300ms));
}

TEST_CASE("sv2 JD fallback pays the solo address on the next downstream job", "[sv2 jd]") {
    enum class trigger { allocate, declare_error, custom_error, share_error, pool_drop, jds_drop };
    auto which = trigger::allocate;
    SECTION("allocation timeout") {}
    SECTION("declaration error") { which = trigger::declare_error; }
    SECTION("custom job error") { which = trigger::custom_error; }
    SECTION("share error") { which = trigger::share_error; }
    SECTION("pool drop") { which = trigger::pool_drop; }
    SECTION("jds drop") { which = trigger::jds_drop; }
    bool const full = which != trigger::custom_error;
    char const* tag = "sv2_jd_fall_alloc";
    if (which == trigger::declare_error) tag = "sv2_jd_fall_declare";
    if (which == trigger::custom_error) tag = "sv2_jd_fall_custom";
    if (which == trigger::share_error) tag = "sv2_jd_fall_share";
    if (which == trigger::pool_drop) tag = "sv2_jd_fall_pool";
    if (which == trigger::jds_drop) tag = "sv2_jd_fall_jds";
    auto chain = started_chain(tag);
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    configuration.jd.mode = full ? "full-template" : "coinbase-only";
    std::string error;
    auto const payout = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(payout);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, full);
    miner_link miner(fixture, provider.port);
    REQUIRE(pays(coinbase_from(miner.work), payout->script, 1));
    if (which == trigger::allocate) {
        // The JDS has the token request and does not answer it. The 3 second
        // declaration deadline closes both upstreams. The miner already on
        // solo work is not given another job, and a new miner is still paid
        // sv2.mining.address.
        auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
        REQUIRE(allocation.user_identifier == "account.worker");
        auto pool_closed = fixture.begin_read(fixture.pool.socket, 1);
        auto jds_closed = fixture.begin_read(fixture.jds.socket, 1);
        auto pending = fixture.begin_read(miner.socket, 1);
        REQUIRE(fixture.wait_until([&] { return pool_closed->done && jds_closed->done; }));
        REQUIRE(pool_closed->size == 0);
        REQUIRE(pool_closed->error == ::asio::error::eof);
        REQUIRE(jds_closed->size == 0);
        REQUIRE(jds_closed->error == ::asio::error::eof);
        CHECK_FALSE(pending->done);
        solo_job(fixture, provider.port, payout->script);
        require_no_reconnect(fixture);
        return;
    }
    auto const declared = declare_local_template(fixture, miner, full,
        which != trigger::declare_error);
    REQUIRE(declared.pooled.prev.prev_hash == miner.work.prev.prev_hash);
    REQUIRE(declared.pooled.job.merkle_path == miner.work.job.merkle_path);
    if (which == trigger::declare_error) {
        fixture.jds.send(declare_mining_job_error{declared.declaration.request_id,
            "unknown-pool-policy", {}});
    } else if (which == trigger::custom_error) {
        fixture.pool.send(set_custom_mining_job_error{declared.custom.channel_id,
            declared.custom.request_id, "policy-refused"});
    } else if (which == trigger::share_error) {
        fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
            declared.custom.request_id, 77});
        data_chunk const extranonce(miner.opened.extranonce_size, 0x5a);
        auto const nonce = find_nonce(declared.pooled, extranonce, false);
        fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
            declared.pooled.job.job_id, nonce, declared.pooled.prev.ntime_start,
            declared.pooled.job.version, extranonce}));
        auto const forwarded = fixture.pool.receive<submit_shares_extended>();
        REQUIRE(forwarded.job_id == 77);
        fixture.pool.send(submit_shares_error{forwarded.channel_id,
            forwarded.sequence_number, "stale-share"});
    } else {
        fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
            declared.custom.request_id, 77});
        (which == trigger::pool_drop ? fixture.pool : fixture.jds).finish_writing();
    }
    require_solo_again(miner, declared.pooled, payout->script);
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    require_no_reconnect(fixture);
}

TEST_CASE("sv2 JD granted payout outputs are what downstream miners and the pool see", "[sv2 jd]") {
    bool full = true;
    bool exact = true;
    SECTION("full-template exact outputs") {}
    SECTION("coinbase-only exact outputs") { full = false; }
    SECTION("full-template without the exact flag") { exact = false; }
    char const* tag = "sv2_jd_pay_exact";
    if ( ! full) tag = "sv2_jd_pay_coin";
    if ( ! exact) tag = "sv2_jd_pay_split";
    auto chain = started_chain(tag);
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    configuration.jd.mode = full ? "full-template" : "coinbase-only";
    std::string error;
    auto const solo = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(solo);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, full, nullptr, true);
    miner_link miner(fixture, provider.port);
    REQUIRE(pays(coinbase_from(miner.work), solo->script, 1));
    auto const tip = chain->chain().chain_view();
    REQUIRE(tip);
    auto const round = complete_payout(fixture, miner, full, exact, tip->connected_tip_height + 1);
    fixture.pool.send(set_custom_mining_job_success{round.custom.channel_id,
        round.custom.request_id, 77});
    auto quiet = fixture.begin_read(fixture.pool.socket, 1);
    CHECK_FALSE(fixture.wait_until([&] { return quiet->done; }, 300ms));
}

TEST_CASE("sv2 JD extension silence closes that JDS connection and asks for a base token on the next one", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_ext_quiet");
    client_fixture fixture;
    provider_run provider(fixture, settings(fixture.pool, fixture.jds), chain);
    fixture.pool.handshake();
    fixture.jds.handshake();
    (void)fixture.pool.receive<setup_connection>();
    (void)fixture.jds.receive<setup_connection>();
    fixture.jds.send(setup_connection_success{2, 0});
    auto const extensions = fixture.jds.receive<request_extensions>();
    REQUIRE(extensions.request_id == 1);
    REQUIRE(extensions.requested_extensions == requested_extensions(true));
    auto const began = std::chrono::steady_clock::now();
    auto const channel = fixture.open();
    fixture.accept(channel);
    // Armed before the close. A pool frame, or the pool socket dropping, is
    // the replacement taking the mining connection with it.
    auto pool_quiet = fixture.begin_read(fixture.pool.socket, 1);
    ::asio::ip::tcp::socket again(fixture.io);
    auto accepted = std::make_shared<operation>();
    fixture.jds.acceptor.async_accept(again, [accepted](std::error_code error) {
        accepted->error = error;
        accepted->done = true;
    });
    // The socket that received RequestExtensions carries no token. The close
    // is the response deadline, so it is not immediate.
    fixture.eof(fixture.jds.socket);
    REQUIRE(std::chrono::steady_clock::now() - began >= 1s);
    REQUIRE_FALSE(pool_quiet->done);
    REQUIRE(fixture.wait_until([&] { return accepted->done; }));
    REQUIRE_FALSE(accepted->error);
    auto transport = handshake_responder(fixture, again, fixture.jds.authority);
    auto const setup = receive_encrypted<setup_connection>(fixture, again, transport);
    REQUIRE(setup.protocol == setup_connection::protocol_job_declaration);
    REQUIRE(setup.flags == 1u);
    REQUIRE(setup.endpoint_host == "127.0.0.1");
    REQUIRE(setup.endpoint_port == fixture.jds.port);
    send_encrypted(fixture, again, transport, frame(setup_connection_success{2, 0}));
    // SetupConnection.Success is not followed by RequestExtensions. decode
    // rejects that type; the next frame is the base-token request.
    auto const allocation = receive_encrypted<allocate_mining_job_token>(fixture, again, transport);
    REQUIRE(allocation.user_identifier == "account.worker");
    REQUIRE(allocation.request_id != 0);
    CHECK_FALSE(fixture.wait_until([&] { return pool_quiet->done; }, 300ms));
}

TEST_CASE("sv2 JD stale payout notice allocates a new token before declaring", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_pay_stale");
    client_fixture fixture;
    provider_run provider(fixture, settings(fixture.pool, fixture.jds), chain);
    handshake_upstreams(fixture, true, nullptr, true);
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    auto const reserved = output_bytes(paid_split(false));
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, reserved});
    auto const request = fixture.jds.receive<request_payout_outputs>();
    REQUIRE(request.request_id == allocation.request_id);
    REQUIRE(request.mining_job_token == data_chunk{1, 2, 3});
    auto const prefix = node::mining::payout_coinbase_prefix(request.height, request.request_id,
        8, jd_session::min_extranonce_size);
    REQUIRE(prefix);
    REQUIRE(request.coinbase_prefix == *prefix);
    fixture.jds.send(payout_outputs_stale{});
    request_payout_outputs_success success;
    success.request_id = request.request_id;
    success.flags = exact_outputs_flag;
    success.coinbase_tx_version = 2;
    success.coinbase_tx_input_sequence = 0xfffffffeu;
    success.coinbase_tx_locktime = request.height == 0 ? 0 : request.height - 1;
    success.coinbase_tx_outputs = reserved;
    fixture.jds.send(success);
    auto const again = fixture.jds.receive<allocate_mining_job_token>();
    REQUIRE(again.request_id == request.request_id + 1);
    REQUIRE(again.user_identifier == allocation.user_identifier);
    auto quiet = fixture.begin_read(fixture.jds.socket, 1);
    CHECK_FALSE(fixture.wait_until([&] { return quiet->done; }, 200ms));
}

TEST_CASE("sv2 JD payout error and timeout fall back without replacing the solo job", "[sv2 jd]") {
    bool timeout = false;
    SECTION("error") {}
    SECTION("timeout") { timeout = true; }
    auto chain = started_chain(timeout ? "sv2_jd_pay_time" : "sv2_jd_pay_err");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    std::string error;
    auto const solo = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(solo);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true, nullptr, true);
    miner_link miner(fixture, provider.port);
    REQUIRE(pays(coinbase_from(miner.work), solo->script, 1));
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    auto const reserved = output_bytes(paid_split(false));
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, reserved});
    auto const request = fixture.jds.receive<request_payout_outputs>();
    REQUIRE(request.request_id == allocation.request_id);
    if ( ! timeout) {
        fixture.jds.send(request_payout_outputs_error{request.request_id,
            "payout-outputs-unavailable"});
    }
    auto pool_closed = fixture.begin_read(fixture.pool.socket, 1);
    auto jds_closed = fixture.begin_read(fixture.jds.socket, 1);
    auto pending = fixture.begin_read(miner.socket, 1);
    if (timeout) CHECK_FALSE(fixture.wait_until([&] { return pool_closed->done; }, 500ms));
    REQUIRE(fixture.wait_until([&] { return pool_closed->done && jds_closed->done; }));
    REQUIRE(pool_closed->size == 0);
    REQUIRE(pool_closed->error == ::asio::error::eof);
    REQUIRE(jds_closed->size == 0);
    REQUIRE(jds_closed->error == ::asio::error::eof);
    CHECK_FALSE(pending->done);
    solo_job(fixture, provider.port, solo->script);
    require_no_reconnect(fixture);
}

TEST_CASE("sv2 JD repeated stale payout answers fall back to solo", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_pay_repeat");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    std::string error;
    auto const solo = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(solo);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true, nullptr, true);
    miner_link miner(fixture, provider.port);
    REQUIRE(pays(coinbase_from(miner.work), solo->script, 1));
    auto const reserved = output_bytes(paid_split(false));
    auto const first = fixture.jds.receive<allocate_mining_job_token>();
    fixture.jds.send(allocate_mining_job_token_success{first.request_id, {1, 2, 3}, reserved});
    auto const first_request = fixture.jds.receive<request_payout_outputs>();
    REQUIRE(first_request.request_id == first.request_id);
    fixture.jds.send(request_payout_outputs_error{first_request.request_id,
        std::string(error_stale_payout_outputs)});
    auto const second = fixture.jds.receive<allocate_mining_job_token>();
    REQUIRE(second.request_id == first.request_id + 1);
    fixture.jds.send(allocate_mining_job_token_success{second.request_id, {1, 2, 3}, reserved});
    auto const second_request = fixture.jds.receive<request_payout_outputs>();
    REQUIRE(second_request.request_id == second.request_id);
    fixture.jds.send(request_payout_outputs_error{second_request.request_id,
        std::string(error_stale_payout_outputs)});
    auto pool_closed = fixture.begin_read(fixture.pool.socket, 1);
    auto jds_closed = fixture.begin_read(fixture.jds.socket, 1);
    auto pending = fixture.begin_read(miner.socket, 1);
    REQUIRE(fixture.wait_until([&] { return pool_closed->done && jds_closed->done; }));
    REQUIRE(pool_closed->size == 0);
    REQUIRE(pool_closed->error == ::asio::error::eof);
    REQUIRE(jds_closed->size == 0);
    REQUIRE(jds_closed->error == ::asio::error::eof);
    CHECK_FALSE(pending->done);
    solo_job(fixture, provider.port, solo->script);
    require_no_reconnect(fixture);
}

TEST_CASE("sv2 JD forwards a share for an upstream job id of zero without falling back", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_zero_job");
    client_fixture fixture;
    auto const configuration = settings(fixture.pool, fixture.jds);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    miner_link miner(fixture, provider.port);
    auto const declared = declare_local_template(fixture, miner, true);
    // sv2-spec 5.3.19: SetCustomMiningJob.Success.job_id is a U32. Zero is in range.
    fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
        declared.custom.request_id, 0});
    data_chunk const extranonce(miner.opened.extranonce_size, 0x5a);
    auto const nonce = find_nonce(declared.pooled, extranonce, false);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
        declared.pooled.job.job_id, nonce, declared.pooled.prev.ntime_start,
        declared.pooled.job.version, extranonce}));
    auto const local = receive_plain<submit_shares_success>(fixture, miner.socket);
    REQUIRE(local.channel_id == miner.opened.channel_id);
    REQUIRE(local.last_sequence_number == 1);
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.channel_id == 7);
    REQUIRE(forwarded.sequence_number == 0);
    REQUIRE(forwarded.job_id == 0);
    REQUIRE(forwarded.nonce == nonce);
    REQUIRE(forwarded.ntime == declared.pooled.prev.ntime_start);
    REQUIRE(forwarded.version == declared.pooled.job.version);
    data_chunk full = declared.pooled.prefix;
    full.insert(full.end(), extranonce.begin(), extranonce.end());
    REQUIRE(forwarded.extranonce == data_chunk(full.begin() + 8, full.end()));
    fixture.pool.send(submit_shares_success{forwarded.channel_id,
        forwarded.sequence_number, 1, 1});
    // Job id zero is not a refusal. The pool stays up and the miner is not
    // moved back to the solo coinbase. accept() gave this channel the maximum
    // target, so the share meets it.
    auto pool_quiet = fixture.begin_read(fixture.pool.socket, 1);
    auto miner_quiet = fixture.begin_read(miner.socket, 1);
    CHECK_FALSE(fixture.wait_until([&] {
        return pool_quiet->done || miner_quiet->done;
    }, 300ms));
}

TEST_CASE("sv2 JD does not submit a cached share under a replaced declaration", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_replaced_share");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    // The in-flight declaration has to survive connect_bodies, which is what
    // publishes the tip that replaces it. This test does not wait the deadline out.
    configuration.jd.response_timeout_seconds = 30;
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    miner_link miner(fixture, provider.port);
    auto const first = declare_local_template(fixture, miner, true);

    data_chunk const cached_extranonce(miner.opened.extranonce_size, 0x5a);
    auto const cached_nonce = find_nonce(first.pooled, cached_extranonce, false);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
        first.pooled.job.job_id, cached_nonce, first.pooled.prev.ntime_start,
        first.pooled.job.version, cached_extranonce}));
    auto const cached_ack = receive_plain<submit_shares_success>(fixture, miner.socket);
    REQUIRE(cached_ack.channel_id == miner.opened.channel_id);
    REQUIRE(cached_ack.last_sequence_number == 1);
    // Accepted downstream, withheld upstream until SetCustomMiningJob.Success.
    expect_idle(fixture, fixture.pool.socket);

    auto const block = test::mine_block(first.pooled.prev.prev_hash, 2,
        uint32_t(zulu_time()), 0, {}, 0);
    std::vector<domain::chain::block> const blocks{block};
    REQUIRE(chain->organizer().add_headers(test::headers_of(blocks)).headers_added == 1);
    test::persist_headers(*chain, blocks, 2);
    test::connect_bodies(*chain, blocks, 2);
    auto const view = chain->chain().chain_view();
    REQUIRE(view);
    REQUIRE(view->tip_hash == block.hash());

    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    REQUIRE(allocation.request_id != 0);
    REQUIRE(allocation.request_id != first.custom.request_id);
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    auto const declaration = fixture.jds.receive<declare_mining_job>();
    REQUIRE(declaration.request_id == allocation.request_id);
    fixture.jds.send(provide_missing_transactions{declaration.request_id, {}});
    auto const supplied = fixture.jds.receive<provide_missing_transactions_success>();
    REQUIRE(supplied.request_id == declaration.request_id);
    REQUIRE(supplied.transaction_list.empty());
    fixture.jds.send(declare_mining_job_success{declaration.request_id, {9, 8, 7}});
    auto const custom = fixture.pool.receive<set_custom_mining_job>();
    REQUIRE(custom.channel_id == 7);
    REQUIRE(custom.request_id == allocation.request_id);
    REQUIRE(custom.prev_hash == block.hash());

    // The tip can publish a solo job before the new pool coinbase. Take the
    // job that pays the pool on this block.
    std::optional<downstream_work> accepted_work;
    for (int attempt = 0; attempt < 3 && ! accepted_work; ++attempt) {
        auto const job = miner.next();
        if (pays(coinbase_from(job), pool_script(), 2) && job.prev.prev_hash == block.hash()) {
            accepted_work = job;
        }
    }
    REQUIRE(accepted_work);

    data_chunk const extranonce(miner.opened.extranonce_size, 0x5b);
    auto const nonce = find_nonce(*accepted_work, extranonce, false);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 2,
        accepted_work->job.job_id, nonce, accepted_work->prev.ntime_start,
        accepted_work->job.version, extranonce}));
    auto const local = receive_plain<submit_shares_success>(fixture, miner.socket);
    REQUIRE(local.channel_id == miner.opened.channel_id);
    REQUIRE(local.last_sequence_number == 2);
    expect_idle(fixture, fixture.pool.socket);

    constexpr uint32_t accepted_job_id = 91;
    fixture.pool.send(set_custom_mining_job_success{custom.channel_id,
        custom.request_id, accepted_job_id});
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.channel_id == 7);
    REQUIRE(forwarded.sequence_number == 0);
    REQUIRE(forwarded.job_id == accepted_job_id);
    REQUIRE(forwarded.nonce == nonce);
    REQUIRE(forwarded.ntime == accepted_work->prev.ntime_start);
    REQUIRE(forwarded.version == accepted_work->job.version);
    data_chunk full = accepted_work->prefix;
    full.insert(full.end(), extranonce.begin(), extranonce.end());
    // Reconstructs the 0x5b roll from this declaration. The cached share rolled 0x5a.
    REQUIRE(forwarded.extranonce == data_chunk(full.begin() + 8, full.end()));
    // A second frame would be the share mined on the replaced coinbase.
    REQUIRE(fixture.pool.frames.empty());
    fixture.pool.send(submit_shares_success{forwarded.channel_id,
        forwarded.sequence_number, 1, 1});
    auto pool_quiet = fixture.begin_read(fixture.pool.socket, 1);
    auto miner_quiet = fixture.begin_read(miner.socket, 1);
    CHECK_FALSE(fixture.wait_until([&] {
        return pool_quiet->done || miner_quiet->done;
    }, 300ms));
}

TEST_CASE("sv2 JD ignores a share error for a sequence it never sent", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_unknown_share");
    client_fixture fixture;
    auto const configuration = settings(fixture.pool, fixture.jds);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    miner_link miner(fixture, provider.port);
    auto const declared = declare_local_template(fixture, miner, true);
    fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
        declared.custom.request_id, 77});

    captured_log log;
    fixture.pool.send(submit_shares_error{declared.custom.channel_id, 99, "never-sent"});
    REQUIRE(fixture.wait_until([&] {
        return log.count("ignoring share error for unknown sequence 99") >= 1;
    }));
    REQUIRE(log.count("pool refused a valid share") == 0);

    // The pool is still the one paying this job. A later share is forwarded.
    data_chunk const extranonce(miner.opened.extranonce_size, 0x5a);
    auto const nonce = find_nonce(declared.pooled, extranonce, false);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
        declared.pooled.job.job_id, nonce, declared.pooled.prev.ntime_start,
        declared.pooled.job.version, extranonce}));
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.channel_id == declared.custom.channel_id);
    REQUIRE(forwarded.sequence_number == 0);
    REQUIRE(forwarded.job_id == 77);
    REQUIRE(forwarded.nonce == nonce);
}

TEST_CASE("sv2 JD ignores a share error for a replaced declaration", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_stale_share_error");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    // The accepted declaration has to survive connect_bodies, which publishes
    // the tip that replaces it. This test does not wait that deadline out.
    configuration.jd.response_timeout_seconds = 30;
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    miner_link miner(fixture, provider.port);
    auto const first = declare_local_template(fixture, miner, true);
    fixture.pool.send(set_custom_mining_job_success{first.custom.channel_id,
        first.custom.request_id, 77});

    data_chunk const first_extranonce(miner.opened.extranonce_size, 0x5a);
    auto const first_nonce = find_nonce(first.pooled, first_extranonce, false);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
        first.pooled.job.job_id, first_nonce, first.pooled.prev.ntime_start,
        first.pooled.job.version, first_extranonce}));
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.channel_id == 7);
    REQUIRE(forwarded.sequence_number == 0);
    REQUIRE(forwarded.job_id == 77);
    fixture.pool.send(submit_shares_success{forwarded.channel_id,
        forwarded.sequence_number, 1, 1});

    auto const block = test::mine_block(first.pooled.prev.prev_hash, 2,
        uint32_t(zulu_time()), 0, {}, 0);
    std::vector<domain::chain::block> const blocks{block};
    REQUIRE(chain->organizer().add_headers(test::headers_of(blocks)).headers_added == 1);
    test::persist_headers(*chain, blocks, 2);
    test::connect_bodies(*chain, blocks, 2);
    auto const view = chain->chain().chain_view();
    REQUIRE(view);
    REQUIRE(view->tip_hash == block.hash());

    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    REQUIRE(allocation.request_id != first.custom.request_id);
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    auto const declaration = fixture.jds.receive<declare_mining_job>();
    REQUIRE(declaration.request_id == allocation.request_id);
    fixture.jds.send(provide_missing_transactions{declaration.request_id, {}});
    auto const supplied = fixture.jds.receive<provide_missing_transactions_success>();
    REQUIRE(supplied.request_id == declaration.request_id);
    REQUIRE(supplied.transaction_list.empty());
    fixture.jds.send(declare_mining_job_success{declaration.request_id, {9, 8, 7}});
    auto const custom = fixture.pool.receive<set_custom_mining_job>();
    REQUIRE(custom.channel_id == 7);
    REQUIRE(custom.request_id == allocation.request_id);
    REQUIRE(custom.prev_hash == block.hash());

    std::optional<downstream_work> accepted_work;
    for (int attempt = 0; attempt < 3 && ! accepted_work; ++attempt) {
        auto const job = miner.next();
        if (pays(coinbase_from(job), pool_script(), 2) && job.prev.prev_hash == block.hash()) {
            accepted_work = job;
        }
    }
    REQUIRE(accepted_work);
    constexpr uint32_t accepted_job_id = 91;
    fixture.pool.send(set_custom_mining_job_success{custom.channel_id,
        custom.request_id, accepted_job_id});

    captured_log log;
    fixture.pool.send(submit_shares_error{forwarded.channel_id,
        forwarded.sequence_number, "replaced-job"});
    REQUIRE(fixture.wait_until([&] {
        return log.count("ignoring stale share error replaced-job for sequence 0") >= 1;
    }));
    REQUIRE(log.count("pool refused a valid share") == 0);

    data_chunk const extranonce(miner.opened.extranonce_size, 0x5b);
    auto const nonce = find_nonce(*accepted_work, extranonce, false);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 2,
        accepted_work->job.job_id, nonce, accepted_work->prev.ntime_start,
        accepted_work->job.version, extranonce}));
    auto const again = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(again.channel_id == 7);
    REQUIRE(again.sequence_number == 1);
    REQUIRE(again.job_id == accepted_job_id);
    REQUIRE(again.nonce == nonce);
}

TEST_CASE("sv2 JD falls back when the pool rejects a share on the accepted declaration", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_share_refused");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    std::string error;
    auto const payout = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(payout);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    miner_link miner(fixture, provider.port);
    auto const declared = declare_local_template(fixture, miner, true);
    fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
        declared.custom.request_id, 77});
    data_chunk const extranonce(miner.opened.extranonce_size, 0x5a);
    auto const nonce = find_nonce(declared.pooled, extranonce, false);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
        declared.pooled.job.job_id, nonce, declared.pooled.prev.ntime_start,
        declared.pooled.job.version, extranonce}));
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.sequence_number == 0);
    REQUIRE(forwarded.job_id == 77);

    captured_log log;
    fixture.pool.send(submit_shares_error{forwarded.channel_id, forwarded.sequence_number,
        std::string("valid\nshare")});
    require_solo_again(miner, declared.pooled, payout->script);
    REQUIRE(log.count("pool refused a valid share on an accepted job (valid?share)") >= 1);
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    require_no_reconnect(fixture);
}

TEST_CASE("sv2 JD closes on a malformed or misaddressed share reply", "[sv2 jd]") {
    enum class fault { truncated, truncated_error, success_channel, error_channel };
    auto which = fault::truncated;
    SECTION("truncated SubmitShares.Success") {}
    SECTION("truncated SubmitShares.Error") { which = fault::truncated_error; }
    SECTION("SubmitShares.Success for the group channel") { which = fault::success_channel; }
    SECTION("SubmitShares.Error for the group channel") { which = fault::error_channel; }
    char const* tag = "sv2_jd_bad_success";
    char const* phrase = "malformed share success";
    if (which == fault::truncated_error) {
        tag = "sv2_jd_truncated_share_error";
        phrase = "malformed share error";
    }
    if (which == fault::success_channel) {
        tag = "sv2_jd_bad_channel";
        phrase = "share success for another channel";
    }
    if (which == fault::error_channel) {
        tag = "sv2_jd_bad_error_channel";
        phrase = "share error for another channel";
    }
    auto chain = started_chain(tag);
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    std::string error;
    auto const payout = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(payout);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    miner_link miner(fixture, provider.port);
    auto const declared = declare_local_template(fixture, miner, true);
    fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
        declared.custom.request_id, 77});

    // accept() opened channel 7 and group channel 8. The group id is not the
    // upstream channel, even though mining updates accept either.
    captured_log log;
    if (which == fault::truncated) fixture.pool.send_plain(truncated_share_success());
    else if (which == fault::truncated_error) fixture.pool.send_plain(truncated_share_error());
    else if (which == fault::success_channel) fixture.pool.send(submit_shares_success{8, 0, 1, 1});
    else fixture.pool.send(submit_shares_error{8, 0, "other-channel"});
    require_solo_again(miner, declared.pooled, payout->script);
    REQUIRE(fixture.wait_until([&] { return log.count(phrase) >= 1; }));
    REQUIRE(log.count("pool refused a valid share") == 0);
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    require_no_reconnect(fixture);
}

TEST_CASE("sv2 JD falls back when the pool changes the extranonce prefix of declared work", "[sv2 jd]") {
    bool group = false;
    SECTION("channel prefix") {}
    SECTION("group prefix") { group = true; }
    auto chain = started_chain(group ? "sv2_jd_prefix_group" : "sv2_jd_prefix_channel");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    std::string error;
    auto const payout = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(payout);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    miner_link miner(fixture, provider.port);
    auto const declared = declare_local_template(fixture, miner, true);

    // accept() stored an 8-byte prefix of 0x12 and an extranonce size of 8.
    // Eight bytes of 0x34 still fit (8 + 8 <= 32) and are not that prefix.
    data_chunk const prefix(8, 0x34);
    captured_log log;
    fixture.pool.send(set_extranonce_prefix{group ? uint32_t{8} : uint32_t{7}, prefix});
    if ( ! group) {
        require_solo_again(miner, declared.pooled, payout->script);
        REQUIRE(fixture.wait_until([&] {
            return log.count("pool changed the extranonce prefix of declared work") >= 1;
        }));
        fixture.eof(fixture.pool.socket);
        fixture.eof(fixture.jds.socket);
        require_no_reconnect(fixture);
        return;
    }

    // A prefix addressed to the group is ignored, so this declaration keeps
    // the prefix it was built with and neither upstream closes.
    auto pool_quiet = fixture.begin_read(fixture.pool.socket, 1);
    auto jds_quiet = fixture.begin_read(fixture.jds.socket, 1);
    auto miner_quiet = fixture.begin_read(miner.socket, 1);
    REQUIRE_FALSE(fixture.wait_until([&] {
        return pool_quiet->done || jds_quiet->done || miner_quiet->done;
    }, 300ms));
    REQUIRE(log.count("pool changed the extranonce prefix of declared work") == 0);
    // Cancel so the next read on each socket starts at a frame boundary.
    std::error_code cancel_error;
    fixture.pool.socket.cancel(cancel_error);
    REQUIRE_FALSE(cancel_error);
    fixture.jds.socket.cancel(cancel_error);
    REQUIRE_FALSE(cancel_error);
    miner.socket.cancel(cancel_error);
    REQUIRE_FALSE(cancel_error);
    REQUIRE(fixture.wait_until([&] {
        return pool_quiet->done && jds_quiet->done && miner_quiet->done;
    }));

    // The custom job is still in flight. Accepting it and forwarding a share
    // is the declaration staying up after the ignored prefix.
    fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
        declared.custom.request_id, 77});
    data_chunk const extranonce(miner.opened.extranonce_size, 0x5a);
    auto const nonce = find_nonce(declared.pooled, extranonce, false);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
        declared.pooled.job.job_id, nonce, declared.pooled.prev.ntime_start,
        declared.pooled.job.version, extranonce}));
    auto const local = receive_plain<submit_shares_success>(fixture, miner.socket);
    REQUIRE(local.channel_id == miner.opened.channel_id);
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.channel_id == 7);
    REQUIRE(forwarded.sequence_number == 0);
    REQUIRE(forwarded.job_id == 77);
    REQUIRE(forwarded.nonce == nonce);
    REQUIRE(log.count("pool changed the extranonce prefix of declared work") == 0);
}

TEST_CASE("sv2 JD pushes a block found before DeclareMiningJob.Success once", "[sv2 jd]") {
    // SetCustomMiningJob is sent only from install(), and full-template install()
    // runs only on DeclareMiningJob.Success. A block therefore cannot be found
    // after SetCustomMiningJob.Success and before that declare success. The
    // order this client does produce is a block on the optimistic pool coinbase
    // while the declare is still outstanding.
    //
    // No submission pipeline: organize() would connect the block and the new tip
    // would retire this declaration before the success could be delivered. The
    // proof's block bit is set by the mining engine before that connect.
    auto chain = started_chain("sv2_jd_early_block");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    configuration.jd.response_timeout_seconds = 30;
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    miner_link miner(fixture, provider.port);
    auto const declared = declare_local_template(fixture, miner, true, false);
    data_chunk const extranonce(miner.opened.extranonce_size, 0x5a);
    auto const nonce = find_nonce(declared.pooled, extranonce, true);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
        declared.pooled.job.job_id, nonce, declared.pooled.prev.ntime_start,
        declared.pooled.job.version, extranonce}));
    auto const local = receive_plain<submit_shares_success>(fixture, miner.socket);
    REQUIRE(local.channel_id == miner.opened.channel_id);
    REQUIRE(local.last_sequence_number == 1);
    expect_idle(fixture, fixture.jds.socket);

    fixture.jds.send(declare_mining_job_success{declared.declaration.request_id, {9, 8, 7}});
    auto const pushed = fixture.jds.receive<push_solution>();
    REQUIRE(fixture.jds.frames.empty());
    data_chunk full = declared.pooled.prefix;
    full.insert(full.end(), extranonce.begin(), extranonce.end());
    REQUIRE(full.size() == 8 + jd_session::min_extranonce_size);
    REQUIRE(pushed.extranonce == full);
    REQUIRE(pushed.prev_hash == declared.pooled.prev.prev_hash);
    REQUIRE(pushed.nonce == nonce);
    REQUIRE(pushed.ntime == declared.pooled.prev.ntime_start);
    REQUIRE(pushed.nbits == declared.pooled.prev.nbits);
    REQUIRE(pushed.version == declared.pooled.job.version);

    auto const custom = fixture.pool.receive<set_custom_mining_job>();
    REQUIRE(custom.channel_id == 7);
    REQUIRE(custom.request_id == declared.declaration.request_id);
    REQUIRE(custom.mining_job_token == data_chunk{9, 8, 7});
    constexpr uint32_t custom_job_id = 77;
    fixture.pool.send(set_custom_mining_job_success{custom.channel_id,
        custom.request_id, custom_job_id});
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.channel_id == 7);
    REQUIRE(forwarded.sequence_number == 0);
    REQUIRE(forwarded.job_id == custom_job_id);
    REQUIRE(forwarded.nonce == nonce);
    REQUIRE(forwarded.extranonce == data_chunk(full.begin() + 8, full.end()));
    // SetCustomMiningJob.Success flushes the cached share and must not push again.
    REQUIRE(fixture.jds.frames.empty());
    expect_idle(fixture, fixture.jds.socket);

    // The pool may reach this block's tip, from the PushSolution above, before
    // it answers the share that found it, and call the job unknown. That is a
    // race on the share's own block, not a refusal of current work.
    captured_log log;
    fixture.pool.send(submit_shares_error{forwarded.channel_id, forwarded.sequence_number,
        "invalid-job-id"});
    REQUIRE(fixture.wait_until([&] {
        return log.count("ignoring share error invalid-job-id for the block found by sequence 0") >= 1;
    }));
    REQUIRE(log.count("pool refused a valid share") == 0);
    expect_idle(fixture, fixture.jds.socket);
}

TEST_CASE("sv2 JD coinbase-only does not push a block to the JDS", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_coinbase_block");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    configuration.jd.mode = "coinbase-only";
    // The custom job is outstanding across the share's organize check.
    configuration.jd.response_timeout_seconds = 30;
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, false);
    miner_link miner(fixture, provider.port);
    auto const declared = declare_local_template(fixture, miner, false);
    data_chunk const extranonce(miner.opened.extranonce_size, 0x5a);
    auto const nonce = find_nonce(declared.pooled, extranonce, true);
    fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
        declared.pooled.job.job_id, nonce, declared.pooled.prev.ntime_start,
        declared.pooled.job.version, extranonce}));
    auto const local = receive_plain<submit_shares_success>(fixture, miner.socket);
    REQUIRE(local.last_sequence_number == 1);
    expect_idle(fixture, fixture.jds.socket);

    fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
        declared.custom.request_id, 77});
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.channel_id == 7);
    REQUIRE(forwarded.sequence_number == 0);
    REQUIRE(forwarded.job_id == 77);
    REQUIRE(forwarded.nonce == nonce);
    REQUIRE(fixture.jds.frames.empty());
    expect_idle(fixture, fixture.jds.socket);
}

TEST_CASE("sv2 JD opens a new channel on the live pool coinbase without SetExtranoncePrefix", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_open_pool");
    client_fixture fixture;
    auto const configuration = settings(fixture.pool, fixture.jds);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, true);
    // Opens while the declaration is still solo. The switch that follows is
    // the SetExtranoncePrefix path; this case starts a connection afterwards.
    miner_link early(fixture, provider.port);
    auto const declared = declare_local_template(fixture, early, true);
    fixture.pool.send(set_custom_mining_job_success{declared.custom.channel_id,
        declared.custom.request_id, 77});

    ::asio::ip::tcp::socket late(fixture.io);
    auto connected = std::make_shared<operation>();
    late.async_connect({::asio::ip::address_v4::loopback(), provider.port},
        [connected](std::error_code error) {
            connected->error = error;
            connected->done = true;
        });
    REQUIRE(fixture.wait_until([&] { return connected->done; }));
    REQUIRE_FALSE(connected->error);
    setup_connection setup;
    setup.protocol = setup_connection::protocol_mining;
    setup.min_version = 2;
    setup.max_version = 2;
    fixture.write(late, frame(setup));
    REQUIRE(receive_plain<setup_connection_success>(fixture, late).used_version == 2);
    hash_digest target;
    target.fill(0xff);
    fixture.write(late, frame(open_extended_mining_channel{1, "late", 0.f, target,
        node::mining::jd_downstream_extranonce_size}));

    auto const opened_frame = read_plain_frame(fixture, late);
    auto const job_frame = read_plain_frame(fixture, late);
    auto const prev_frame = read_plain_frame(fixture, late);
    // The whole activation, in order: success, future job, SetNewPrevHash.
    // A corrected prefix is its own frame and would land before the job.
    REQUIRE(opened_frame.header.msg_type == open_extended_mining_channel_success::message_type);
    REQUIRE(job_frame.header.msg_type == new_extended_mining_job::message_type);
    REQUIRE(prev_frame.header.msg_type == mining_set_new_prev_hash::message_type);
    auto const opened = decode<open_extended_mining_channel_success>(opened_frame.bytes);
    REQUIRE(opened.request_id == 1);
    REQUIRE(opened.extranonce_size == node::mining::jd_downstream_extranonce_size);
    REQUIRE(opened.extranonce_prefix.size() == 8 + node::mining::jd_allocation_size);
    REQUIRE(std::equal(opened.extranonce_prefix.begin(), opened.extranonce_prefix.begin() + 8,
        data_chunk(8, 0x12).begin()));
    auto const job = decode<new_extended_mining_job>(job_frame.bytes);
    auto const prev = decode<mining_set_new_prev_hash>(prev_frame.bytes);
    REQUIRE(job.channel_id == opened.channel_id);
    REQUIRE_FALSE(job.ntime_start.has_value());
    REQUIRE(prev.channel_id == opened.channel_id);
    REQUIRE(prev.job_id == job.job_id);
    downstream_work const work{opened.extranonce_prefix, opened.extranonce_size, job, prev};
    REQUIRE(pays(coinbase_from(work), pool_script(), 2));
}

TEST_CASE("sv2 JD installs a pool publication once while a template fetch is in flight", "[sv2 jd]") {
    // Coinbase-only publishes on the allocate token, which is the notice a
    // downstream fetch can observe. One channel gets that coinbase once.
    // Opening another channel on the same connection must not install it again:
    // a second apply would emit another job id for the first channel.
    auto chain = started_chain("sv2_jd_publish_once");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    configuration.jd.mode = "coinbase-only";
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, false);
    miner_link miner(fixture, provider.port);
    auto const declared = declare_local_template(fixture, miner, false);
    REQUIRE(pays(coinbase_from(declared.pooled), pool_script(), 2));
    std::error_code error;
    fixture.io.restart();
    fixture.io.poll();
    REQUIRE(miner.socket.available(error) == 0);
    REQUIRE_FALSE(error);

    hash_digest target;
    target.fill(0xff);
    fixture.write(miner.socket, frame(open_extended_mining_channel{2, "other", 0.f, target,
        node::mining::jd_downstream_extranonce_size}));
    auto const opened = receive_plain<open_extended_mining_channel_success>(fixture, miner.socket);
    REQUIRE(opened.request_id == 2);
    REQUIRE(opened.channel_id != miner.opened.channel_id);
    REQUIRE(opened.extranonce_prefix.size() == declared.pooled.prefix.size());
    REQUIRE(opened.extranonce_prefix != declared.pooled.prefix);
    REQUIRE(std::equal(opened.extranonce_prefix.begin(), opened.extranonce_prefix.begin() + 8,
        data_chunk(8, 0x12).begin()));
    auto const job_frame = read_plain_frame(fixture, miner.socket);
    auto const prev_frame = read_plain_frame(fixture, miner.socket);
    REQUIRE(job_frame.header.msg_type == new_extended_mining_job::message_type);
    REQUIRE(prev_frame.header.msg_type == mining_set_new_prev_hash::message_type);
    auto const job = decode<new_extended_mining_job>(job_frame.bytes);
    auto const prev = decode<mining_set_new_prev_hash>(prev_frame.bytes);
    REQUIRE(job.channel_id == opened.channel_id);
    REQUIRE(prev.channel_id == opened.channel_id);
    REQUIRE(prev.job_id == job.job_id);
    REQUIRE(job.job_id != declared.pooled.job.job_id);
    REQUIRE(job.coinbase_tx_prefix == declared.pooled.job.coinbase_tx_prefix);
    REQUIRE(job.coinbase_tx_suffix == declared.pooled.job.coinbase_tx_suffix);
    downstream_work const second{opened.extranonce_prefix, opened.extranonce_size, job, prev};
    REQUIRE(pays(coinbase_from(second), pool_script(), 2));
    fixture.io.restart();
    fixture.io.poll();
    REQUIRE(miner.socket.available(error) == 0);
    REQUIRE_FALSE(error);
}
