// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "sync_harness.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <string>
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

#include <kth/domain/message/transaction.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/jd_messages.hpp>
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
data_chunk frame(Message const& message, uint16_t extension_type = framed_extension_type<Message>()) {
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
    return process(session, framed_extension_type<Message>(), Message::message_type, to_data_chunk(message));
}

template <typename Message>
Message response(data_chunk const& bytes) {
    byte_reader source(bytes);
    auto const header = message_header::from_data(source);
    REQUIRE(header.has_value());
    REQUIRE(header->extension_type == framed_extension_type<Message>());
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
    // The endpoint block_chain::submit_block feeds, for a case whose solutions
    // a submission_pipeline drains. Without one, a submission is refused at
    // once instead of waiting for a coordinator that never runs.
    std::shared_ptr<blockchain::block_submission_channel> submissions;
    std::shared_ptr<template_provider> provider;
    std::shared_ptr<completion> completed = std::make_shared<completion>();
    std::vector<std::shared_ptr<::asio::ip::tcp::socket>> clients;
    bool running = false;

    explicit listener_fixture(node::sv2_settings const& settings, bool local_admission = false)
        : submissions(local_admission ? std::make_shared<blockchain::block_submission_channel>(4, 1) : nullptr)
    {
        REQUIRE(chain.created());
        REQUIRE(chain.start(submissions));
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
    settings.noise = false;
    settings.port = 0;
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
        provider = std::make_shared<template_provider>(noise_settings, io.get_executor(), chain.chain());
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
        REQUIRE(fixture.submissions);
        REQUIRE(fixture.execute(state->network.start()) == error::success);
        ::asio::co_spawn(fixture.io,
            node::sync::sync_orchestrator(fixture.chain.chain(), fixture.chain.organizer(),
                state->network, domain::config::network::regtest,
                [](std::string const& reason) { FAIL("sync failed: " << reason); },
                node::sync::node_header_walk_timing, fixture.submissions.get()),
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
domain::chain::transaction coinbase_for(template_pair const& job) {
    auto script = job.work.coinbase_prefix;
    script.resize(100, 0);
    domain::chain::input::list inputs;
    inputs.emplace_back(domain::chain::output_point{null_hash, domain::chain::point::null_index},
        domain::chain::script(std::move(script), false), job.work.coinbase_tx_input_sequence);
    domain::chain::output::list outputs;
    outputs.emplace_back(job.work.coinbase_tx_value_remaining,
        test::miner_script(), domain::chain::token_data_opt{});
    return domain::chain::transaction(job.work.coinbase_tx_version, job.work.coinbase_tx_locktime,
        std::move(inputs), std::move(outputs));
}

domain::chain::block solution_block(template_pair const& job,
    std::vector<data_chunk> const& raw_transactions, bool valid_work = true) {
    domain::chain::transaction::list transactions;
    transactions.push_back(coinbase_for(job));
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

// A session that negotiated BCH large templates, then sent its constraints.
template_provider_session large_session() {
    auto session = ready_session(false);
    auto const granted = process(session, request_extensions{1, {large_templates_extension}});
    REQUIRE_FALSE(granted.close);
    REQUIRE(response<request_extensions_success>(granted.response).supported_extensions
        == std::vector<uint16_t>{large_templates_extension});
    require_silent(process(session, coinbase_output_constraints{100, 2}));
    return session;
}

std::vector<uint32_t> numbers(uint32_t first, uint32_t last) {
    std::vector<uint32_t> values;
    for (auto value = first; value <= last; ++value) values.push_back(value);
    return values;
}

bool ctor_less(hash_digest const& left, hash_digest const& right) {
    return std::lexicographical_compare(left.rbegin(), left.rend(), right.rbegin(), right.rend());
}

// Distinct transactions in CTOR order, as the chain selects them. Each number
// is a lock time; `script_size` pads the output.
blockchain::mining_template numbered_template(std::vector<uint32_t> const& locktimes,
    hash_digest const& previous = null_hash, size_t script_size = 0) {
    std::vector<std::pair<hash_digest, transaction_const_ptr>> sorted;
    for (auto const locktime : locktimes) {
        domain::chain::output::list outputs;
        outputs.emplace_back(1000, domain::chain::script(data_chunk(script_size, 0), false),
            domain::chain::token_data_opt{});
        auto tx = std::make_shared<domain::message::transaction const>(
            domain::chain::transaction(1, locktime, {}, std::move(outputs)));
        auto const id = tx->hash();
        sorted.emplace_back(id, std::move(tx));
    }
    std::sort(sorted.begin(), sorted.end(), [](auto const& left, auto const& right) {
        return ctor_less(left.first, right.first);
    });
    auto value = empty_template();
    value.previous_block_hash = previous;
    for (auto& item : sorted) value.selection.txs.push_back(std::move(item.second));
    return value;
}

std::vector<data_chunk> raw_transactions(blockchain::mining_template const& value) {
    std::vector<data_chunk> raw;
    for (auto const& tx : value.selection.txs) {
        domain::chain::transaction const& transaction = *tx;
        raw.push_back(to_data_chunk(transaction, true));
    }
    return raw;
}

message_header header_of(data_chunk const& bytes) {
    REQUIRE(bytes.size() >= message_header::size);
    auto const header = from_data_chunk<message_header>(byte_span(bytes).first(message_header::size));
    REQUIRE(header.has_value());
    REQUIRE(header->msg_length == bytes.size() - message_header::size);
    return *header;
}

// Every data frame the session has, taken one at a time as the listener does
// while the socket drains. Bounded, so data that never ends fails the test.
std::vector<data_chunk> drain(template_provider_session& session) {
    std::vector<data_chunk> frames;
    for (size_t count = 0; session.sending_data(); ++count) {
        REQUIRE(count < 100000);
        auto bytes = session.next_data_frame();
        if ( ! bytes.empty()) frames.push_back(std::move(bytes));
    }
    return frames;
}

// One template's data as a client reads it: Kept first for a delta, then
// pages, then the core Success, every frame naming the template.
struct template_data {
    std::optional<request_transaction_data_kept> kept;
    std::vector<data_chunk> transactions;
    size_t pages = 0;
};

template_data collect(std::vector<data_chunk> const& frames, uint64_t id) {
    REQUIRE_FALSE(frames.empty());
    template_data data;
    for (size_t index = 0; index < frames.size(); ++index) {
        auto const& bytes = frames[index];
        auto const header = header_of(bytes);
        REQUIRE(header.msg_length <= template_provider_session::data_page_size);
        if (index + 1 == frames.size()) {
            auto const success = response<request_transaction_data_success>(bytes);
            REQUIRE(success.template_id == id);
            REQUIRE(success.excess_data.empty());
            data.transactions.insert(data.transactions.end(),
                success.transaction_list.begin(), success.transaction_list.end());
        } else if (index == 0 && header.msg_type == request_transaction_data_kept::message_type) {
            data.kept = response<request_transaction_data_kept>(bytes);
            REQUIRE(data.kept->template_id == id);
        } else {
            auto const page = response<request_transaction_data_transactions>(bytes);
            REQUIRE(page.template_id == id);
            REQUIRE_FALSE(page.transaction_list.empty());
            ++data.pages;
            data.transactions.insert(data.transactions.end(),
                page.transaction_list.begin(), page.transaction_list.end());
        }
    }
    return data;
}

// The client's list: as sent for a full push, or for a delta the base's kept
// transactions and the added ones, sorted by transaction id.
std::vector<data_chunk> rebuild(template_data const& data, std::vector<data_chunk> const& base) {
    if ( ! data.kept) return data.transactions;
    auto const& kept = data.kept->kept;
    REQUIRE(kept.size() == (base.size() + 7) / 8);
    if (base.size() % 8 != 0) REQUIRE((kept.back() >> (base.size() % 8)) == 0);
    std::vector<std::pair<hash_digest, data_chunk>> list;
    for (size_t index = 0; index < base.size(); ++index) {
        if (((kept[index / 8] >> (index % 8)) & 1) != 0) list.emplace_back(bitcoin_hash(base[index]), base[index]);
    }
    for (auto const& tx : data.transactions) list.emplace_back(bitcoin_hash(tx), tx);
    std::sort(list.begin(), list.end(), [](auto const& left, auto const& right) {
        return ctor_less(left.first, right.first);
    });
    std::vector<data_chunk> rebuilt;
    for (auto& item : list) rebuilt.push_back(std::move(item.second));
    return rebuilt;
}

// The check a client makes before it uses a list: the coinbase merkle path of
// a placeholder coinbase followed by the list must be the template's.
std::vector<hash_digest> client_merkle_path(std::vector<data_chunk> const& list) {
    std::vector<hash_digest> hashes{null_hash};
    for (auto const& tx : list) hashes.push_back(bitcoin_hash(tx));
    std::vector<hash_digest> path;
    while (hashes.size() > 1) {
        if (hashes.size() % 2 != 0) hashes.push_back(hashes.back());
        path.push_back(hashes[1]);
        std::vector<hash_digest> parents;
        for (size_t index = 0; index < hashes.size(); index += 2) {
            parents.push_back(bitcoin_hash(build_chunk({hashes[index], hashes[index + 1]})));
        }
        hashes = std::move(parents);
    }
    return path;
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

TEST_CASE("sv2 session answers extension negotiation only before constraints", "[sv2 template_provider]") {
    auto session = ready_session(false);
    // None of these is implemented on Template Distribution and nothing is
    // required, so the answer lists each as unsupported (extension 0x0001,
    // section 4.1), and the connection stays standard.
    auto const refused = process(session, request_extensions{3, {0x0002, requested_payout_outputs_extension}});
    REQUIRE_FALSE(refused.close);
    auto const error = response<request_extensions_error>(refused.response);
    REQUIRE(error.request_id == 3);
    REQUIRE(error.unsupported_extensions == std::vector<uint16_t>{0x0002, requested_payout_outputs_extension});
    REQUIRE(error.required_extensions.empty());
    auto const empty = response<request_extensions_error>(process(session, request_extensions{4, {}}).response);
    REQUIRE(empty.request_id == 4);
    REQUIRE(empty.unsupported_extensions.empty());
    REQUIRE(empty.required_extensions.empty());
    require_silent(process(session, coinbase_output_constraints{100, 2}));
    REQUIRE(response<request_transaction_data_error>(process(session,
        request_transaction_data{7}).response).error_code == "template-id-not-found");

    // Once Template Distribution has begun, negotiation is out of order.
    require_closed(process(session, request_extensions{5, {0x0002}}));

    auto malformed = ready_session(false);
    auto bytes = to_data_chunk(request_extensions{1, {0x0002}});
    bytes.pop_back();
    require_closed(process(malformed, extensions_negotiation_type, request_extensions::message_type, bytes));

    // The extension's server-only messages, and any with the channel bit,
    // are discarded like an unknown extension's and do not count as messages.
    auto discarded = ready_session(false);
    require_silent(process(discarded, request_extensions_success{1, {}}));
    require_silent(process(discarded, request_extensions_error{1, {}, {}}));
    require_silent(process(discarded, static_cast<uint16_t>(extensions_negotiation_type | message_header::channel_msg_flag),
        request_extensions::message_type, to_data_chunk(request_extensions{1, {}})));
    require_silent(process(discarded, coinbase_output_constraints{100, 2}));
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
    auto session = ready_session();
    require_closed(session.set_template(std::move(value)));
}

TEST_CASE("sv2 sends a template too large for its transaction data and refuses only that data", "[sv2 template_provider]") {
    // A BCH block can outgrow RequestTransactionData.Success. A client that
    // mines the merkle path never asks for the transactions, so it keeps the
    // connection and gets the work.
    auto value = empty_template();
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
    auto const count = value.selection.txs.size();
    auto session = ready_session();
    auto const large = template_messages(session.set_template(std::move(value)));
    REQUIRE_FALSE(large.work.merkle_path.empty());
    // Every request gets the same refusal; only the first is logged.
    for (int request = 0; request < 2; ++request) {
        auto const refused = process(session, request_transaction_data{large.work.template_id});
        REQUIRE_FALSE(refused.close);
        auto const error = response<request_transaction_data_error>(refused.response);
        REQUIRE(error.template_id == large.work.template_id);
        REQUIRE(error.error_code == "template-too-large");
    }
    // Its solution is still assembled for submission, as for any template.
    auto const solved = process(session, solution_message(large,
        domain::chain::block({}, {coinbase_for(large)})));
    require_silent(solved);
    REQUIRE(solved.solution);
    REQUIRE(solved.solution->transactions().size() == count + 1);

    // A template that fits, on the same connection, gets its transactions.
    domain::chain::output::list outputs;
    outputs.emplace_back(1000, test::miner_script(), domain::chain::token_data_opt{});
    domain::chain::transaction const tx(1, 0, {}, std::move(outputs));
    auto normal = empty_template();
    normal.selection.txs.push_back(std::make_shared<domain::message::transaction const>(tx));
    auto const fits = template_messages(session.set_template(std::move(normal)));
    auto const served = process(session, request_transaction_data{fits.work.template_id});
    REQUIRE_FALSE(served.close);
    auto const data = response<request_transaction_data_success>(served.response);
    REQUIRE(data.template_id == fits.work.template_id);
    data_chunk bytes(tx.serialized_size(true));
    byte_writer writer(bytes);
    REQUIRE(tx.to_data(writer, true));
    REQUIRE(data.transaction_list == std::vector<data_chunk>{bytes});
    REQUIRE(response<request_transaction_data_error>(process(session,
        request_transaction_data{large.work.template_id}).response).error_code == "template-too-large");
}

// BCH large templates (0x4C54) on Template Distribution.

TEST_CASE("sv2 session grants BCH large templates and only that extension", "[sv2 template_provider]") {
    auto session = ready_session(false);
    auto const granted = process(session, request_extensions{2,
        {0x0002, large_templates_extension, requested_payout_outputs_extension}});
    REQUIRE_FALSE(granted.close);
    auto const success = response<request_extensions_success>(granted.response);
    REQUIRE(success.request_id == 2);
    REQUIRE(success.supported_extensions == std::vector<uint16_t>{large_templates_extension});
    require_silent(process(session, coinbase_output_constraints{100, 2}));
    REQUIRE_FALSE(session.sending_data());
    (void)template_messages(session.set_template(empty_template()));
    REQUIRE(session.sending_data());

    // Once it is on, its messages come only from the server.
    for (uint16_t const extension : {large_templates_extension,
            static_cast<uint16_t>(large_templates_extension | message_header::channel_msg_flag)}) {
        for (uint8_t const type : {request_transaction_data_transactions::message_type,
                request_transaction_data_kept::message_type, uint8_t{0x01}}) {
            CAPTURE(extension, type);
            auto large = large_session();
            require_closed(process(large, extension, type, data_chunk{1, 2, 3}));
        }
    }

    // Without it they are an unknown extension's, discarded as before.
    auto standard = ready_session();
    require_silent(process(standard, large_templates_extension,
        request_transaction_data_kept::message_type, data_chunk{1, 2, 3}));

    // The latest answer decides: one without the extension leaves it off.
    auto changed = ready_session(false);
    (void)response<request_extensions_success>(process(changed,
        request_extensions{1, {large_templates_extension}}).response);
    auto const refused = response<request_extensions_error>(process(changed,
        request_extensions{2, {0x0002}}).response);
    REQUIRE(refused.unsupported_extensions == std::vector<uint16_t>{0x0002});
    require_silent(process(changed, coinbase_output_constraints{100, 2}));
    (void)template_messages(changed.set_template(empty_template()));
    REQUIRE_FALSE(changed.sending_data());
}

TEST_CASE("sv2 session without BCH large templates sends no data it was not asked for", "[sv2 template_provider]") {
    // A refused or absent negotiation leaves standard SV2 exactly as it was:
    // nothing follows SetNewPrevHash, the answer is one Success, and a list
    // that does not fit one is refused.
    for (bool const negotiated : {false, true}) {
        CAPTURE(negotiated);
        auto session = ready_session(false);
        if (negotiated) {
            (void)response<request_extensions_error>(process(session, request_extensions{1, {0x0002}}).response);
        }
        require_silent(process(session, coinbase_output_constraints{100, 2}));
        auto value = numbered_template(numbers(1, 3));
        auto const expected = raw_transactions(value);
        auto const job = template_messages(session.set_template(std::move(value)));
        REQUIRE_FALSE(session.sending_data());
        REQUIRE(session.next_data_frame().empty());
        auto const served = process(session, request_transaction_data{job.work.template_id});
        REQUIRE_FALSE(served.close);
        REQUIRE(response<request_transaction_data_success>(served.response).transaction_list == expected);
        REQUIRE_FALSE(session.sending_data());

        auto large = empty_template();
        auto tx = std::make_shared<domain::message::transaction const>(domain::chain::transaction{});
        large.selection.txs.assign(0x10000, tx);
        auto const refused = template_messages(session.set_template(std::move(large)));
        REQUIRE_FALSE(session.sending_data());
        REQUIRE(response<request_transaction_data_error>(process(session,
            request_transaction_data{refused.work.template_id}).response).error_code == "template-too-large");
    }
}

TEST_CASE("sv2 session pushes a template too large for one Success in pages a client rebuilds", "[sv2 template_provider]") {
    // The same templates the core Success cannot carry.
    auto value = empty_template();
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
    auto const expected = raw_transactions(value);
    auto session = large_session();
    // The template's own frames are NewTemplate and SetNewPrevHash only. Its
    // data is built a frame at a time, as the socket drains.
    auto const job = template_messages(session.set_template(std::move(value)));
    REQUIRE(session.sending_data());
    REQUIRE_FALSE(session.answering());
    auto const data = collect(drain(session), job.work.template_id);
    REQUIRE_FALSE(data.kept);
    REQUIRE(data.pages > 0);
    REQUIRE(data.transactions == expected);
    REQUIRE(client_merkle_path(data.transactions) == job.work.merkle_path);
    REQUIRE(session.next_data_frame().empty());
}

TEST_CASE("sv2 session sizes the Kept bitmap to its base", "[sv2 template_provider]") {
    for (uint32_t const count : {0u, 1u, 7u, 8u, 9u, 17u}) {
        CAPTURE(count);
        auto session = large_session();
        auto base_value = numbered_template(numbers(1, count));
        auto const base = raw_transactions(base_value);
        auto const first = template_messages(session.set_template(std::move(base_value)));
        REQUIRE(collect(drain(session), first.work.template_id).transactions == base);

        // Everything is kept and one transaction is added.
        auto locktimes = numbers(1, count);
        locktimes.push_back(1000);
        auto value = numbered_template(locktimes);
        auto const expected = raw_transactions(value);
        auto const next = template_messages(session.set_template(std::move(value)));
        auto const delta = collect(drain(session), next.work.template_id);
        REQUIRE(delta.kept);
        REQUIRE(delta.kept->base_template_id == first.work.template_id);
        // Exactly ceil(count / 8) bytes, every base bit set, unused high bits clear.
        REQUIRE(delta.kept->kept.size() == (count + 7) / 8);
        for (size_t index = 0; index < delta.kept->kept.size(); ++index) {
            auto const bits = std::min<size_t>(8, count - index * 8);
            REQUIRE(delta.kept->kept[index] == static_cast<uint8_t>((1u << bits) - 1));
        }
        REQUIRE(delta.transactions.size() == 1);
        REQUIRE(rebuild(delta, base) == expected);
        REQUIRE(client_merkle_path(rebuild(delta, base)) == next.work.merkle_path);
    }
}

TEST_CASE("sv2 session pushes only what a refresh on the same tip adds", "[sv2 template_provider]") {
    auto session = large_session();
    // Twenty-one transactions, so the last bitmap byte has unused high bits.
    auto first_value = numbered_template(numbers(1, 21));
    auto const base = raw_transactions(first_value);
    auto const first = template_messages(session.set_template(std::move(first_value)));
    auto const full = collect(drain(session), first.work.template_id);
    REQUIRE_FALSE(full.kept);
    REQUIRE(full.transactions == base);
    REQUIRE(client_merkle_path(full.transactions) == first.work.merkle_path);

    // The refresh loses two transactions and gains three.
    auto locktimes = numbers(1, 21);
    std::erase(locktimes, 5u);
    std::erase(locktimes, 17u);
    locktimes.insert(locktimes.end(), {100, 101, 102});
    auto second_value = numbered_template(locktimes);
    auto const expected = raw_transactions(second_value);
    auto const second = template_messages(session.set_template(std::move(second_value)));
    REQUIRE(second.prev.prev_hash == first.prev.prev_hash);
    auto const delta = collect(drain(session), second.work.template_id);
    REQUIRE(delta.kept);
    REQUIRE(delta.kept->base_template_id == first.work.template_id);
    REQUIRE(delta.kept->kept.size() == 3);
    for (size_t index = 0; index < base.size(); ++index) {
        CAPTURE(index);
        bool const kept = std::find(expected.begin(), expected.end(), base[index]) != expected.end();
        REQUIRE(((delta.kept->kept[index / 8] >> (index % 8)) & 1) == (kept ? 1 : 0));
    }
    REQUIRE((delta.kept->kept.back() >> (base.size() % 8)) == 0);
    REQUIRE(delta.transactions.size() == 3);
    for (auto const& tx : delta.transactions) {
        REQUIRE(std::find(base.begin(), base.end(), tx) == base.end());
    }
    auto const rebuilt = rebuild(delta, base);
    REQUIRE(rebuilt == expected);
    REQUIRE(client_merkle_path(rebuilt) == second.work.merkle_path);
}

TEST_CASE("sv2 session pushes a delta after a new tip, against a base it keeps itself", "[sv2 template_provider]") {
    auto session = large_session();
    auto tip = [](uint8_t mark) {
        hash_digest hash{};
        hash[0] = mark;
        return hash;
    };
    auto first_value = numbered_template(numbers(1, 10), tip(1));
    auto const first_raw = raw_transactions(first_value);
    auto const first = template_messages(session.set_template(std::move(first_value)));
    (void)collect(drain(session), first.work.template_id);

    // The block on the first tip took 1 to 6; 7 to 10 stay and 11 and 12 join.
    auto second_value = numbered_template(numbers(7, 12), tip(2));
    auto const second_raw = raw_transactions(second_value);
    auto const second = template_messages(session.set_template(std::move(second_value)));
    auto const after_tip = collect(drain(session), second.work.template_id);
    REQUIRE(after_tip.kept);
    REQUIRE(after_tip.kept->base_template_id == first.work.template_id);
    REQUIRE(after_tip.transactions.size() == 2);
    REQUIRE(rebuild(after_tip, first_raw) == second_raw);
    REQUIRE(client_merkle_path(rebuild(after_tip, first_raw)) == second.work.merkle_path);

    // Two more tips drop the second template itself. The base is the
    // session's copy of its ids, so the next push is still a delta against it.
    session.set_tip(tip(3));
    session.set_tip(tip(4));
    REQUIRE(response<request_transaction_data_error>(process(session,
        request_transaction_data{second.work.template_id}).response).error_code == "stale-template-id");
    auto third_value = numbered_template(numbers(11, 13), tip(4));
    auto const third_raw = raw_transactions(third_value);
    auto const third = template_messages(session.set_template(std::move(third_value)));
    auto const later = collect(drain(session), third.work.template_id);
    REQUIRE(later.kept);
    REQUIRE(later.kept->base_template_id == second.work.template_id);
    REQUIRE(rebuild(later, second_raw) == third_raw);
    REQUIRE(client_merkle_path(rebuild(later, second_raw)) == third.work.merkle_path);
}

TEST_CASE("sv2 session abandons an unfinished push and sends the next one full", "[sv2 template_provider]") {
    auto session = large_session();
    // A finished push, so the one abandoned below has a base.
    auto const base = template_messages(session.set_template(numbered_template(numbers(1, 10))));
    (void)collect(drain(session), base.work.template_id);
    // About 2.5 MiB of transactions: Kept, two pages, then the Success.
    auto const first = template_messages(session.set_template(numbered_template(numbers(1, 2500), null_hash, 1024)));
    auto const take = [&](uint8_t type) {
        auto const bytes = session.next_data_frame();
        REQUIRE(header_of(bytes).extension_type == large_templates_extension);
        REQUIRE(header_of(bytes).msg_type == type);
        REQUIRE(session.sending_data());
    };
    SECTION("the next template arrives after a page") {
        take(request_transaction_data_kept::message_type);
        take(request_transaction_data_transactions::message_type);
    }
    SECTION("the next template arrives before the first frame") {
        REQUIRE(session.sending_data());
    }
    SECTION("the template is dropped two tips later") {
        take(request_transaction_data_kept::message_type);
        take(request_transaction_data_transactions::message_type);
        hash_digest tip{};
        tip[0] = 1;
        // The tip just replaced still continues its push...
        session.set_tip(tip);
        take(request_transaction_data_transactions::message_type);
        // ...but not once its template is gone.
        tip[0] = 2;
        session.set_tip(tip);
        REQUIRE(session.next_data_frame().empty());
        REQUIRE_FALSE(session.sending_data());
    }
    // Nothing more is sent for the first template, and the next push is full
    // although a base was pushed before it.
    auto value = numbered_template(numbers(1, 2501), null_hash, 1024);
    auto const expected = raw_transactions(value);
    auto const next = template_messages(session.set_template(std::move(value)));
    REQUIRE(next.work.template_id > first.work.template_id);
    auto const data = collect(drain(session), next.work.template_id);
    REQUIRE_FALSE(data.kept);
    REQUIRE(data.pages > 0);
    REQUIRE(data.transactions == expected);
    REQUIRE(client_merkle_path(data.transactions) == next.work.merkle_path);

    // That push finished, so the one after it is a delta again.
    auto const after = template_messages(session.set_template(numbered_template(numbers(1, 2502), null_hash, 1024)));
    auto const delta = collect(drain(session), after.work.template_id);
    REQUIRE(delta.kept);
    REQUIRE(delta.kept->base_template_id == next.work.template_id);
    REQUIRE(delta.transactions.size() == 1);
}

TEST_CASE("sv2 session answers RequestTransactionData in pages after any push and keeps the base", "[sv2 template_provider]") {
    auto session = large_session();
    auto first_value = numbered_template(numbers(1, 2000), null_hash, 1024);
    auto const first_raw = raw_transactions(first_value);
    auto const first = template_messages(session.set_template(std::move(first_value)));
    (void)drain(session);
    auto second_value = numbered_template(numbers(1, 2001), null_hash, 1024);
    auto const second_raw = raw_transactions(second_value);
    auto const second = template_messages(session.set_template(std::move(second_value)));

    // Asked for while the second push is under way: that push ends first and
    // the answer follows, so the two never interleave.
    auto frames = std::vector<data_chunk>{session.next_data_frame()};
    require_silent(process(session, request_transaction_data{first.work.template_id}));
    REQUIRE(session.answering());
    auto const rest = drain(session);
    REQUIRE_FALSE(session.answering());
    auto const end = std::find_if(rest.begin(), rest.end(), [](data_chunk const& bytes) {
        return header_of(bytes).extension_type == 0;
    });
    REQUIRE(end != rest.end());
    frames.insert(frames.end(), rest.begin(), end + 1);
    auto const pushed = collect(frames, second.work.template_id);
    REQUIRE(pushed.kept);
    REQUIRE(rebuild(pushed, first_raw) == second_raw);
    auto const answer = collect(std::vector<data_chunk>(end + 1, rest.end()), first.work.template_id);
    REQUIRE_FALSE(answer.kept);
    REQUIRE(answer.pages > 0);
    REQUIRE(answer.transactions == first_raw);

    // The answer leaves the base alone: the next push is a delta against the
    // second template, the last one pushed.
    auto third_value = numbered_template(numbers(1, 2002), null_hash, 1024);
    auto const third_raw = raw_transactions(third_value);
    auto const third = template_messages(session.set_template(std::move(third_value)));
    auto const delta = collect(drain(session), third.work.template_id);
    REQUIRE(delta.kept);
    REQUIRE(delta.kept->base_template_id == second.work.template_id);
    REQUIRE(rebuild(delta, second_raw) == third_raw);

    // With nothing under way the answer starts at once, even for a template
    // whose list exceeds one Success.
    auto const started = process(session, request_transaction_data{third.work.template_id});
    REQUIRE_FALSE(started.close);
    REQUIRE(header_of(started.response).msg_type == request_transaction_data_transactions::message_type);
    REQUIRE(session.answering());
    auto replay = std::vector<data_chunk>{started.response};
    auto const remaining = drain(session);
    replay.insert(replay.end(), remaining.begin(), remaining.end());
    REQUIRE(collect(replay, third.work.template_id).transactions == third_raw);

    // Unknown ids get the usual error, also behind a push.
    REQUIRE(response<request_transaction_data_error>(process(session,
        request_transaction_data{999}).response).error_code == "template-id-not-found");
    auto const fourth = template_messages(session.set_template(numbered_template(numbers(1, 2003), null_hash, 1024)));
    (void)session.next_data_frame();
    require_silent(process(session, request_transaction_data{999}));
    auto const behind = drain(session);
    REQUIRE(response<request_transaction_data_error>(behind.back()).error_code == "template-id-not-found");
    REQUIRE(header_of(behind[behind.size() - 2]).msg_type == request_transaction_data_success::message_type);
    REQUIRE(response<request_transaction_data_success>(behind[behind.size() - 2]).template_id
        == fourth.work.template_id);

    // Templates of an earlier tip are stale.
    hash_digest tip{};
    tip[0] = 1;
    session.set_tip(tip);
    REQUIRE(response<request_transaction_data_error>(process(session,
        request_transaction_data{fourth.work.template_id}).response).error_code == "stale-template-id");

    // One answer waits at a time: the listener reads nothing more until it is
    // sent, so a second request then breaks the protocol.
    auto const fifth = template_messages(session.set_template(numbered_template(numbers(1, 2), tip)));
    require_silent(process(session, request_transaction_data{fifth.work.template_id}));
    require_closed(process(session, request_transaction_data{fifth.work.template_id}));
}

TEST_CASE("sv2 Noise listener serves setup and chain templates over encrypted frames", "[sv2 noise]") {
    noise_listener_fixture fixture;
    auto const trunk = seed_chain(fixture, 5);
    auto const expected = fixture.execute(fixture.chain.chain().fetch_mining_template(blockchain::coinbase_reserve{248 + 120, 3}));
    REQUIRE(expected.has_value());
    auto client = fixture.connect(fixture.start());
    auto transport = fixture.handshake(*client);
    fixture.send(*client, transport, setup());
    auto const success = fixture.receive<setup_connection_success>(*client, transport);
    REQUIRE(success.used_version == 2);
    REQUIRE(success.flags == 0);

    fixture.send(*client, transport, coinbase_output_constraints{120, 3});
    auto const work = fixture.receive<new_template>(*client, transport);
    auto const previous = fixture.receive<set_new_prev_hash>(*client, transport);
    REQUIRE(work.template_id == previous.template_id);
    REQUIRE(work.future_template);
    REQUIRE(work.version == expected->version);
    REQUIRE(work.coinbase_tx_value_remaining == expected->coinbase_value);
    REQUIRE(previous.prev_hash == trunk.back().hash());
    REQUIRE(previous.n_bits == expected->bits);

    fixture.send(*client, transport, request_transaction_data{work.template_id});
    fixture.finish_writing(*client);
    REQUIRE(fixture.receive<request_transaction_data_success>(*client, transport).template_id
        == work.template_id);
    fixture.require_closed(*client);
    fixture.stop();
}

TEST_CASE("sv2 Noise listener never downgrades and bounds incomplete handshakes", "[sv2 noise]") {
    noise_listener_fixture fixture;
    fixture.noise_settings.handshake_timeout_seconds = 1;
    fixture.configure();
    auto const port = fixture.start();
    SECTION("plaintext setup is closed without a setup response") {
        auto client = fixture.connect(port);
        auto const bytes = frame(setup());
        fixture.write(*client, byte_span(bytes).first(3));
        fixture.write(*client, byte_span(bytes).subspan(3));
        fixture.finish_writing(*client);
        fixture.require_closed(*client);
    }
    SECTION("idle and incomplete first flights expire") {
        auto idle = fixture.connect(port);
        auto partial = fixture.connect(port);
        fixture.write(*partial, data_chunk(63, 1));
        fixture.require_closed(*idle);
        fixture.require_closed(*partial);
    }
    SECTION("stop joins an incomplete handshake") {
        auto client = fixture.connect(port);
        fixture.write(*client, data_chunk(13, 1));
        fixture.stop();
        fixture.require_closed(*client);
    }
    fixture.stop();
}

TEST_CASE("sv2 Noise listener closes on split encrypted tag failures", "[sv2 noise]") {
    for (bool const payload_tag : {false, true}) {
        CAPTURE(payload_tag);
        noise_listener_fixture fixture;
        auto client = fixture.connect(fixture.start());
        auto transport = fixture.handshake(*client);
        auto bytes = transport.encrypt_frame(frame(setup()));
        REQUIRE(bytes.has_value());
        (*bytes)[payload_tag ? bytes->size() - 1 : noise_transport::encrypted_header_size - 1] ^= 1;
        if ( ! payload_tag) bytes->resize(noise_transport::encrypted_header_size);
        auto const split = payload_tag ? bytes->size() - 1 : noise_transport::encrypted_header_size - 1;
        fixture.write(*client, byte_span(*bytes).first(split));
        fixture.write(*client, byte_span(*bytes).subspan(split));
        fixture.finish_writing(*client);
        fixture.require_closed(*client);
        fixture.stop();
    }
}

TEST_CASE("sv2 Noise listener refuses handshakes once an offline certificate expires", "[sv2 noise]") {
    noise_listener_fixture fixture;
    auto const unix_now = [] {
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    };
    auto const now = unix_now();
    std::string error;
    auto const online = load_noise_credentials(fixture.noise_settings, now, error);
    REQUIRE(online.has_value());
    REQUIRE(online->authority.has_value());
    auto const certificate = noise_certificate::sign(online->authority->secret(),
        online->static_key.public_key(), now - 10, now + 2);
    REQUIRE(certificate.has_value());
    fixture.noise_settings.certificate_file = (fixture.chain.dir() / "offline-certificate").string();
    {
        auto const bytes = certificate->bytes();
        std::ofstream output(fixture.noise_settings.certificate_file, std::ios::binary);
        output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
        REQUIRE(output.good());
    }
    fixture.noise_settings.authority_public_key = noise_authority_key(fixture.authority);
    fixture.configure();
    auto const port = fixture.start();

    REQUIRE(fixture.wait_until([&] { return unix_now() > now + 2; }));
    auto client = fixture.connect(port);
    noise_initiator initiator(noise_keypair::generate(), fixture.authority, unix_now());
    auto const first = initiator.start();
    REQUIRE(first.has_value());
    fixture.write(*client, *first);
    fixture.require_closed(*client);
    fixture.stop();
}

TEST_CASE("sv2 Noise listener reports invalid credentials before binding", "[sv2 noise]") {
    noise_listener_fixture fixture;
    fixture.noise_settings.certificate_file = (fixture.chain.dir() / "absent-certificate").string();
    fixture.noise_settings.authority_public_key = noise_authority_key(fixture.authority);
    fixture.configure();
    fixture.run();
    auto const state = fixture.listening();
    REQUIRE(state->error == std::make_error_code(std::errc::invalid_argument));
    REQUIRE(state->port == 0);
    REQUIRE(fixture.wait_until([&] { return fixture.completed->done; }));
    REQUIRE_FALSE(fixture.completed->error);
}

TEST_CASE("sv2 Noise stop joins partial encrypted headers and payloads", "[sv2 noise]") {
    noise_listener_fixture fixture;
    auto const port = fixture.start();
    auto in_header = fixture.connect(port);
    auto header_transport = fixture.handshake(*in_header);
    auto const header_bytes = header_transport.encrypt_frame(frame(setup()));
    REQUIRE(header_bytes);
    fixture.write(*in_header, byte_span(*header_bytes).first(3));

    auto in_payload = fixture.connect(port);
    auto payload_transport = fixture.handshake(*in_payload);
    auto const payload_bytes = payload_transport.encrypt_frame(frame(setup()));
    REQUIRE(payload_bytes);
    fixture.write(*in_payload,
        byte_span(*payload_bytes).first(noise_transport::encrypted_header_size + 1));

    auto ready = fixture.connect(port);
    auto ready_transport = fixture.handshake(*ready);
    fixture.send(*ready, ready_transport, setup());
    REQUIRE(fixture.receive<setup_connection_success>(*ready, ready_transport).used_version == 2);
    fixture.stop();
    fixture.require_closed(*in_header);
    fixture.require_closed(*in_payload);
    fixture.require_closed(*ready);
}

TEST_CASE("sv2 Noise preserves setup and frame refusal across reads", "[sv2 noise]") {
    noise_listener_fixture fixture;
    auto client = fixture.connect(fixture.start());
    auto transport = fixture.handshake(*client);
    SECTION("unsupported setup receives an encrypted error") {
        auto message = setup();
        message.protocol = setup_connection::protocol_mining;
        fixture.send(*client, transport, message);
        fixture.finish_writing(*client);
        REQUIRE(fixture.receive<setup_connection_error>(*client, transport).error_code
            == "unsupported-protocol");
    }
    SECTION("channel messages remain invalid") {
        fixture.send(*client, transport, setup());
        (void)fixture.receive<setup_connection_success>(*client, transport);
        auto const bytes = transport.encrypt_frame(frame(coinbase_output_constraints{120, 3},
            message_header::channel_msg_flag));
        REQUIRE(bytes);
        fixture.write(*client, byte_span(*bytes).first(21));
        fixture.write(*client, byte_span(*bytes).subspan(21));
        fixture.finish_writing(*client);
    }
    SECTION("an oversized authenticated header is refused before its payload") {
        auto plain = to_data_chunk(message_header{0, 0,
            template_provider_session::max_payload_size + 1});
        plain.resize(message_header::size + template_provider_session::max_payload_size + 1);
        auto const bytes = transport.encrypt_frame(plain);
        REQUIRE(bytes);
        fixture.write(*client, byte_span(*bytes).first(21));
        fixture.write(*client, byte_span(*bytes).subspan(21, 1));
        fixture.finish_writing(*client);
    }
    SECTION("an incomplete encrypted payload is refused at EOF") {
        auto const bytes = transport.encrypt_frame(frame(setup()));
        REQUIRE(bytes);
        fixture.write(*client, byte_span(*bytes).first(21));
        fixture.write(*client,
            byte_span(*bytes).subspan(21, bytes->size() - 22));
        fixture.finish_writing(*client);
    }
    fixture.require_closed(*client);
    fixture.stop();
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
    REQUIRE(fixture.execute(fixture.chain.chain().submit_transaction(
        std::make_shared<domain::message::transaction const>(parent))) == error::success);
    REQUIRE(fixture.execute(fixture.chain.chain().submit_transaction(
        std::make_shared<domain::message::transaction const>(child))) == error::success);

    auto const expected = fixture.execute(fixture.chain.chain().fetch_mining_template(blockchain::coinbase_reserve{248 + 120, 3}));
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
    listener_fixture fixture(loopback_settings(), true);
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
    // Neither a repeated solution (on the replaced tip's work, so submitted
    // again and found a duplicate) nor an id never issued gets a TDP reply. The
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

TEST_CASE("sv2 submits solutions for the replaced tip's templates, not older ones", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings(), true);
    (void)seed_chain(fixture, 5);
    submission_pipeline pipeline(fixture);
    auto client = fixture.connect(fixture.start());
    fixture.write(*client, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*client);
    auto const job = request_template(fixture, *client);
    auto const solved = solution_block(job, {});
    fixture.write(*client, frame(solution_message(job, solved)));
    auto const next = receive_template(fixture, *client);
    REQUIRE(next.prev.prev_hash == solved.hash());

    // A block found on the replaced tip's work, sent after the new tip: a
    // rival at the same height, which the chain judges and keeps as a side
    // branch. Another timestamp makes it another block.
    auto late = job;
    late.prev.header_timestamp += 1;
    auto const rival = solution_block(late, {});
    fixture.write(*client, frame(solution_message(late, rival)));
    // Its transaction data stays stale. This reply follows the submission's
    // answer, so the chain has judged the rival by now.
    fixture.write(*client, frame(request_transaction_data{job.work.template_id}));
    REQUIRE(fixture.receive<request_transaction_data_error>(*client).error_code == "stale-template-id");
    auto const& index = fixture.chain.organizer().index();
    REQUIRE(index.find(rival.hash()) != blockchain::header_index::null_index);
    REQUIRE(fixture.chain.chain().chain_view()->tip_hash == solved.hash());

    // One tip later that work is two tips back, and a solution on it is
    // ignored. Had it been submitted, its header would be kept like the rival's.
    auto const extended = solution_block(next, {});
    fixture.write(*client, frame(solution_message(next, extended)));
    auto const latest = receive_template(fixture, *client);
    REQUIRE(latest.prev.prev_hash == extended.hash());
    late.prev.header_timestamp += 1;
    auto const older = solution_block(late, {});
    fixture.write(*client, frame(solution_message(late, older)));
    fixture.write(*client, frame(request_transaction_data{latest.work.template_id}));
    fixture.finish_writing(*client);
    REQUIRE(fixture.receive<request_transaction_data_success>(*client).template_id == latest.work.template_id);
    REQUIRE(index.find(older.hash()) == blockchain::header_index::null_index);
    REQUIRE(fixture.chain.chain().chain_view()->tip_hash == extended.hash());
    fixture.require_closed(*client);
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

TEST_CASE("sv2 listener pushes chain templates to a client that negotiated BCH large templates", "[sv2 template_provider]") {
    listener_fixture fixture(loopback_settings());
    auto const trunk = seed_chain(fixture, 101);
    auto const view = fixture.chain.chain().chain_view();
    REQUIRE(view);
    auto const& first_coinbase = trunk.front().transactions().front();
    auto const parent = test::spend_p2pkh(first_coinbase, 0,
        first_coinbase.outputs().front().value() - 1000, view->state->enabled_flags());
    auto const child = test::spend_p2pkh(parent, 0,
        parent.outputs().front().value() - 1000, view->state->enabled_flags());
    REQUIRE(fixture.execute(fixture.chain.chain().submit_transaction(
        std::make_shared<domain::message::transaction const>(parent))) == error::success);
    REQUIRE(fixture.execute(fixture.chain.chain().submit_transaction(
        std::make_shared<domain::message::transaction const>(child))) == error::success);
    auto const expected = fixture.execute(fixture.chain.chain().fetch_mining_template(blockchain::coinbase_reserve{248 + 120, 3}));
    REQUIRE(expected.has_value());
    REQUIRE(expected->selection.txs.size() == 2);
    auto const raw = raw_transactions(*expected);

    auto client = fixture.connect(fixture.start());
    fixture.write(*client, frame(setup()));
    (void)fixture.receive<setup_connection_success>(*client);
    fixture.write(*client, frame(request_extensions{1, {large_templates_extension}}));
    auto const granted = fixture.receive<request_extensions_success>(*client);
    REQUIRE(granted.request_id == 1);
    REQUIRE(granted.supported_extensions == std::vector<uint16_t>{large_templates_extension});

    // The first push is full, without a request. Two transactions fit the
    // core Success alone.
    auto const job = request_template(fixture, *client);
    auto const data = fixture.receive<request_transaction_data_success>(*client);
    REQUIRE(data.template_id == job.work.template_id);
    REQUIRE(data.transaction_list == raw);
    REQUIRE(client_merkle_path(data.transaction_list) == job.work.merkle_path);
    (void)solution_block(job, data.transaction_list);

    // A refresh on the same tip keeps both, and adds nothing.
    auto const refreshed = request_template(fixture, *client, {200, 4});
    REQUIRE(refreshed.prev.prev_hash == job.prev.prev_hash);
    auto const kept = fixture.receive<request_transaction_data_kept>(*client);
    REQUIRE(kept.template_id == refreshed.work.template_id);
    REQUIRE(kept.base_template_id == job.work.template_id);
    REQUIRE(kept.kept == data_chunk{0x03});
    auto const added = fixture.receive<request_transaction_data_success>(*client);
    REQUIRE(added.template_id == refreshed.work.template_id);
    REQUIRE(added.transaction_list.empty());
    REQUIRE(client_merkle_path(raw) == refreshed.work.merkle_path);

    // The core request is answered with the whole list and leaves the base.
    fixture.write(*client, frame(request_transaction_data{job.work.template_id}));
    REQUIRE(fixture.receive<request_transaction_data_success>(*client).transaction_list == raw);
    auto const third = request_template(fixture, *client, {300, 5});
    REQUIRE(fixture.receive<request_transaction_data_kept>(*client).base_template_id
        == refreshed.work.template_id);
    fixture.finish_writing(*client);
    REQUIRE(fixture.receive<request_transaction_data_success>(*client).template_id == third.work.template_id);
    fixture.require_closed(*client);
    fixture.stop();
}
