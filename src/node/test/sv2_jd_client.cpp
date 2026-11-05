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

#include <kth/blockchain/interface/block_chain.hpp>
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
#include <kth/node/sv2/jd_failover.hpp>
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
    if constexpr (requires { Message::channel_message; }) {
        return Message::channel_message ? message_header::channel_msg_flag : 0;
    }
    return 0;
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

    void eof_after_pending(::asio::ip::tcp::socket& socket) {
        auto const end = std::chrono::steady_clock::now() + 5s;
        for (;;) {
            auto const now = std::chrono::steady_clock::now();
            REQUIRE(now < end);
            auto const state = begin_read(socket, 8192, false);
            REQUIRE(wait_until([&] { return state->done; },
                std::chrono::duration_cast<std::chrono::milliseconds>(end - now)));
            if (state->error) {
                REQUIRE(state->error == ::asio::error::eof);
                REQUIRE(state->size == 0);
                return;
            }
            REQUIRE(state->size != 0);
        }
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

    void accept_again() {
        std::error_code error;
        socket.close(error);
        REQUIRE_FALSE(error);
        transport.reset();
        frames.clear();
        accepted = std::make_shared<operation>();
        acceptor.async_accept(socket, [state = accepted](std::error_code error) {
            state->error = error;
            state->done = true;
        });
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
        send_frame(frame(message));
    }

    void send_frame(data_chunk const& plaintext) {
        auto const bytes = transport->encrypt_frame(plaintext);
        REQUIRE(bytes);
        // A protocol reply can straddle reads just like its encrypted tags.
        driver.write(socket, data_chunk(bytes->begin(), bytes->end() - 1));
        driver.write(socket, data_chunk(bytes->end() - 1, bytes->end()));
    }

    void send_plain(data_chunk plain) {
        send_frame(plain);
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

std::string upstream(fake_upstream const& pool, fake_upstream const& jds,
    std::string const& identity = "account.worker", std::string const& mode = "full-template") {
    return "127.0.0.1:" + std::to_string(pool.port) + "|"
        + noise_authority_key(pool.authority.public_key()) + "|127.0.0.1:"
        + std::to_string(jds.port) + "|" + noise_authority_key(jds.authority.public_key())
        + "|" + identity + "|" + mode;
}

node::sv2_settings settings(fake_upstream const& first_pool, fake_upstream const& first_jds,
    fake_upstream const& second_pool, fake_upstream const& second_jds,
    std::string const& mode = "full-template") {
    auto value = settings(first_pool, first_jds);
    value.jd.mining_address.clear();
    value.jd.mining_authority_public_key.clear();
    value.jd.jds_address.clear();
    value.jd.jds_authority_public_key.clear();
    value.jd.user_identity.clear();
    value.jd.mode.clear();
    value.jd.upstreams = {upstream(first_pool, first_jds, "account.worker", mode),
        upstream(second_pool, second_jds, "account.worker", mode)};
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
        await(ready);
        if (success) REQUIRE_NOTHROW(ready.get());
        else REQUIRE_THROWS_AS(ready.get(), std::system_error);
        client->stop();
        await(result);
        REQUIRE_NOTHROW(result.get());
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

void handshake_upstreams(fake_upstream& pool, fake_upstream& jds, bool full,
    hash_digest const* target = nullptr, std::string const& identity = "account.worker") {
    pool.handshake();
    jds.handshake();
    auto const mining = pool.receive<setup_connection>();
    REQUIRE(mining.protocol == setup_connection::protocol_mining);
    REQUIRE(mining.endpoint_port == pool.port);
    REQUIRE(mining.flags == (mining_flag_requires_work_selection | mining_flag_requires_version_rolling));
    auto const declaration = jds.receive<setup_connection>();
    REQUIRE(declaration.protocol == setup_connection::protocol_job_declaration);
    REQUIRE(declaration.endpoint_port == jds.port);
    REQUIRE(declaration.flags == (full ? 1u : 0u));
    jds.send(setup_connection_success{2, 0});
    pool.send(setup_connection_success{2, mining_flag_requires_extended_channels});
    auto const request = pool.receive<open_extended_mining_channel>();
    REQUIRE(request.user_identity == identity);
    REQUIRE(request.min_extranonce_size == jd_session::min_extranonce_size);
    hash_digest maximum;
    maximum.fill(0xff);
    pool.send(open_extended_mining_channel_success{request.request_id, 7,
        target == nullptr ? maximum : *target,
        jd_session::min_extranonce_size, data_chunk(8, 0x12), 8});
}

void handshake_upstreams(client_fixture& fixture, bool full, hash_digest const* target = nullptr) {
    handshake_upstreams(fixture.pool, fixture.jds, full, target);
}

set_custom_mining_job accept_coinbase_template(fake_upstream& pool, fake_upstream& jds,
    uint32_t job_id = 77) {
    auto const allocation = jds.receive<allocate_mining_job_token>();
    jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    auto const custom = pool.receive<set_custom_mining_job>();
    REQUIRE(custom.request_id == allocation.request_id);
    pool.send(set_custom_mining_job_success{custom.channel_id, custom.request_id, job_id});
    return custom;
}

node::mining::pool_proof proof_for(jd_notice const& notice) {
    node::mining::pool_proof proof;
    proof.work_id = notice.pooled.work_id;
    // Coinbase generation the pool declared. A zero here is not "unset":
    // the client drops a proof whose generation is not that declaration.
    proof.generation = notice.pooled.generation;
    proof.prev_hash = notice.previous;
    proof.upstream_extranonce.resize(notice.pooled.upstream_extranonce_size, 0x44);
    proof.version = notice.pooled.tmpl.version;
    proof.ntime = notice.pooled.tmpl.current_time;
    proof.bits = notice.pooled.tmpl.bits;
    return proof;
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
declared_template declare_local_template(io_driver& driver, fake_upstream& pool,
    fake_upstream& jds, miner_link& miner, bool full, bool finish = true) {
    auto const allocation = jds.receive<allocate_mining_job_token>();
    REQUIRE(allocation.user_identifier == "account.worker");
    REQUIRE(allocation.request_id != 0);
    // Coinbase-only must not follow the token with DeclareMiningJob. Arm the
    // read before the token succeeds, while the JDS socket is still idle.
    std::shared_ptr<operation> quiet;
    if ( ! full) quiet = driver.begin_read(jds.socket, 1);
    jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    declare_mining_job declaration{};
    if (full) {
        declaration = jds.receive<declare_mining_job>();
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
        jds.send(provide_missing_transactions{declaration.request_id, {}});
        auto const supplied = jds.receive<provide_missing_transactions_success>();
        REQUIRE(supplied.request_id == declaration.request_id);
        REQUIRE(supplied.transaction_list.empty());
        jds.send(declare_mining_job_success{declaration.request_id, {9, 8, 7}});
    }
    auto const custom = pool.receive<set_custom_mining_job>();
    if (quiet) {
        CHECK_FALSE(quiet->done);
        // Join the silence read before callers read the JDS socket again.
        std::error_code error;
        jds.socket.cancel(error);
        REQUIRE_FALSE(error);
        REQUIRE(driver.wait_until([&] { return quiet->done; }));
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

declared_template declare_local_template(client_fixture& fixture, miner_link& miner,
    bool full, bool finish = true) {
    return declare_local_template(fixture, fixture.pool, fixture.jds, miner, full, finish);
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

void require_no_immediate_retry(client_fixture& fixture) {
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

void require_no_reconnect(client_fixture& fixture) {
    require_no_immediate_retry(fixture);
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

} // namespace

TEST_CASE("sv2 JD client negotiates both modes and backs off after either upstream disconnect", "[sv2 jd]") {
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
            auto const request = fixture.open();
            fixture.accept(request);
            fixture.await(fixture.ready);
            REQUIRE_NOTHROW(fixture.ready.get());
            // An EOF after the one channel request also proves that no second
            // channel request was queued while both upstreams were ready.
            (pool_drop ? fixture.pool : fixture.jds).finish_writing();
            fixture.eof(fixture.pool.socket);
            fixture.eof(fixture.jds.socket);
            require_no_immediate_retry(fixture);
            CHECK(fixture.result.wait_for(0s) != std::future_status::ready);
            fixture.client->stop();
            fixture.await(fixture.result);
            REQUIRE_NOTHROW(fixture.result.get());
        }
    }
}

TEST_CASE("sv2 JD starts the first configured entry and advances after connection or setup failure", "[sv2 jd]") {
    enum class trigger { connect, pool_setup, jds_setup, fixed_version, channel, timeout, latency, pool_drop, jds_drop };
    auto which = trigger::connect;
    SECTION("connect failure") {}
    SECTION("pool setup refused") { which = trigger::pool_setup; }
    SECTION("JDS setup refused") { which = trigger::jds_setup; }
    SECTION("required version rolling missing") { which = trigger::fixed_version; }
    SECTION("extended channel refused") { which = trigger::channel; }
    SECTION("setup message timeout") { which = trigger::timeout; }
    SECTION("setup latency exceeds threshold") { which = trigger::latency; }
    SECTION("pool dropped") { which = trigger::pool_drop; }
    SECTION("JDS dropped") { which = trigger::jds_drop; }
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds);
    configuration.jd.response_timeout_seconds = 1;
    if (which == trigger::latency) {
        configuration.jd.response_timeout_seconds = 3;
        configuration.jd.max_latency_milliseconds = 1000;
    }
    configuration.jd.upstreams.back() = upstream(backup_pool, backup_jds, "backup.worker", "coinbase-only");
    if (which == trigger::connect) {
        std::error_code error;
        fixture.pool.acceptor.close(error);
        REQUIRE_FALSE(error);
    }
    fixture.start(configuration);
    if (which != trigger::connect) {
        fixture.setup();
        (void)fixture.jds.receive<setup_connection>();
        CHECK_FALSE(backup_pool.accepted->done);
        CHECK_FALSE(backup_jds.accepted->done);
        if (which == trigger::pool_setup) {
            fixture.pool.send(setup_connection_error{mining_flag_requires_work_selection, "unsupported-feature-flags"});
        } else if (which == trigger::jds_setup) {
            fixture.jds.send(setup_connection_error{1, "unsupported-feature-flags"});
        } else if (which == trigger::fixed_version) {
            fixture.pool.send(setup_connection_success{2, mining_flag_requires_fixed_version});
        } else if (which == trigger::timeout) {
            fixture.jds.send(setup_connection_success{2, 0});
        } else {
            fixture.jds.send(setup_connection_success{2, 0});
            if (which == trigger::latency) {
                auto const delayed_until = std::chrono::steady_clock::now() + 1100ms;
                REQUIRE(fixture.wait_until([&] {
                    return std::chrono::steady_clock::now() >= delayed_until;
                }, 2s));
                fixture.pool.send(setup_connection_success{2, mining_flag_requires_extended_channels});
            } else {
                auto const request = fixture.open();
                if (which == trigger::channel) {
                    fixture.pool.send(open_mining_channel_error{request.request_id, "unknown-user"});
                } else {
                    fixture.accept(request);
                    fixture.await(fixture.ready);
                    REQUIRE_NOTHROW(fixture.ready.get());
                    (which == trigger::pool_drop ? fixture.pool : fixture.jds).finish_writing();
                }
            }
        }
        fixture.eof(fixture.pool.socket);
        fixture.eof(fixture.jds.socket);
    }
    handshake_upstreams(backup_pool, backup_jds, false, nullptr, "backup.worker");
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == std::optional<size_t>{1}
            && status->entries[1].health == jd_health::healthy;
    }));
    if (fixture.ready.valid()) {
        fixture.await(fixture.ready);
        REQUIRE_NOTHROW(fixture.ready.get());
    }
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
    fixture.eof(backup_pool.socket);
    fixture.eof(backup_jds.socket);
}

TEST_CASE("sv2 JD flags an authority mismatch and moves to the next entry without an immediate retry", "[sv2 jd]") {
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds);
    auto const wrong_key = noise_authority_key(keypair(9).public_key());
    auto& first = configuration.jd.upstreams.front();
    auto const actual_key = noise_authority_key(fixture.pool.authority.public_key());
    auto const offset = first.find(actual_key);
    REQUIRE(offset != std::string::npos);
    first.replace(offset, actual_key.size(), wrong_key);
    fixture.start(configuration);
    fixture.jds.handshake();
    (void)fixture.jds.receive<setup_connection>();
    fixture.pool.handshake();
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    handshake_upstreams(backup_pool, backup_jds, true);
    fixture.await(fixture.ready);
    REQUIRE_NOTHROW(fixture.ready.get());
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == std::optional<size_t>{1}
            && status->entries[1].health == jd_health::healthy;
    }));
    auto const status = fixture.client->status();
    REQUIRE(status->entries.size() == 2);
    REQUIRE(status->entries.front().health == jd_health::authority_hold);
    REQUIRE_FALSE(status->entries.front().last_error.empty());
    REQUIRE(status->entries.front().retry_at > std::chrono::steady_clock::now());
    require_no_immediate_retry(fixture);
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
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
    if (stage != silence::jds_setup) fixture.jds.send(setup_connection_success{2, 0});
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
    auto configuration = settings(fixture.pool, fixture.jds);
    // DeclareMiningJob.Error demotes this entry. The client keeps running, so
    // the hold has to outlast the successor below or that tip could revive it.
    configuration.jd.retry_initial_seconds = 3600;
    configuration.jd.retry_max_seconds = 3600;
    fixture.start(configuration, &chain->chain());
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
        auto const status = fixture.client->status();
        auto const notice = fixture.client->notice();
        return status && notice && ! notice->pool && notice->generation > pooled->generation
            && status->entries.size() == 1
            && status->entries[0].health == jd_health::backoff
            && ! status->active && ! status->probe
            && fixture.result.wait_for(0s) != std::future_status::ready;
    }, 1s));
    auto const solo = fixture.client->notice();
    REQUIRE(solo);
    REQUIRE_FALSE(solo->pool);
    REQUIRE(solo->generation > pooled->generation);
    auto const demoted = fixture.client->status();
    REQUIRE(demoted);
    REQUIRE(demoted->entries[0].health == jd_health::backoff);
    REQUIRE(demoted->entries[0].last_error.find("JDS refused the local template") != std::string::npos);
    REQUIRE(demoted->entries[0].retry_at > std::chrono::steady_clock::now());
    REQUIRE_FALSE(demoted->active);
    REQUIRE_FALSE(demoted->probe);
    REQUIRE(fixture.result.wait_for(0s) != std::future_status::ready);
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

    // The successor is a real tip on this client's strand. It must not open
    // either socket again or replace the solo notice published for the error.
    auto const block = test::mine_block(solo->previous, 2, uint32_t(zulu_time()), 0, {}, 0);
    std::vector<domain::chain::block> const blocks{block};
    REQUIRE(chain->organizer().add_headers(test::headers_of(blocks)).headers_added == 1);
    test::persist_headers(*chain, blocks, 2);
    test::connect_bodies(*chain, blocks, 2);
    auto const view = chain->chain().chain_view();
    REQUIRE(view);
    REQUIRE(view->tip_hash == block.hash());
    REQUIRE_FALSE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return accepted->done || jd_accepted->done
            || fixture.client->notice() != solo
            || ! status
            || status->entries.size() != 1
            || status->entries[0].health != jd_health::backoff
            || status->active.has_value()
            || status->probe.has_value()
            || fixture.result.wait_for(0s) == std::future_status::ready;
    }, 50ms));

    fixture.client->stop();
    REQUIRE(fixture.wait_until([&] {
        return fixture.result.wait_for(0s) == std::future_status::ready;
    }, 1s));
    REQUIRE_NOTHROW(fixture.result.get());
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
    // One entry, demoted rather than removed. The hold has to outlast every
    // section, including the share refusal's dishonest hold.
    configuration.jd.retry_initial_seconds = 3600;
    configuration.jd.retry_max_seconds = 3600;
    configuration.jd.authority_hold_seconds = 3600;
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
        require_no_immediate_retry(fixture);
        REQUIRE(provider.result.wait_for(0s) != std::future_status::ready);
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
    require_no_immediate_retry(fixture);
    REQUIRE(provider.result.wait_for(0s) != std::future_status::ready);
}

TEST_CASE("sv2 JD advances to the next pool after token, declaration, custom job or share failure", "[sv2 jd]") {
    enum class trigger { token, declaration, custom, share, share_timeout };
    auto which = trigger::token;
    SECTION("token timeout") {}
    SECTION("declaration refused") { which = trigger::declaration; }
    SECTION("custom job refused") { which = trigger::custom; }
    SECTION("valid share refused") { which = trigger::share; }
    SECTION("share response timeout") { which = trigger::share_timeout; }
    bool const full = which != trigger::custom;
    auto chain = started_chain("sv2_jd_next_pool");
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds,
        full ? "full-template" : "coinbase-only");
    configuration.jd.response_timeout_seconds = 1;
    std::string error;
    auto const solo = node::mining::payout_from_address(configuration.mining.address,
        domain::config::network::regtest, error);
    REQUIRE(solo);
    provider_run provider(fixture, configuration, chain);
    handshake_upstreams(fixture, full);
    miner_link miner(fixture, provider.port);
    REQUIRE(pays(coinbase_from(miner.work), solo->script, 1));
    auto const original = miner.work;
    std::optional<declared_template> declared;
    if (which == trigger::token) {
        auto const token = fixture.jds.receive<allocate_mining_job_token>();
        REQUIRE(token.user_identifier == "account.worker");
    } else {
        declared = declare_local_template(fixture, miner, full, which != trigger::declaration);
        if (which == trigger::declaration) {
            fixture.jds.send(declare_mining_job_error{declared->declaration.request_id,
                "pool-policy", {}});
        } else if (which == trigger::custom) {
            fixture.pool.send(set_custom_mining_job_error{declared->custom.channel_id,
                declared->custom.request_id, "pool-policy"});
        } else {
            fixture.pool.send(set_custom_mining_job_success{declared->custom.channel_id,
                declared->custom.request_id, 77});
            data_chunk const extranonce(miner.opened.extranonce_size, 0x5a);
            auto const nonce = find_nonce(declared->pooled, extranonce, false);
            fixture.write(miner.socket, frame(submit_shares_extended{miner.opened.channel_id, 1,
                declared->pooled.job.job_id, nonce, declared->pooled.prev.ntime_start,
                declared->pooled.job.version, extranonce}));
            auto const forwarded = fixture.pool.receive<submit_shares_extended>();
            REQUIRE(forwarded.job_id == 77);
            REQUIRE(receive_plain<submit_shares_success>(fixture, miner.socket).last_sequence_number == 1);
            if (which == trigger::share) {
                fixture.pool.send(submit_shares_error{forwarded.channel_id,
                    forwarded.sequence_number, "stale-share"});
            }
        }
        require_solo_again(miner, declared->pooled, solo->script);
    }
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    // No template or downstream connection was replaced while the first pool
    // failed. The second entry declares the same local transaction selection.
    handshake_upstreams(backup_pool, backup_jds, full);
    auto const replacement = declare_local_template(fixture, backup_pool, backup_jds, miner, full);
    REQUIRE(replacement.pooled.prev.prev_hash == original.prev.prev_hash);
    REQUIRE(replacement.pooled.job.merkle_path == original.job.merkle_path);
    backup_pool.send(set_custom_mining_job_success{replacement.custom.channel_id,
        replacement.custom.request_id, 88});
    backup_pool.finish_writing();
    require_solo_again(miner, replacement.pooled, solo->script);
    fixture.eof(backup_pool.socket);
    fixture.eof(backup_jds.socket);
    solo_job(fixture, provider.port, solo->script);
    provider.provider->stop();
    fixture.await(provider.result);
    REQUIRE_NOTHROW(provider.result.get());
}

TEST_CASE("sv2 JD retains one silent recovery probe while the healthy entry stays active", "[sv2 jd]") {
    client_fixture fixture;
    fake_upstream second_pool(fixture, 6);
    fake_upstream second_jds(fixture, 7);
    fake_upstream third_pool(fixture, 8);
    fake_upstream third_jds(fixture, 9);
    auto configuration = settings(fixture.pool, fixture.jds, second_pool, second_jds);
    configuration.jd.upstreams.push_back(upstream(third_pool, third_jds));
    configuration.jd.retry_initial_seconds = 1;
    configuration.jd.probe_interval_seconds = 1;
    fixture.start(configuration);
    handshake_upstreams(fixture, true);
    fixture.await(fixture.ready);
    REQUIRE_NOTHROW(fixture.ready.get());
    fixture.pool.finish_writing();
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    handshake_upstreams(second_pool, second_jds, true);
    second_pool.finish_writing();
    fixture.eof(second_pool.socket);
    fixture.eof(second_jds.socket);
    handshake_upstreams(third_pool, third_jds, true);
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == std::optional<size_t>{2}
            && status->entries[2].health == jd_health::healthy;
    }));
    fixture.pool.accept_again();
    fixture.jds.accept_again();
    second_pool.accept_again();
    second_jds.accept_again();
    REQUIRE(fixture.wait_until([&] {
        return fixture.pool.accepted->done && fixture.jds.accepted->done;
    }, 2s));
    REQUIRE_FALSE(fixture.pool.accepted->error);
    REQUIRE_FALSE(fixture.jds.accepted->error);
    auto const probing = fixture.client->status();
    REQUIRE(probing->active == std::optional<size_t>{2});
    REQUIRE(probing->probe == std::optional<size_t>{0});
    // Leave the first probe in Noise. Even once the other entry is eligible,
    // it must not reserve another pair or displace the working third entry.
    CHECK_FALSE(fixture.wait_until([&] {
        return second_pool.accepted->done || second_jds.accepted->done;
    }, 1100ms));
    auto const held = fixture.client->status();
    REQUIRE(held->active == std::optional<size_t>{2});
    REQUIRE(held->probe == std::optional<size_t>{0});
    REQUIRE(held->entries[2].health == jd_health::healthy);
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
    fixture.eof(third_pool.socket);
    fixture.eof(third_jds.socket);
}

TEST_CASE("sv2 JD recovery waits for accepted work, the hold and a local job boundary", "[sv2 jd]") {
    bool from_solo = false;
    SECTION("recover from another pool") {}
    SECTION("recover from solo after every pool failed") { from_solo = true; }
    auto chain = started_chain("sv2_jd_recovery");
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds, "coinbase-only");
    if (from_solo) configuration.jd.upstreams.pop_back();
    configuration.jd.retry_initial_seconds = 1;
    configuration.jd.probe_interval_seconds = 1;
    configuration.jd.recovery_hold_seconds = 1;
    fixture.start(configuration, &chain->chain());
    handshake_upstreams(fixture, false);
    (void)accept_coinbase_template(fixture.pool, fixture.jds);
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->entries[0].health == jd_health::healthy;
    }));
    fixture.pool.finish_writing();
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    if ( ! from_solo) {
        handshake_upstreams(backup_pool, backup_jds, false);
        (void)accept_coinbase_template(backup_pool, backup_jds, 88);
    }
    auto const retained = from_solo ? std::optional<size_t>{} : std::optional<size_t>{1};
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == retained
            && (from_solo || status->entries[1].health == jd_health::healthy);
    }));
    auto const active_notice = fixture.client->notice();
    REQUIRE(active_notice);
    REQUIRE(active_notice->pool == ! from_solo);
    fixture.pool.accept_again();
    fixture.jds.accept_again();
    handshake_upstreams(fixture, false);
    auto const token = fixture.jds.receive<allocate_mining_job_token>();
    auto const connected_probe = fixture.client->status();
    REQUIRE(connected_probe->probe == std::optional<size_t>{0});
    REQUIRE(connected_probe->entries[0].health == jd_health::connecting);
    REQUIRE(connected_probe->active == retained);
    fixture.jds.send(allocate_mining_job_token_success{token.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    auto const custom = fixture.pool.receive<set_custom_mining_job>();
    fixture.pool.send(set_custom_mining_job_success{custom.channel_id, custom.request_id, 99});
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->entries[0].healthy_since.has_value();
    }));
    auto const recovered = fixture.client->status();
    REQUIRE(recovered->active == retained);
    REQUIRE(fixture.client->notice()->generation == active_notice->generation);
    auto const hold_end = *recovered->entries[0].healthy_since + 1s;
    REQUIRE(fixture.wait_until([&] { return std::chrono::steady_clock::now() >= hold_end; }, 2s));
    CHECK_FALSE(fixture.wait_until([&] {
        return fixture.client->status()->active == std::optional<size_t>{0};
    }, 100ms));
    REQUIRE(fixture.client->notice()->generation == active_notice->generation);
    auto const view = chain->chain().chain_view();
    REQUIRE(view);
    auto const block = test::mine_block(view->tip_hash, 2, uint32_t(zulu_time()), 0, {}, 0);
    std::vector<domain::chain::block> const blocks{block};
    REQUIRE(chain->organizer().add_headers(test::headers_of(blocks)).headers_added == 1);
    test::persist_headers(*chain, blocks, 2);
    test::connect_bodies(*chain, blocks, 2);
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->active == std::optional<size_t>{0};
    }));
    REQUIRE_FALSE(fixture.client->status()->probe);
    if ( ! from_solo) {
        fixture.eof_after_pending(backup_pool.socket);
        fixture.eof_after_pending(backup_jds.socket);
    }
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
}

TEST_CASE("sv2 JD flags valid share refusals and demotes invalid acknowledgements", "[sv2 jd]") {
    enum class response { channel, sequence, count, malformed_success, trailing_success,
        refusal_channel, refusal_sequence, malformed_refusal, trailing_refusal, dishonest };
    auto which = response::channel;
    SECTION("success names another channel") {}
    SECTION("success names an unknown sequence") { which = response::sequence; }
    SECTION("success count disagrees with sequence") { which = response::count; }
    SECTION("success is truncated") { which = response::malformed_success; }
    SECTION("success has trailing data") { which = response::trailing_success; }
    SECTION("refusal names another channel") { which = response::refusal_channel; }
    SECTION("refusal names an unknown sequence") { which = response::refusal_sequence; }
    SECTION("refusal is truncated") { which = response::malformed_refusal; }
    SECTION("refusal has trailing data") { which = response::trailing_refusal; }
    SECTION("valid share refused on the current accepted job") { which = response::dishonest; }
    auto chain = started_chain("sv2_jd_bad_share_reply");
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto const configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds, "coinbase-only");
    fixture.start(configuration, &chain->chain());
    handshake_upstreams(fixture, false);
    (void)accept_coinbase_template(fixture.pool, fixture.jds);
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->entries[0].health == jd_health::healthy;
    }));
    auto const notice = fixture.client->notice();
    REQUIRE(notice);
    fixture.client->observe_share(proof_for(*notice));
    auto const share = fixture.pool.receive<submit_shares_extended>();
    // An error for a sequence this attempt did not send is not a refusal.
    // send() runs the strand, so the capture has to be installed first.
    std::optional<captured_log> unknown_refusal;
    if (which == response::refusal_sequence) unknown_refusal.emplace();
    if (which == response::malformed_success || which == response::malformed_refusal) {
        auto const type = which == response::malformed_success
            ? submit_shares_success::message_type : submit_shares_error::message_type;
        fixture.pool.send_frame(to_data_chunk(message_header{message_header::channel_msg_flag, type, 0}));
    } else if (which == response::trailing_success || which == response::trailing_refusal) {
        auto payload = which == response::trailing_success
            ? to_data_chunk(submit_shares_success{share.channel_id, share.sequence_number, 1, 1})
            : to_data_chunk(submit_shares_error{share.channel_id, share.sequence_number, "stale-share"});
        payload.push_back(0);
        auto const type = which == response::trailing_success
            ? submit_shares_success::message_type : submit_shares_error::message_type;
        auto bytes = to_data_chunk(message_header{message_header::channel_msg_flag,
            type, static_cast<uint32_t>(payload.size())});
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        fixture.pool.send_frame(bytes);
    } else if (which == response::refusal_channel || which == response::refusal_sequence
        || which == response::dishonest) {
        fixture.pool.send(submit_shares_error{share.channel_id + (which == response::refusal_channel ? 1u : 0u),
            share.sequence_number + (which == response::refusal_sequence ? 1u : 0u), "stale-share"});
    } else {
        fixture.pool.send(submit_shares_success{share.channel_id + (which == response::channel ? 1u : 0u),
            share.sequence_number + (which == response::sequence ? 1u : 0u),
            which == response::count ? 2u : 1u, 1});
    }
    if (unknown_refusal) {
        auto const sequence = share.sequence_number + 1;
        REQUIRE(fixture.wait_until([&] {
            return unknown_refusal->count("ignoring share error for unknown sequence "
                + std::to_string(sequence)) >= 1;
        }));
        CHECK_FALSE(fixture.wait_until([&] {
            return fixture.client->status()->entries[0].health != jd_health::healthy;
        }, 100ms));
        REQUIRE(fixture.client->status()->entries[0].health == jd_health::healthy);
        REQUIRE(fixture.client->status()->active == std::optional<size_t>{0});
        REQUIRE(unknown_refusal->count("pool refused a valid share") == 0);
        REQUIRE(unknown_refusal->count("invalid upstream share") == 0);
        REQUIRE(fixture.result.wait_for(0s) != std::future_status::ready);
        fixture.client->stop();
        fixture.await(fixture.result);
        REQUIRE_NOTHROW(fixture.result.get());
        return;
    }
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    handshake_upstreams(backup_pool, backup_jds, false);
    (void)accept_coinbase_template(backup_pool, backup_jds, 88);
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == std::optional<size_t>{1}
            && status->entries[1].health == jd_health::healthy;
    }));
    auto const failed = fixture.client->status()->entries[0];
    REQUIRE(failed.health == (which == response::dishonest ? jd_health::dishonest : jd_health::backoff));
    REQUIRE(failed.last_error.find(which == response::dishonest
        ? "pool refused a valid share" : "invalid upstream share") != std::string::npos);
    if (which == response::dishonest) {
        REQUIRE(failed.retry_at > std::chrono::steady_clock::now());
    }
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
}

TEST_CASE("sv2 JD accepts a batched share success that counts fewer than the submits it covers", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_batched_share_success");
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto const configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds, "coinbase-only");
    fixture.start(configuration, &chain->chain());
    handshake_upstreams(fixture, false);
    (void)accept_coinbase_template(fixture.pool, fixture.jds);
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->entries[0].health == jd_health::healthy;
    }));
    auto const notice = fixture.client->notice();
    REQUIRE(notice);
    auto proof = proof_for(*notice);
    fixture.client->observe_share(proof);
    auto const first = fixture.pool.receive<submit_shares_extended>();
    ++proof.nonce;
    fixture.client->observe_share(proof);
    auto const second = fixture.pool.receive<submit_shares_extended>();
    ++proof.nonce;
    fixture.client->observe_share(proof);
    auto const third = fixture.pool.receive<submit_shares_extended>();
    auto const channel = first.channel_id;
    auto const s0 = first.sequence_number;
    auto const s1 = second.sequence_number;
    auto const s2 = third.sequence_number;
    REQUIRE(second.channel_id == channel);
    REQUIRE(third.channel_id == channel);
    REQUIRE(s1 == s0 + 1);
    REQUIRE(s2 == s1 + 1);
    // sv2-spec 5.3.13: last_sequence_number is the newest correct submit.
    // The accepted count may be smaller than that prefix. The sum is not checked.
    captured_log log;
    fixture.pool.send(submit_shares_success{channel, s2, 2, 2});
    ++proof.nonce;
    fixture.client->observe_share(proof);
    auto const forwarded = fixture.pool.receive<submit_shares_extended>();
    REQUIRE(forwarded.channel_id == channel);
    REQUIRE(forwarded.sequence_number == s2 + 1);
    REQUIRE(forwarded.nonce == proof.nonce);
    CHECK_FALSE(fixture.wait_until([&] {
        return fixture.client->status()->entries[0].health != jd_health::healthy;
    }, 100ms));
    auto const staying = fixture.client->status();
    REQUIRE(staying->entries[0].health == jd_health::healthy);
    REQUIRE(staying->active == std::optional<size_t>{0});
    REQUIRE(log.count("invalid upstream share") == 0);
    // A count of 2 accepts the newest submits in the prefix and leaves the
    // older one, s0, in the refusal record. The tip and this declaration are
    // still the ones that share was mined on, so its error is a refusal of
    // current work (sv2-spec 5.3.14), not the stale race.
    fixture.pool.send(submit_shares_error{channel, s0, "difficulty-too-low"});
    REQUIRE(fixture.wait_until([&] {
        return log.count("pool refused a valid share on an accepted job (difficulty-too-low)") >= 1;
    }));
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    handshake_upstreams(backup_pool, backup_jds, false);
    (void)accept_coinbase_template(backup_pool, backup_jds, 88);
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == std::optional<size_t>{1}
            && status->entries[1].health == jd_health::healthy;
    }));
    auto const failed = fixture.client->status()->entries[0];
    REQUIRE(failed.health == jd_health::dishonest);
    REQUIRE(failed.last_error.find("pool refused a valid share") != std::string::npos);
    REQUIRE(failed.retry_at > std::chrono::steady_clock::now());
    REQUIRE(fixture.result.wait_for(0s) != std::future_status::ready);
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
}

TEST_CASE("sv2 JD still refuses a share success with a zero count or a count past its sequence", "[sv2 jd]") {
    uint32_t count = 0;
    SECTION("zero count") {}
    SECTION("count past its sequence") { count = 2; }
    auto chain = started_chain("sv2_jd_share_success_count");
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto const configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds, "coinbase-only");
    fixture.start(configuration, &chain->chain());
    handshake_upstreams(fixture, false);
    (void)accept_coinbase_template(fixture.pool, fixture.jds);
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->entries[0].health == jd_health::healthy;
    }));
    auto const notice = fixture.client->notice();
    REQUIRE(notice);
    fixture.client->observe_share(proof_for(*notice));
    auto const share = fixture.pool.receive<submit_shares_extended>();
    // Built in share_response as "invalid upstream share " + kind + ": " + detail.
    char const* const acknowledgement =
        "invalid upstream share acknowledgement: share success does not match the outstanding submits";
    captured_log log;
    fixture.pool.send(submit_shares_success{share.channel_id, share.sequence_number, count, 1});
    REQUIRE(fixture.wait_until([&] { return log.count(acknowledgement) >= 1; }));
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    handshake_upstreams(backup_pool, backup_jds, false);
    (void)accept_coinbase_template(backup_pool, backup_jds, 88);
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == std::optional<size_t>{1}
            && status->entries[1].health == jd_health::healthy;
    }));
    auto const failed = fixture.client->status()->entries[0];
    REQUIRE(failed.health == jd_health::backoff);
    REQUIRE(failed.last_error == acknowledgement);
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
}

TEST_CASE("sv2 JD bounds unacknowledged shares and a cached flush cannot block failover", "[sv2 jd]") {
    enum class bound { history, cached, solutions };
    auto which = bound::history;
    SECTION("unacknowledged share history is bounded") {}
    SECTION("cached shares fill the output queue on acceptance") { which = bound::cached; }
    SECTION("solution burst fills the JDS output queue") { which = bound::solutions; }
    bool const full = which == bound::solutions;
    auto chain = started_chain("sv2_jd_share_bounds");
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds,
        full ? "full-template" : "coinbase-only");
    configuration.jd.upstreams.back() = upstream(backup_pool, backup_jds, "account.worker", "coinbase-only");
    configuration.jd.response_timeout_seconds = 600;
    configuration.jd.max_latency_milliseconds = 600000;
    fixture.start(configuration, &chain->chain());
    handshake_upstreams(fixture, full);
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    if (full) {
        auto const declared = fixture.jds.receive<declare_mining_job>();
        fixture.jds.send(declare_mining_job_success{declared.request_id, {9, 8, 7}});
    }
    auto const custom = fixture.pool.receive<set_custom_mining_job>();
    auto const notice = fixture.client->notice();
    REQUIRE(notice);
    auto proof = proof_for(*notice);
    if (which == bound::cached) {
        // A single accepted job flushes the cache without waiting for writes.
        // More than the queue fits must demote the pool instead of blocking.
        for (uint32_t nonce = 0; nonce < 33; ++nonce) {
            proof.nonce = nonce;
            fixture.client->observe_share(proof);
        }
        CHECK_FALSE(fixture.wait_until([&] {
            return fixture.client->status()->entries[0].health == jd_health::backoff;
        }, 50ms));
        fixture.pool.send(set_custom_mining_job_success{custom.channel_id, custom.request_id, 77});
    } else {
        fixture.pool.send(set_custom_mining_job_success{custom.channel_id, custom.request_id, 77});
        REQUIRE(fixture.wait_until([&] {
            return fixture.client->status()->entries[0].health == jd_health::healthy;
        }));
        if (which == bound::solutions) {
            proof.block = true;
            proof.full_extranonce.resize(32, 0x44);
            for (uint32_t nonce = 0; nonce < 33; ++nonce) {
                proof.nonce = nonce;
                fixture.client->observe_share(proof);
            }
        } else {
            auto const end = std::chrono::steady_clock::now() + 10s;
            for (uint32_t nonce = 0; nonce < 1024; ++nonce) {
                REQUIRE(std::chrono::steady_clock::now() < end);
                proof.nonce = nonce;
                fixture.client->observe_share(proof);
                auto const share = fixture.pool.receive<submit_shares_extended>();
                REQUIRE(share.sequence_number == nonce);
            }
            REQUIRE(fixture.client->status()->entries[0].health == jd_health::healthy);
            ++proof.nonce;
            fixture.client->observe_share(proof);
        }
    }
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->entries[0].health == jd_health::backoff;
    }));
    auto const reason = fixture.client->status()->entries[0].last_error;
    auto const expected = which == bound::cached ? "pool output queue refused a share"
        : which == bound::solutions ? "JDS output queue refused PushSolution"
        : "upstream share acknowledgement history exhausted";
    REQUIRE(reason == expected);
    handshake_upstreams(backup_pool, backup_jds, false);
    (void)accept_coinbase_template(backup_pool, backup_jds, 88);
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == std::optional<size_t>{1}
            && status->entries[1].health == jd_health::healthy;
    }));
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
}

TEST_CASE("sv2 JD changing the coinbase prefix after allocation demotes the entry", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_declaration_bounds");
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto const configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds, "coinbase-only");
    fixture.start(configuration, &chain->chain());
    handshake_upstreams(fixture, false);
    auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
    fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
        {1, 2, 3}, output_bytes(pool_output_list())});
    auto const custom = fixture.pool.receive<set_custom_mining_job>();
    fixture.pool.send(set_extranonce_prefix{custom.channel_id, data_chunk(8, 0x34)});
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->entries[0].health == jd_health::backoff;
    }));
    auto const reason = fixture.client->status()->entries[0].last_error;
    REQUIRE(reason == "pool changed the extranonce prefix of declared work");
    handshake_upstreams(backup_pool, backup_jds, false);
    (void)accept_coinbase_template(backup_pool, backup_jds, 88);
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == std::optional<size_t>{1}
            && status->entries[1].health == jd_health::healthy;
    }));
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
}

TEST_CASE("sv2 JD ignores proofs for another publication or an incompatible share", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_share_owner");
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto const configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds, "coinbase-only");
    fixture.start(configuration, &chain->chain());
    handshake_upstreams(fixture, false);
    (void)accept_coinbase_template(fixture.pool, fixture.jds);
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->entries[0].health == jd_health::healthy;
    }));
    auto const original = fixture.client->notice();
    REQUIRE(original);
    auto const old_proof = proof_for(*original);
    fixture.pool.finish_writing();
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    hash_digest target{};
    target[0] = 1;
    handshake_upstreams(backup_pool, backup_jds, false, &target);
    (void)accept_coinbase_template(backup_pool, backup_jds);
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->entries[1].health == jd_health::healthy;
    }));
    auto const replacement = fixture.client->notice();
    REQUIRE(replacement);
    REQUIRE(replacement->pooled.work_id != original->pooled.work_id);
    REQUIRE(replacement->pooled.upstream_channel_id == original->pooled.upstream_channel_id);
    REQUIRE(replacement->pooled.upstream_extranonce_prefix == original->pooled.upstream_extranonce_prefix);
    auto proof = proof_for(*replacement);
    fixture.client->observe_share(proof);
    auto const first = backup_pool.receive<submit_shares_extended>();
    ++proof.nonce;
    fixture.client->observe_share(proof);
    auto const second = backup_pool.receive<submit_shares_extended>();
    REQUIRE(second.sequence_number == first.sequence_number + 1);
    backup_pool.send(submit_shares_success{second.channel_id, second.sequence_number, 2, 2});
    auto const quiet = fixture.begin_read(backup_pool.socket, 1);
    fixture.client->observe_share(old_proof);
    auto wrong_width = proof;
    wrong_width.upstream_extranonce.pop_back();
    fixture.client->observe_share(std::move(wrong_width));
    proof.header_hash.fill(0xff);
    fixture.client->observe_share(std::move(proof));
    CHECK_FALSE(fixture.wait_until([&] { return quiet->done; }, 100ms));
    REQUIRE(fixture.client->status()->entries[1].health == jd_health::healthy);
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
}

TEST_CASE("sv2 JD slow declarations and share health thresholds advance to the next entry", "[sv2 jd]") {
    enum class trigger { declaration_latency, share_latency, stale, rejected };
    auto which = trigger::declaration_latency;
    SECTION("token allocation exceeds latency limit") {}
    SECTION("accepted share exceeds latency limit") { which = trigger::share_latency; }
    SECTION("old tip stale share rate exceeds limit") { which = trigger::stale; }
    SECTION("old tip rejected share rate exceeds limit") { which = trigger::rejected; }
    auto chain = started_chain("sv2_jd_health_demotion");
    client_fixture fixture;
    fake_upstream backup_pool(fixture, 6);
    fake_upstream backup_jds(fixture, 7);
    auto configuration = settings(fixture.pool, fixture.jds, backup_pool, backup_jds, "coinbase-only");
    configuration.jd.max_latency_milliseconds = 1000;
    configuration.jd.health_window = 2;
    configuration.jd.max_stale_percent = which == trigger::stale ? 1 : 100;
    configuration.jd.max_reject_percent = which == trigger::rejected ? 1 : 100;
    if (which == trigger::stale || which == trigger::rejected) {
        configuration.jd.response_timeout_seconds = 600;
        configuration.jd.max_latency_milliseconds = 600000;
    }
    fixture.start(configuration, &chain->chain());
    handshake_upstreams(fixture, false);
    if (which == trigger::declaration_latency) {
        auto const allocation = fixture.jds.receive<allocate_mining_job_token>();
        auto const delayed_until = std::chrono::steady_clock::now() + 1100ms;
        REQUIRE(fixture.wait_until([&] {
            return std::chrono::steady_clock::now() >= delayed_until;
        }, 2s));
        fixture.jds.send(allocate_mining_job_token_success{allocation.request_id,
            {1, 2, 3}, output_bytes(pool_output_list())});
    } else {
        (void)accept_coinbase_template(fixture.pool, fixture.jds);
        REQUIRE(fixture.wait_until([&] {
            return fixture.client->status()->entries[0].health == jd_health::healthy;
        }));
        auto const notice = fixture.client->notice();
        REQUIRE(notice);
        auto proof = proof_for(*notice);
        fixture.client->observe_share(proof);
        auto const first = fixture.pool.receive<submit_shares_extended>();
        if (which == trigger::share_latency) {
            auto const delayed_until = std::chrono::steady_clock::now() + 1100ms;
            REQUIRE(fixture.wait_until([&] {
                return std::chrono::steady_clock::now() >= delayed_until;
            }, 2s));
            fixture.pool.send(submit_shares_success{first.channel_id, first.sequence_number, 1, 1});
        } else {
            ++proof.nonce;
            fixture.client->observe_share(proof);
            auto const second = fixture.pool.receive<submit_shares_extended>();
            auto const view = chain->chain().chain_view();
            REQUIRE(view);
            auto const block = test::mine_block(view->tip_hash, 2, uint32_t(zulu_time()), 0, {}, 0);
            std::vector<domain::chain::block> const blocks{block};
            REQUIRE(chain->organizer().add_headers(test::headers_of(blocks)).headers_added == 1);
            test::persist_headers(*chain, blocks, 2);
            test::connect_bodies(*chain, blocks, 2);
            auto const reason = which == trigger::stale ? "stale-share" : "pool-rejected";
            fixture.pool.send(submit_shares_error{first.channel_id, first.sequence_number, reason});
            CHECK_FALSE(fixture.wait_until([&] {
                return fixture.client->status()->entries[0].health != jd_health::healthy;
            }, 100ms));
            REQUIRE(fixture.client->status()->entries[0].health == jd_health::healthy);
            fixture.pool.send(submit_shares_error{second.channel_id, second.sequence_number, reason});
        }
    }
    // There can be a new-tip allocation queued before the refusal arrives.
    // Observe the status transition instead of mistaking that frame for EOF.
    REQUIRE(fixture.wait_until([&] {
        return fixture.client->status()->entries[0].health == jd_health::backoff;
    }));
    auto const failed = fixture.client->status()->entries[0];
    auto const expected = which == trigger::stale ? "stale share rate"
        : which == trigger::rejected ? "rejected share rate" : "latency";
    REQUIRE(failed.last_error.find(expected) != std::string::npos);
    handshake_upstreams(backup_pool, backup_jds, false);
    (void)accept_coinbase_template(backup_pool, backup_jds, 88);
    REQUIRE(fixture.wait_until([&] {
        auto const status = fixture.client->status();
        return status && status->active == std::optional<size_t>{1}
            && status->entries[1].health == jd_health::healthy;
    }));
    fixture.client->stop();
    fixture.await(fixture.result);
    REQUIRE_NOTHROW(fixture.result.get());
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
        return log.count("stale share error replaced-job for sequence 0 on a replaced declaration") >= 1;
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

TEST_CASE("sv2 JD demotes an entry when a replaced declaration's share error fills the stale window", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_stale_window");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    // The accepted declaration has to survive connect_bodies, which publishes
    // the tip that replaces it. This test does not wait that deadline out.
    configuration.jd.response_timeout_seconds = 30;
    // The default window is 20, so one stale sample returns before the rate
    // check. One sample is the whole window, and one percent is under it.
    configuration.jd.health_window = 1;
    configuration.jd.max_stale_percent = 1;
    // The hold has to outlast the close observed below. The default retry
    // could open this entry again while that read is still running.
    configuration.jd.retry_initial_seconds = 3600;
    configuration.jd.retry_max_seconds = 3600;
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

    // The success above filled the window with one accepted sample. This error
    // replaces that sample. The share belongs to the declaration just replaced,
    // so the sample is stale, and one stale sample is over the one percent cap.
    captured_log log;
    fixture.pool.send(submit_shares_error{forwarded.channel_id,
        forwarded.sequence_number, "replaced-job"});
    REQUIRE(fixture.wait_until([&] {
        return log.count("Entry 1 backoff: upstream stale share rate exceeded threshold") >= 1;
    }));
    REQUIRE(log.count("stale share error replaced-job for sequence 0 on a replaced declaration") >= 1);
    REQUIRE(log.count("pool refused a valid share") == 0);
    REQUIRE(log.count("upstream rejected share rate exceeded threshold") == 0);
    fixture.eof(fixture.pool.socket);
    fixture.eof(fixture.jds.socket);
    require_no_immediate_retry(fixture);
    REQUIRE(provider.result.wait_for(0s) != std::future_status::ready);
}

TEST_CASE("sv2 JD falls back when the pool rejects a share on the accepted declaration", "[sv2 jd]") {
    auto chain = started_chain("sv2_jd_share_refused");
    client_fixture fixture;
    auto configuration = settings(fixture.pool, fixture.jds);
    // The refusal flags the pool and holds it. The client stays up on solo.
    configuration.jd.retry_initial_seconds = 3600;
    configuration.jd.retry_max_seconds = 3600;
    configuration.jd.authority_hold_seconds = 3600;
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
    REQUIRE(provider.result.wait_for(0s) != std::future_status::ready);
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
    // A malformed reply fails this attempt. The retry must not fall inside the test.
    configuration.jd.retry_initial_seconds = 3600;
    configuration.jd.retry_max_seconds = 3600;
    configuration.jd.authority_hold_seconds = 3600;
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
    REQUIRE(provider.result.wait_for(0s) != std::future_status::ready);
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
        return log.count("share error invalid-job-id for the block found by sequence 0 counts as stale") >= 1;
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
