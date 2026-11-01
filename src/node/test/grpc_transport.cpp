// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// A real generated gRPC client against the real transport on loopback. Only the
// handlers behind the transport are test doubles.

#include <test_helpers.hpp>

#include <catch2/generators/catch_generators.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

#include <asio/as_tuple.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/system_executor.hpp>
#include <asio/use_awaitable.hpp>

#include "grpc_test_support.hpp"

using namespace kth;
using namespace kth::node;
using namespace kth::node::grpc_rpc;
using kth::node::test::client;
using kth::node::test::basic_auth;
namespace v1 = ::kth::rpc::v1;

namespace {

using gate_channel = ::asio::experimental::concurrent_channel<void(std::error_code)>;

transport_options loopback_options() {
    transport_options options;
    options.port = 0;
    options.user = "user";
    options.password = "secret";
    options.shutdown_grace = std::chrono::milliseconds(200);
    return options;
}

std::string hash_bytes(unsigned char first) {
    std::string bytes(32, '\0');
    bytes[0] = static_cast<char>(first);
    bytes[31] = static_cast<char>(0xff);
    return bytes;
}

// Hand-written handlers: just enough behaviour to observe the transport.
handlers echo_handlers() {
    handlers h;
    h.get_chain_info = [](v1::GetChainInfoRequest const&) -> ::asio::awaitable<result<v1::GetChainInfoResponse>> {
        v1::GetChainInfoResponse response;
        response.set_network(v1::NETWORK_REGTEST);
        response.set_block_height(123456);
        response.set_header_height(123460);
        response.set_best_block_hash(hash_bytes(0x01));
        co_return response;
    };
    h.get_block = [](v1::GetBlockRequest const& request) -> ::asio::awaitable<result<v1::GetBlockResponse>> {
        v1::GetBlockResponse response;
        response.set_raw_block(request.hash());
        response.set_height(7);
        co_return response;
    };
    h.get_transaction = [](v1::GetTransactionRequest const& request)
            -> ::asio::awaitable<result<v1::GetTransactionResponse>> {
        v1::GetTransactionResponse response;
        response.set_raw_transaction(request.hash());
        co_return response;
    };
    return h;
}

template <typename Predicate>
bool eventually(Predicate&& predicate) {
    // Observes asynchronous server bookkeeping from the test thread only; the
    // production code under test does no polling.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ( ! predicate()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

} // namespace

TEST_CASE("grpc transport answers a typed unary call with binary fields", "[grpc transport]") {
    transport server(loopback_options(), echo_handlers());
    auto const port = server.start();
    REQUIRE(port.has_value());
    REQUIRE(*port != 0);
    REQUIRE(server.port() == *port);

    client c(*port);
    auto const info = c.chain_info();
    REQUIRE(info.status.ok());
    CHECK(info.response.network() == v1::NETWORK_REGTEST);
    CHECK(info.response.block_height() == 123456);
    CHECK(info.response.header_height() == 123460);
    CHECK(info.response.best_block_hash() == hash_bytes(0x01));
    CHECK(info.response.best_block_hash().size() == 32);

    // Bytes are carried untouched, including NULs and 0xff, in both directions.
    auto const block = c.block(hash_bytes(0xab));
    REQUIRE(block.status.ok());
    CHECK(block.response.raw_block() == hash_bytes(0xab));
    CHECK(block.response.height() == 7);

    auto const tx = c.transaction(hash_bytes(0xcd));
    REQUIRE(tx.status.ok());
    CHECK(tx.response.raw_transaction() == hash_bytes(0xcd));
    CHECK_FALSE(tx.response.has_confirmation());

    server.stop();
    server.join();
}

TEST_CASE("grpc transport maps handler errors to explicit status codes", "[grpc transport]") {
    struct expectation { status_code code; grpc::StatusCode wire; };
    auto const e = GENERATE(
        expectation{status_code::invalid_argument, grpc::StatusCode::INVALID_ARGUMENT},
        expectation{status_code::not_found, grpc::StatusCode::NOT_FOUND},
        expectation{status_code::resource_exhausted, grpc::StatusCode::RESOURCE_EXHAUSTED},
        expectation{status_code::unavailable, grpc::StatusCode::UNAVAILABLE},
        expectation{status_code::aborted, grpc::StatusCode::ABORTED},
        expectation{status_code::internal, grpc::StatusCode::INTERNAL});

    auto h = echo_handlers();
    h.get_block = [e](v1::GetBlockRequest const&) -> ::asio::awaitable<result<v1::GetBlockResponse>> {
        co_return std::unexpected(rpc_status{e.code, "reason for the client"});
    };
    transport server(loopback_options(), std::move(h));
    auto const port = server.start();
    REQUIRE(port.has_value());

    auto const reply = client(*port).block(hash_bytes(1));
    CHECK(reply.status.error_code() == e.wire);
    CHECK(reply.status.error_message() == "reason for the client");
}

TEST_CASE("grpc transport turns a throwing handler into INTERNAL without leaking detail", "[grpc transport]") {
    auto h = echo_handlers();
    h.get_block = [](v1::GetBlockRequest const&) -> ::asio::awaitable<result<v1::GetBlockResponse>> {
        throw std::runtime_error("secret internal detail");
        co_return v1::GetBlockResponse{};
    };
    transport server(loopback_options(), std::move(h));
    auto const port = server.start();
    REQUIRE(port.has_value());

    client c(*port);
    auto const reply = c.block(hash_bytes(1));
    CHECK(reply.status.error_code() == grpc::StatusCode::INTERNAL);
    CHECK(reply.status.error_message().find("secret") == std::string::npos);
    // The failure of one call does not take the service down.
    CHECK(c.chain_info().status.ok());
}

TEST_CASE("grpc transport rejects missing or wrong credentials before any handler runs", "[grpc transport]") {
    std::atomic<int> invoked{0};
    auto h = echo_handlers();
    h.get_chain_info = [&invoked](v1::GetChainInfoRequest const&)
            -> ::asio::awaitable<result<v1::GetChainInfoResponse>> {
        ++invoked;
        co_return v1::GetChainInfoResponse{};
    };
    transport server(loopback_options(), std::move(h));
    auto const port = server.start();
    REQUIRE(port.has_value());

    CHECK(client(*port, "").chain_info().status.error_code() == grpc::StatusCode::UNAUTHENTICATED);
    CHECK(client(*port, basic_auth("user", "wrong")).chain_info().status.error_code()
        == grpc::StatusCode::UNAUTHENTICATED);
    CHECK(client(*port, basic_auth("other", "secret")).chain_info().status.error_code()
        == grpc::StatusCode::UNAUTHENTICATED);
    CHECK(client(*port, "Bearer secret").chain_info().status.error_code()
        == grpc::StatusCode::UNAUTHENTICATED);
    CHECK(invoked == 0);

    CHECK(client(*port).chain_info().status.ok());
    CHECK(invoked == 1);
}

TEST_CASE("grpc transport bounds request and response sizes explicitly", "[grpc transport]") {
    auto options = loopback_options();
    options.max_request_bytes = 256;
    options.max_response_bytes = 1024;

    auto h = echo_handlers();
    h.get_block = [](v1::GetBlockRequest const& request) -> ::asio::awaitable<result<v1::GetBlockResponse>> {
        v1::GetBlockResponse response;
        response.set_raw_block(std::string(request.hash().size() * 10, 'x'));
        co_return response;
    };
    transport server(options, std::move(h));
    auto const port = server.start();
    REQUIRE(port.has_value());
    client c(*port);

    SECTION("a request over the limit is refused by size") {
        auto const reply = c.block(std::string(1000, 'a'));
        CHECK(reply.status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED);
    }
    SECTION("a response over the limit is refused with the limit in the message") {
        auto const reply = c.block(std::string(200, 'a'));   // 2000 byte answer
        CHECK(reply.status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED);
        CHECK(reply.status.error_message().find("1024") != std::string::npos);
    }
    SECTION("an answer at the limit goes through") {
        auto const reply = c.block(std::string(50, 'a'));    // 500 byte answer
        CHECK(reply.status.ok());
    }
}

TEST_CASE("grpc transport refuses calls past max_in_flight and recovers", "[grpc transport]") {
    auto options = loopback_options();
    options.max_in_flight = 1;

    gate_channel gate(::asio::system_executor(), 1);
    std::promise<void> entered;
    std::atomic<bool> first{true};

    auto h = echo_handlers();
    h.get_block = [&](v1::GetBlockRequest const&) -> ::asio::awaitable<result<v1::GetBlockResponse>> {
        if (first.exchange(false)) {
            entered.set_value();
            co_await gate.async_receive(::asio::as_tuple(::asio::use_awaitable));
        }
        co_return v1::GetBlockResponse{};
    };
    transport server(options, std::move(h));
    auto const port = server.start();
    REQUIRE(port.has_value());
    client c(*port);

    auto blocked = std::async(std::launch::async, [&] { return c.block(hash_bytes(1)); });
    REQUIRE(entered.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(server.in_flight() == 1);

    // Refused at admission: no queueing behind the busy call.
    auto const refused = c.block(hash_bytes(2));
    CHECK(refused.status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED);
    CHECK(refused.status.error_message().find("in-flight") != std::string::npos);

    REQUIRE(gate.try_send(std::error_code{}));
    REQUIRE(blocked.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(blocked.get().status.ok());

    REQUIRE(eventually([&] { return server.in_flight() == 0; }));
    CHECK(c.block(hash_bytes(3)).status.ok());
}

TEST_CASE("grpc transport cancels the handler when the client cancels", "[grpc transport]") {
    gate_channel gate(::asio::system_executor(), 1);
    std::promise<void> entered;
    std::promise<bool> cancelled_seen;
    std::atomic<bool> first{true};

    auto h = echo_handlers();
    h.get_block = [&](v1::GetBlockRequest const&) -> ::asio::awaitable<result<v1::GetBlockResponse>> {
        if (first.exchange(false)) {
            entered.set_value();
            auto [ec] = co_await gate.async_receive(::asio::as_tuple(::asio::use_awaitable));
            // Only a cancellation wakes this: nobody ever sends on the gate here.
            cancelled_seen.set_value(ec == ::asio::experimental::error::channel_cancelled ||
                ec == ::asio::error::operation_aborted);
        }
        co_return v1::GetBlockResponse{};
    };
    auto options = loopback_options();
    options.max_in_flight = 1;
    transport server(options, std::move(h));
    auto const port = server.start();
    REQUIRE(port.has_value());
    client c(*port);

    std::promise<grpc::ClientContext*> context_ready;
    auto context_future = context_ready.get_future();
    auto call = std::async(std::launch::async, [&] {
        return c.call<v1::GetBlockResponse>(&v1::NodeService::Stub::GetBlock,
            [] { v1::GetBlockRequest r; r.set_hash(hash_bytes(1)); return r; }(),
            [&](grpc::ClientContext& context) { context_ready.set_value(&context); });
    });
    auto* context = context_future.get();
    REQUIRE(entered.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready);

    context->TryCancel();
    REQUIRE(call.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(call.get().status.error_code() == grpc::StatusCode::CANCELLED);

    auto observed = cancelled_seen.get_future();
    REQUIRE(observed.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(observed.get());

    // The slot came back and the service still answers.
    REQUIRE(eventually([&] { return server.in_flight() == 0; }));
    CHECK(c.block(hash_bytes(2)).status.ok());
}

TEST_CASE("grpc transport stop before start refuses to start and joins cleanly", "[grpc transport]") {
    transport server(loopback_options(), echo_handlers());
    server.stop();
    server.stop();
    auto const started = server.start();
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().find("stopped before it started") != std::string::npos);
    server.join();
    CHECK(server.port() == 0);
}

TEST_CASE("grpc transport destroyed without start or stop does not hang", "[grpc transport]") {
    transport server(loopback_options(), echo_handlers());
}

TEST_CASE("grpc transport refuses unsafe or invalid configuration before binding", "[grpc transport]") {
    SECTION("non-loopback bind") {
        for (auto const* bind : {"0.0.0.0", "::", "192.0.2.1", "10.0.0.5"}) {
            auto options = loopback_options();
            options.bind = bind;
            transport server(options, echo_handlers());
            auto const started = server.start();
            REQUIRE_FALSE(started.has_value());
            CHECK(started.error().find("loopback") != std::string::npos);
        }
    }
    SECTION("not an IP literal") {
        auto options = loopback_options();
        options.bind = "localhost";
        transport server(options, echo_handlers());
        CHECK_FALSE(server.start().has_value());
    }
    SECTION("no credentials") {
        auto options = loopback_options();
        options.user.clear();
        options.password.clear();
        transport server(options, echo_handlers());
        auto const started = server.start();
        REQUIRE_FALSE(started.has_value());
        CHECK(started.error().find("credentials") != std::string::npos);
    }
    SECTION("zero admission") {
        auto options = loopback_options();
        options.max_in_flight = 0;
        transport server(options, echo_handlers());
        CHECK_FALSE(server.start().has_value());
    }
    SECTION("ipv6 loopback is accepted") {
        auto options = loopback_options();
        options.bind = "::1";
        transport server(options, echo_handlers());
        auto const started = server.start();
        if (started.has_value()) {   // hosts without ::1 cannot listen; that is not this test's subject
            CHECK(client(*started).chain_info().status.error_code() != grpc::StatusCode::UNAUTHENTICATED);
        }
    }
}

TEST_CASE("grpc transport refuses a second start and a port that is taken", "[grpc transport]") {
    transport first(loopback_options(), echo_handlers());
    auto const port = first.start();
    REQUIRE(port.has_value());
    CHECK_FALSE(first.start().has_value());

    auto options = loopback_options();
    options.port = *port;
    transport second(options, echo_handlers());
    auto const started = second.start();
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().find("failed to listen") != std::string::npos);
    second.join();   // nothing started: returns at once

    // The first server is unaffected.
    CHECK(client(*port).chain_info().status.ok());
}

TEST_CASE("grpc transport shuts down with a call in flight", "[grpc transport]") {
    gate_channel gate(::asio::system_executor(), 1);
    std::promise<void> entered;
    std::atomic<bool> first{true};

    auto h = echo_handlers();
    h.get_block = [&](v1::GetBlockRequest const&) -> ::asio::awaitable<result<v1::GetBlockResponse>> {
        if (first.exchange(false)) {
            entered.set_value();
            co_await gate.async_receive(::asio::as_tuple(::asio::use_awaitable));
        }
        co_return v1::GetBlockResponse{};
    };
    transport server(loopback_options(), std::move(h));
    auto const port = server.start();
    REQUIRE(port.has_value());
    client c(*port);

    auto call = std::async(std::launch::async, [&] { return c.block(hash_bytes(1)); });
    REQUIRE(entered.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready);

    // stop() returns at once even though a call is still running...
    auto const before = std::chrono::steady_clock::now();
    server.stop();
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));

    // ...join() is what waits, and the grace period then cancels the stuck call.
    auto joined = std::async(std::launch::async, [&] { server.join(); });
    REQUIRE(joined.wait_for(std::chrono::seconds(20)) == std::future_status::ready);
    REQUIRE(call.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    auto const status = call.get().status.error_code();
    CHECK((status == grpc::StatusCode::CANCELLED || status == grpc::StatusCode::UNAVAILABLE));
    CHECK(server.in_flight() == 0);

    // Once down, nothing answers.
    CHECK_FALSE(client(*port).chain_info().status.ok());
}
