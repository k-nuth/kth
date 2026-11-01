// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Behavior of the Cap'n Proto RPC backend through its real transport: a real
// generated client (its own KJ event loop on the test thread) talks over a
// loopback socket to the real server, whose handlers are either a controllable
// fake reader (cancellation, admission, shutdown) or the real chain_reader.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/experimental/channel_error.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>

#include <kth/node/capnp_rpc/reader.hpp>
#include <kth/node/capnp_rpc/server.hpp>
#include <kth/rpc/v1/chain.capnp.h>

#if ! defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "../src/capnp_rpc/bridge.hpp"
#include "sync_harness.hpp"

using namespace kth;
using namespace std::chrono_literals;
namespace crpc = kth::node::capnp_rpc;
namespace v1 = kth::rpc::v1;

namespace {

constexpr char const* test_user = "kth";
constexpr char const* test_password = "s3cret";

hash_digest filled(std::uint8_t value) {
    hash_digest hash;
    hash.fill(value);
    return hash;
}

// Handlers whose behavior a test controls. Set the std::function members before
// the server starts; counters are readable from the test thread at any time.
struct fake_reader final : crpc::reader {
    explicit fake_reader(::asio::any_io_executor executor)
        : gate(executor, 64) {}

    std::function<crpc::result<crpc::chain_info>()> chain_info = [] {
        return crpc::chain_info{7, 9, filled(0x11)};
    };
    std::function<crpc::result<crpc::block_data>(hash_digest const&)> block = [](hash_digest const&) {
        return crpc::block_data{5, data_chunk{1, 2, 3, 4}};
    };
    std::function<crpc::result<crpc::transaction_data>(hash_digest const&)> transaction = [](hash_digest const&) {
        return crpc::transaction_data{data_chunk{9, 8, 7}, std::nullopt};
    };

    // While true each call parks on the gate until released or cancelled.
    std::atomic<bool> hold{false};
    ::asio::experimental::concurrent_channel<void(std::error_code)> gate;
    std::atomic<int> calls{0};
    std::atomic<int> running{0};
    std::atomic<int> cancelled{0};

    void release(int count) {
        for (int i = 0; i < count; ++i) gate.try_send(std::error_code{});
    }

    ::asio::awaitable<void> enter() {
        ++calls;
        ++running;
        struct leave {
            std::atomic<int>& running;
            ~leave() { --running; }
        } on_exit{running};
        if (hold.load()) {
            try {
                co_await gate.async_receive(::asio::use_awaitable);
            } catch (std::system_error const& error) {
                if (error.code() == ::asio::error::operation_aborted ||
                    error.code() == ::asio::experimental::error::channel_cancelled) {
                    ++cancelled;
                }
                throw;
            }
        }
    }

    ::asio::awaitable<crpc::result<crpc::chain_info>> get_chain_info() override {
        co_await enter();
        co_return chain_info();
    }
    ::asio::awaitable<crpc::result<crpc::block_data>> get_block(hash_digest const& hash) override {
        co_await enter();
        co_return block(hash);
    }
    ::asio::awaitable<crpc::result<crpc::transaction_data>> get_transaction(hash_digest const& hash) override {
        co_await enter();
        co_return transaction(hash);
    }
};


// The server under test, on its own executor, with the reader injected.
struct harness {
    explicit harness(crpc::limits limits = {}, std::function<void(fake_reader&)> setup = {},
        std::string bind = "127.0.0.1", bool run = true)
        : reader(pool.get_executor()) {
        settings.enabled = true;
        settings.bind = std::move(bind);
        settings.port = 0;
        settings.user = test_user;
        settings.password = test_password;
        if (setup) setup(reader);
        crpc::server_options options;
        options.limits = limits;
        options.on_listening = [this](std::uint16_t bound) { listening.set_value(bound); };
        server = std::make_unique<crpc::server>(settings, reader, pool.get_executor(), options);
        if (run) start();
    }

    void start() {
        finished = ::asio::co_spawn(pool, server->run(), ::asio::use_future);
    }

    std::uint16_t port() {
        REQUIRE(bound.wait_for(10s) == std::future_status::ready);
        return bound.get();
    }

    // Stop and require that run() joins everything in bounded time.
    bool stop_and_join() {
        server->stop();
        if ( ! finished.valid()) return true;
        if (finished.wait_for(15s) != std::future_status::ready) return false;
        finished.get();
        return true;
    }

    ~harness() {
        if (server) {
            server->stop();
            if (finished.valid()) finished.wait_for(15s);
        }
        pool.join();
    }

    ::asio::thread_pool pool{2};
    node::rpc_settings settings;
    fake_reader reader;
    std::promise<std::uint16_t> listening;
    std::shared_future<std::uint16_t> bound = listening.get_future().share();
    std::unique_ptr<crpc::server> server;
    std::future<void> finished;
};

// One KJ event loop per thread: all clients of a test share it.
struct kj_env {
    kj::AsyncIoContext io = kj::setupAsyncIo();

    template <typename T>
    T wait(kj::Promise<T>&& promise) {
        return io.provider->getTimer()
            .timeoutAfter(10 * kj::SECONDS, kj::mv(promise)).wait(io.waitScope);
    }

    void sleep(std::chrono::milliseconds time) {
        io.provider->getTimer().afterDelay(time.count() * kj::MILLISECONDS).wait(io.waitScope);
    }
};

// A client queues its calls and only writes them while its event loop runs, so
// waiting for the server to observe something must run that loop meanwhile.
bool eventually(kj_env& env, std::function<bool()> const& condition,
    std::chrono::milliseconds limit = 5s) {
    auto const end = std::chrono::steady_clock::now() + limit;
    while ( ! condition()) {
        if (std::chrono::steady_clock::now() > end) return false;
        env.sleep(5ms);
    }
    return true;
}

struct client {
    client(kj_env& env, std::uint16_t port)
        : env(env)
        , stream(env.wait(env.io.provider->getNetwork()
              .parseAddress("127.0.0.1", port)
              .then([](kj::Own<kj::NetworkAddress> address) { return address->connect(); })))
        , rpc(*stream)
        , gateway(rpc.bootstrap().castAs<v1::Gateway>()) {}

    // Returns the capability, or null with the error code recorded.
    kj::Maybe<v1::Chain::Client> login(char const* name = test_user, char const* secret = test_password) {
        auto request = gateway.loginRequest();
        request.setUser(name);
        request.setPassword(secret);
        auto response = env.wait(request.send());
        auto result = response.getResult();
        if (result.isChain()) return result.getChain();
        last_error = result.getError().getCode();
        return nullptr;
    }

    v1::Chain::Client chain() {
        KJ_IF_MAYBE(chain, login()) { return *chain; }
        throw std::runtime_error("login failed");
    }

    kj_env& env;
    kj::Own<kj::AsyncIoStream> stream;
    capnp::TwoPartyClient rpc;
    v1::Gateway::Client gateway;
    v1::ErrorCode last_error = v1::ErrorCode::INTERNAL;
};

kj::ArrayPtr<kj::byte const> bytes(hash_digest const& hash) {
    return kj::arrayPtr(reinterpret_cast<kj::byte const*>(hash.data()), hash.size());
}

auto block_request(v1::Chain::Client& chain, hash_digest const& hash) {
    auto request = chain.getBlockRequest();
    request.setHash(bytes(hash));
    return request;
}

// True if the promise fails with a transport exception (closed connection...).
template <typename P>
bool transport_fails(kj_env& env, P&& promise) {
    return kj::runCatchingExceptions([&] { env.wait(kj::mv(promise)); }) != nullptr;
}

} // namespace

TEST_CASE("capnp rpc returns typed chain info, blocks and transactions", "[capnp rpc]") {
    harness node({}, [](fake_reader& reader) {
        reader.block = [](hash_digest const&) {
            return crpc::block_data{123, data_chunk{0xde, 0xad, 0xbe, 0xef}};
        };
        reader.transaction = [](hash_digest const& hash) -> crpc::result<crpc::transaction_data> {
            if (hash == filled(0xcc)) {
                return crpc::transaction_data{data_chunk{1, 2}, crpc::transaction_data::confirmed_position{40, 3}};
            }
            return crpc::transaction_data{data_chunk{9, 8, 7}, std::nullopt};
        };
    });
    kj_env env;
    client c(env, node.port());
    auto chain = c.chain();

    {
        auto response = env.wait(chain.getChainInfoRequest().send());
        auto result = response.getResult();
        REQUIRE(result.isOk());
        CHECK(result.getOk().getBlockHeight() == 7);
        CHECK(result.getOk().getHeaderHeight() == 9);
        auto best = result.getOk().getBestBlockHash();
        REQUIRE(best.size() == 32);
        CHECK(std::vector<std::uint8_t>(best.begin(), best.end()) ==
              std::vector<std::uint8_t>(32, 0x11));
    }
    {
        auto response = env.wait(block_request(chain, filled(0xab)).send());
        auto result = response.getResult();
        REQUIRE(result.isOk());
        CHECK(result.getOk().getHeight() == 123);
        CHECK(result.getOk().getHash().size() == 32);
        CHECK(result.getOk().getHash()[0] == 0xab);
        auto wire = result.getOk().getWire();
        CHECK(std::vector<std::uint8_t>(wire.begin(), wire.end()) ==
              std::vector<std::uint8_t>{0xde, 0xad, 0xbe, 0xef});
    }
    {
        auto mempool = chain.getTransactionRequest();
        mempool.setHash(bytes(filled(0xaa)));
        auto response = env.wait(mempool.send());
        REQUIRE(response.getResult().isOk());
        CHECK(response.getResult().getOk().getLocation().isMempool());
        CHECK(response.getResult().getOk().getWire().size() == 3);

        auto confirmed = chain.getTransactionRequest();
        confirmed.setHash(bytes(filled(0xcc)));
        auto second = env.wait(confirmed.send());
        REQUIRE(second.getResult().isOk());
        REQUIRE(second.getResult().getOk().getLocation().isConfirmed());
        CHECK(second.getResult().getOk().getLocation().getConfirmed().getBlockHeight() == 40);
        CHECK(second.getResult().getOk().getLocation().getConfirmed().getIndex() == 3);
    }
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc reports malformed hashes without calling the handler", "[capnp rpc]") {
    harness node;
    kj_env env;
    client c(env, node.port());
    auto chain = c.chain();

    for (std::size_t size : {std::size_t{0}, std::size_t{31}, std::size_t{33}, std::size_t{1000}}) {
        std::vector<kj::byte> bad(size, 0x01);
        auto block = chain.getBlockRequest();
        block.setHash(kj::arrayPtr(bad.data(), bad.size()));
        auto response = env.wait(block.send());
        REQUIRE(response.getResult().isError());
        CHECK(response.getResult().getError().getCode() == v1::ErrorCode::INVALID_ARGUMENT);
        CHECK(std::string(response.getResult().getError().getMessage().cStr()).find("32 bytes") != std::string::npos);

        auto transaction = chain.getTransactionRequest();
        transaction.setHash(kj::arrayPtr(bad.data(), bad.size()));
        auto second = env.wait(transaction.send());
        REQUIRE(second.getResult().isError());
        CHECK(second.getResult().getError().getCode() == v1::ErrorCode::INVALID_ARGUMENT);
    }
    CHECK(node.reader.calls == 0);
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc maps handler failures, missing objects and exceptions explicitly", "[capnp rpc]") {
    harness node({}, [](fake_reader& reader) {
        reader.block = [](hash_digest const& hash) -> crpc::result<crpc::block_data> {
            if (hash == filled(0x01)) {
                return std::unexpected(crpc::failure{crpc::status::not_found, "no such block"});
            }
            if (hash == filled(0x02)) {
                return std::unexpected(crpc::failure{crpc::status::unavailable, "node is stopping"});
            }
            throw std::runtime_error("database path /secret/location corrupt");
        };
    });
    kj_env env;
    client c(env, node.port());
    auto chain = c.chain();

    auto check = [&](hash_digest const& hash, v1::ErrorCode code) {
        auto response = env.wait(block_request(chain, hash).send());
        REQUIRE(response.getResult().isError());
        CHECK(response.getResult().getError().getCode() == code);
        return std::string(response.getResult().getError().getMessage().cStr());
    };
    CHECK(check(filled(0x01), v1::ErrorCode::NOT_FOUND) == "no such block");
    CHECK(check(filled(0x02), v1::ErrorCode::UNAVAILABLE) == "node is stopping");
    // An exception is an internal error and must not leak its text to the peer.
    CHECK(check(filled(0x03), v1::ErrorCode::INTERNAL) == "internal error");

    // The connection and its capability survive failed calls.
    auto info = env.wait(chain.getChainInfoRequest().send());
    CHECK(info.getResult().isOk());
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc refuses results above the response limit", "[capnp rpc]") {
    crpc::limits limits;
    limits.max_payload_bytes = 1024;
    harness node(limits, [](fake_reader& reader) {
        reader.block = [](hash_digest const&) {
            return crpc::block_data{1, data_chunk(2048, 0x55)};
        };
        reader.transaction = [](hash_digest const&) {
            return crpc::transaction_data{data_chunk(1024, 0x55), std::nullopt};   // exactly at the limit
        };
    });
    kj_env env;
    client c(env, node.port());
    auto chain = c.chain();

    auto too_big = env.wait(block_request(chain, filled(1)).send());
    REQUIRE(too_big.getResult().isError());
    CHECK(too_big.getResult().getError().getCode() == v1::ErrorCode::RESOURCE_EXHAUSTED);

    auto request = chain.getTransactionRequest();
    request.setHash(bytes(filled(2)));
    auto fits = env.wait(request.send());
    CHECK(fits.getResult().isOk());
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc requires login and closes after repeated failures", "[capnp rpc]") {
    harness node;
    kj_env env;
    {
        client c(env, node.port());
        CHECK(c.login("kth", "wrong") == nullptr);
        CHECK(c.last_error == v1::ErrorCode::UNAUTHENTICATED);
        CHECK(c.login("other", test_password) == nullptr);
        // A right login after fewer failures than the limit still works.
        CHECK(c.login() != nullptr);
    }
    client c(env, node.port());
    for (int attempt = 0; attempt < 3; ++attempt) {
        CHECK(c.login("kth", "wrong") == nullptr);
        CHECK(c.last_error == v1::ErrorCode::UNAUTHENTICATED);
    }
    env.sleep(600ms);
    auto request = c.gateway.loginRequest();
    request.setUser(test_user);
    request.setPassword(test_password);
    CHECK(transport_fails(env, request.send()));
    CHECK(node.reader.calls == 0);
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc closes connections that do not log in in time", "[capnp rpc]") {
    crpc::limits limits;
    limits.login_timeout = 1s;
    harness node(limits);
    kj_env env;

    client idle(env, node.port());
    client active(env, node.port());
    auto chain = active.chain();
    env.sleep(1500ms);

    auto request = idle.gateway.loginRequest();
    request.setUser(test_user);
    request.setPassword(test_password);
    CHECK(transport_fails(env, request.send()));
    // A connection that logged in before the deadline is unaffected.
    CHECK(env.wait(chain.getChainInfoRequest().send()).getResult().isOk());
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc drops oversized requests", "[capnp rpc]") {
    harness node;
    kj_env env;
    client c(env, node.port());
    auto request = c.gateway.loginRequest();
    request.setUser(test_user);
    std::string huge(256 * 1024, 'x');
    request.setPassword(huge.c_str());
    CHECK(transport_fails(env, request.send()));
    // The server is still serving others.
    client other(env, node.port());
    CHECK(other.login() != nullptr);
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc bounds calls in flight without queueing", "[capnp rpc]") {
    crpc::limits limits;
    limits.max_in_flight = 2;
    limits.max_in_flight_per_connection = 2;
    harness node(limits, [](fake_reader& reader) { reader.hold = true; });
    kj_env env;
    client c(env, node.port());
    auto chain = c.chain();

    auto first = block_request(chain, filled(1)).send();
    auto second = block_request(chain, filled(2)).send();
    REQUIRE(eventually(env, [&] { return node.reader.running == 2; }));

    // Over the bound: answered at once, explicitly, while the others still wait.
    auto third = env.wait(block_request(chain, filled(3)).send());
    REQUIRE(third.getResult().isError());
    CHECK(third.getResult().getError().getCode() == v1::ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(node.reader.calls == 2);

    node.reader.release(2);
    CHECK(env.wait(kj::mv(first)).getResult().isOk());
    CHECK(env.wait(kj::mv(second)).getResult().isOk());

    // Slots come back once the work is done.
    node.reader.hold = false;
    CHECK(eventually(env, [&] {
        return env.wait(block_request(chain, filled(4)).send()).getResult().isOk();
    }));
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc limits one connection so others keep their slots", "[capnp rpc]") {
    crpc::limits limits;
    limits.max_in_flight = 4;
    limits.max_in_flight_per_connection = 1;
    harness node(limits, [](fake_reader& reader) { reader.hold = true; });
    kj_env env;
    client a(env, node.port());
    client b(env, node.port());
    auto chain_a = a.chain();
    auto chain_b = b.chain();

    auto held = block_request(chain_a, filled(1)).send();
    REQUIRE(eventually(env, [&] { return node.reader.running == 1; }));
    auto refused = env.wait(block_request(chain_a, filled(2)).send());
    REQUIRE(refused.getResult().isError());
    CHECK(refused.getResult().getError().getCode() == v1::ErrorCode::RESOURCE_EXHAUSTED);

    auto other = block_request(chain_b, filled(3)).send();
    REQUIRE(eventually(env, [&] { return node.reader.running == 2; }));
    node.reader.release(2);
    CHECK(env.wait(kj::mv(held)).getResult().isOk());
    CHECK(env.wait(kj::mv(other)).getResult().isOk());
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc closes connections above the connection limit", "[capnp rpc]") {
    crpc::limits limits;
    limits.max_connections = 1;
    harness node(limits);
    kj_env env;
    client first(env, node.port());
    auto chain = first.chain();

    client second(env, node.port());
    auto request = second.gateway.loginRequest();
    request.setUser(test_user);
    request.setPassword(test_password);
    CHECK(transport_fails(env, request.send()));
    CHECK(env.wait(chain.getChainInfoRequest().send()).getResult().isOk());
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc cancels the handler when the client drops a call", "[capnp rpc]") {
    crpc::limits limits;
    limits.max_in_flight = 1;
    limits.max_in_flight_per_connection = 1;
    harness node(limits, [](fake_reader& reader) { reader.hold = true; });
    kj_env env;
    client c(env, node.port());
    auto chain = c.chain();

    {
        auto call = block_request(chain, filled(1)).send();
        REQUIRE(eventually(env, [&] { return node.reader.running == 1; }));
    }   // promise dropped: Finish is sent

    REQUIRE(eventually(env, [&] { return node.reader.cancelled == 1; }));
    REQUIRE(eventually(env, [&] { return node.reader.running == 0; }));

    // The single slot was handed back: the next call is admitted and served.
    node.reader.hold = false;
    CHECK(eventually(env, [&] {
        return env.wait(block_request(chain, filled(2)).send()).getResult().isOk();
    }));
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc cancels running work when the connection closes", "[capnp rpc]") {
    harness node({}, [](fake_reader& reader) { reader.hold = true; });
    kj_env env;
    {
        client c(env, node.port());
        auto chain = c.chain();
        auto call = block_request(chain, filled(1)).send();
        REQUIRE(eventually(env, [&] { return node.reader.running == 1; }));
    }   // client and connection destroyed with the call outstanding

    CHECK(eventually(env, [&] { return node.reader.cancelled == 1; }));
    CHECK(eventually(env, [&] { return node.reader.running == 0; }));
    CHECK(node.stop_and_join());
}

TEST_CASE("capnp rpc stop before run starts nothing", "[capnp rpc]") {
    harness node({}, {}, "127.0.0.1", /*run*/ false);
    node.server->stop();
    node.start();
    REQUIRE(node.finished.wait_for(5s) == std::future_status::ready);
    node.finished.get();
    CHECK(node.reader.calls == 0);
    CHECK(node.bound.wait_for(0s) != std::future_status::ready);
}

TEST_CASE("capnp rpc shutdown joins running operations and closes clients", "[capnp rpc]") {
    harness node({}, [](fake_reader& reader) { reader.hold = true; });
    kj_env env;
    client c(env, node.port());
    auto chain = c.chain();
    auto call = block_request(chain, filled(1)).send();
    REQUIRE(eventually(env, [&] { return node.reader.running == 1; }));

    REQUIRE(node.stop_and_join());

    // The operation was cancelled at its owner, and the client was not left
    // waiting: its call ends (either with an error result or a closed connection).
    CHECK(node.reader.cancelled == 1);
    CHECK(node.reader.running == 0);
    bool answered_ok = false;
    kj::runCatchingExceptions([&] { answered_ok = env.wait(kj::mv(call)).getResult().isOk(); });
    CHECK( ! answered_ok);
    // Nobody can connect any more.
    CHECK_THROWS(client(env, node.port()));
}

TEST_CASE("capnp rpc refuses to bind a non-loopback address", "[capnp rpc]") {
    harness node({}, {}, "0.0.0.0");
    REQUIRE(node.finished.wait_for(5s) == std::future_status::ready);
    node.finished.get();
    CHECK(node.bound.wait_for(0s) != std::future_status::ready);
}

TEST_CASE("capnp rpc serves the real chain through chain_reader", "[capnp rpc]") {
    test::chain_fixture fixture("capnp_rpc_real_chain");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Three blocks connected through the node's own path, so that heights, the
    // header index and the flat-file block store are all really populated.
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (3 + 30) * test::block_spacing;
    std::vector<domain::chain::block> trunk;
    auto previous = genesis.hash();
    for (uint32_t height = 1; height <= 3; ++height) {
        trunk.push_back(test::mine_block(previous, height, base_time + height * test::block_spacing, 0, {}, 0));
        previous = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == 3);
    test::persist_headers(fixture, trunk, 1);
    test::connect_bodies(fixture, trunk, 1);

    crpc::chain_reader reader(fixture.chain());
    ::asio::thread_pool pool(2);
    node::rpc_settings settings;
    settings.enabled = true;
    settings.port = 0;
    settings.user = test_user;
    settings.password = test_password;
    std::promise<std::uint16_t> listening;
    crpc::server_options options;
    options.on_listening = [&](std::uint16_t port) { listening.set_value(port); };
    crpc::server server(settings, reader, pool.get_executor(), options);
    auto finished = ::asio::co_spawn(pool, server.run(), ::asio::use_future);
    auto ready = listening.get_future();
    REQUIRE(ready.wait_for(10s) == std::future_status::ready);

    kj_env env;
    client c(env, ready.get());
    auto chain = c.chain();

    auto info = env.wait(chain.getChainInfoRequest().send());
    REQUIRE(info.getResult().isOk());
    CHECK(info.getResult().getOk().getBlockHeight() == 3);
    CHECK(info.getResult().getOk().getHeaderHeight() >= 3);
    auto best = info.getResult().getOk().getBestBlockHash();
    REQUIRE(best.size() == 32);
    auto const tip = trunk.back().hash();
    CHECK(std::equal(best.begin(), best.end(), tip.begin()));

    // The exact bytes the node stored, at the right height.
    auto block = env.wait(block_request(chain, trunk[1].hash()).send());
    REQUIRE(block.getResult().isOk());
    CHECK(block.getResult().getOk().getHeight() == 2);
    auto wire = block.getResult().getOk().getWire();
    auto const expected = to_data_chunk(trunk[1]);
    CHECK(std::vector<std::uint8_t>(wire.begin(), wire.end()) ==
          std::vector<std::uint8_t>(expected.begin(), expected.end()));

    // Not on this chain at all.
    auto unknown = env.wait(block_request(chain, filled(0x42)).send());
    REQUIRE(unknown.getResult().isError());
    CHECK(unknown.getResult().getError().getCode() == v1::ErrorCode::NOT_FOUND);

    // No transaction index and an empty mempool: explicit notFound.
    auto missing_tx = chain.getTransactionRequest();
    missing_tx.setHash(bytes(trunk[1].transactions().front().hash()));
    auto tx = env.wait(missing_tx.send());
    REQUIRE(tx.getResult().isError());
    CHECK(tx.getResult().getError().getCode() == v1::ErrorCode::NOT_FOUND);

    server.stop();
    REQUIRE(finished.wait_for(15s) == std::future_status::ready);
    finished.get();
    pool.join();
}

TEST_CASE("capnp rpc maps a confirmed transaction as (tx, index, height)", "[capnp rpc]") {
    // fetch_transaction returns (transaction, index, height); both numbers are
    // size_t, so a swap would compile. Distinct values make one visible.
    auto const genesis = domain::chain::block::genesis_regtest();
    auto tx = std::make_shared<domain::message::transaction const>(genesis.transactions().front());
    std::tuple<std::shared_ptr<domain::message::transaction const>, std::size_t, std::size_t> const confirmed{
        tx, /*index*/ 3, /*height*/ 40};

    auto const mapped = crpc::to_transaction_data(confirmed);
    REQUIRE(mapped.confirmed.has_value());
    CHECK(mapped.confirmed->index == 3);
    CHECK(mapped.confirmed->block_height == 40);
    CHECK(mapped.wire == to_data_chunk(*tx, /*wire*/ true));
}

TEST_CASE("capnp rpc bridge never blocks on a full mailbox and keeps admission until release", "[capnp rpc]") {
    ::asio::thread_pool pool(1);
    crpc::limits limits;
    limits.max_in_flight = 1;
    crpc::detail::bridge shared(pool.get_executor(), limits);

    // Admission: one slot, held until the owner releases it, however many
    // cancels were lost.
    CHECK(shared.try_admit());
    CHECK( ! shared.try_admit());
    shared.release();
    CHECK(shared.try_admit());
    shared.release();

    // The mailbox is bounded and post() reports "full" instead of waiting; the
    // one-off wake/cancel messages are the ones allowed to be lost.
    std::size_t accepted = 0;
    while (shared.post(crpc::detail::cancel_msg{accepted})) {
        REQUIRE(++accepted < 1000);
    }
    CHECK(accepted == 2 * limits.max_in_flight + 8);
    CHECK( ! shared.post(crpc::detail::wake_msg{}));
    pool.join();
}

namespace {

#if ! defined(_WIN32)
struct temp_dir {
    temp_dir() {
        static std::atomic<int> counter{0};
        path = std::filesystem::temp_directory_path() /
            ("kth_capnp_cookie_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(path);
    }
    ~temp_dir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

std::string slurp(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream out;
    out << in.rdbuf();
    return out.str();
}

unsigned mode_of(std::filesystem::path const& path) {
    struct stat info {};
    REQUIRE(::lstat(path.c_str(), &info) == 0);
    return info.st_mode & 07777;
}

// A server with no configured credentials, so it generates a cookie.
struct cookie_node {
    explicit cookie_node(std::filesystem::path cookie_path)
        : reader(pool.get_executor()) {
        settings.enabled = true;
        settings.port = 0;
        crpc::server_options options;
        options.cookie_path = std::move(cookie_path);
        options.on_listening = [this](std::uint16_t port) { listening.set_value(port); };
        server = std::make_unique<crpc::server>(settings, reader, pool.get_executor(), options);
        finished = ::asio::co_spawn(pool, server->run(), ::asio::use_future);
    }
    ~cookie_node() {
        server->stop();
        if (finished.valid()) finished.wait_for(15s);
        pool.join();
    }
    ::asio::thread_pool pool{2};
    node::rpc_settings settings;
    fake_reader reader;
    std::promise<std::uint16_t> listening;
    std::unique_ptr<crpc::server> server;
    std::future<void> finished;
};
#endif

} // namespace

#if ! defined(_WIN32)
TEST_CASE("capnp rpc writes the generated cookie privately and authenticates with it", "[capnp rpc][cookie]") {
    temp_dir dir;
    auto const cookie = dir.path / ".cookie";
    // A stale, world-readable cookie must be replaced, not reused or chmod-raced.
    { std::ofstream(cookie) << "stale"; }
    ::chmod(cookie.c_str(), 0644);

    cookie_node node(cookie);
    auto ready = node.listening.get_future();
    REQUIRE(ready.wait_for(10s) == std::future_status::ready);

    CHECK(mode_of(cookie) == 0600);
    auto const text = slurp(cookie);
    REQUIRE(text.rfind("__cookie__:", 0) == 0);
    CHECK(text.size() == std::string("__cookie__:").size() + 64);
    // No temporary files are left behind.
    std::size_t entries = 0;
    for (auto const& entry : std::filesystem::directory_iterator(dir.path)) {
        (void)entry;
        ++entries;
    }
    CHECK(entries == 1);

    kj_env env;
    client c(env, ready.get());
    CHECK(c.login("__cookie__", text.substr(11).c_str()) != nullptr);
    CHECK(c.login("__cookie__", "wrong") == nullptr);
    CHECK(c.last_error == v1::ErrorCode::UNAUTHENTICATED);
}

TEST_CASE("capnp rpc cookie replacement never writes through a symlink", "[capnp rpc][cookie]") {
    temp_dir dir;
    auto const victim = dir.path / "victim";
    { std::ofstream(victim) << "do not touch"; }
    auto const cookie = dir.path / ".cookie";
    std::filesystem::create_symlink(victim, cookie);

    cookie_node node(cookie);
    auto ready = node.listening.get_future();
    REQUIRE(ready.wait_for(10s) == std::future_status::ready);

    CHECK(slurp(victim) == "do not touch");
    CHECK( ! std::filesystem::is_symlink(cookie));
    CHECK(mode_of(cookie) == 0600);
    CHECK(slurp(cookie).rfind("__cookie__:", 0) == 0);
}

TEST_CASE("capnp rpc does not listen when the cookie cannot be written", "[capnp rpc][cookie]") {
    temp_dir dir;
    cookie_node node(dir.path / "missing-directory" / ".cookie");
    REQUIRE(node.finished.wait_for(10s) == std::future_status::ready);
    node.finished.get();
    CHECK(node.listening.get_future().wait_for(0s) != std::future_status::ready);
    CHECK( ! std::filesystem::exists(dir.path / "missing-directory"));
}
#endif
