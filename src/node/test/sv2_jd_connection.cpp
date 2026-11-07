// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>

#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/framing.hpp>
#include <kth/node/sv2/jd_connection.hpp>
#include <kth/node/sv2/noise.hpp>

using namespace kth;
using namespace kth::node::sv2;
using namespace std::chrono_literals;

namespace {

uint32_t unix_time() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

noise_keypair keypair(uint8_t value) {
    hash_digest secret{};
    secret.back() = value;
    auto const key = noise_keypair::create(secret, {});
    REQUIRE(key);
    return *key;
}

template <typename Message>
data_chunk frame(Message const& message) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{0, Message::message_type,
        static_cast<uint32_t>(payload.size())});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

setup_connection setup() {
    setup_connection value;
    value.protocol = setup_connection::protocol_job_declaration;
    value.min_version = 2;
    value.max_version = 2;
    value.flags = 1;
    return value;
}

enum class behavior {
    exchange, quiet, silent_handshake, short_handshake,
    split_header_tag, split_payload_tag, oversized_header, partial_frame,
    drop, split_response, coalesced_partial, delayed_certificate
};

// The acceptor reports its bound port after listen(). Every asynchronous wait
// is pumped only to a deadline; fixture cleanup also has a fixed deadline.
struct upstream_fixture {
    ::asio::io_context io;
    ::asio::ip::tcp::acceptor acceptor{io};
    ::asio::ip::tcp::socket peer{io};
    ::asio::steady_timer pause{io};
    noise_keypair authority = keypair(4);
    noise_keypair static_key = keypair(3);
    noise_certificate certificate;
    std::shared_ptr<jd_connection> client;
    std::future<void> server;
    std::future<std::string> result;
    std::vector<data_chunk> sent;
    std::vector<data_chunk> received;
    uint16_t port = 0;
    bool ready = false;
    bool allow_ready = true;
    bool allow_frame = true;
    bool throw_frame = false;
    bool stopping = false;

    upstream_fixture() {
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
        auto const now = unix_time();
        set_certificate(now - 60, now + 600);
    }

    ~upstream_fixture() {
        stopping = true;
        if (client) client->stop();
        std::error_code ignored;
        acceptor.close(ignored);
        peer.close(ignored);
        pause.cancel();
        io.restart();
        io.run_for(1s);
        CHECK(( ! result.valid() || result.wait_for(0s) == std::future_status::ready));
        CHECK(( ! server.valid() || server.wait_for(0s) == std::future_status::ready));
    }

    void set_certificate(uint32_t from, uint32_t until) {
        auto const signed_value = noise_certificate::sign(authority.secret(),
            static_key.public_key(), from, until);
        REQUIRE(signed_value);
        certificate = *signed_value;
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 3s) {
        auto const end = std::chrono::steady_clock::now() + timeout;
        while ( ! predicate() && std::chrono::steady_clock::now() < end) {
            io.restart();
            io.run_for(1ms);
        }
        return predicate();
    }

    void start_server(behavior mode) {
        server = ::asio::co_spawn(io, serve(mode), ::asio::use_future);
    }

    void start_client(std::chrono::milliseconds timeout = 1s,
        std::optional<hash_digest> pin = std::nullopt) {
        client = std::make_shared<jd_connection>(io.get_executor(), "127.0.0.1",
            port, pin.value_or(authority.public_key()), timeout);
        run_client();
    }

    void run_client() {
        result = ::asio::co_spawn(io, client->run([this] {
            ready = true;
            return allow_ready && client->send(frame(setup()));
        }, [this](data_chunk bytes) {
            received.push_back(std::move(bytes));
            if (throw_frame) throw std::runtime_error("test response failure");
            return allow_frame;
        }), ::asio::use_future);
    }

    std::string finish() {
        REQUIRE(wait_until([&] { return result.wait_for(0s) == std::future_status::ready; }));
        return result.get();
    }

    ::asio::awaitable<void> serve(behavior mode) {
        auto [accept_error] = co_await acceptor.async_accept(peer,
            ::asio::as_tuple(::asio::use_awaitable));
        if (accept_error) co_return;
        if (mode == behavior::silent_handshake) {
            pause.expires_after(2s);
            (void)co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
            co_return;
        }
        std::array<uint8_t, noise_initiator::flight_size> first{};
        auto [first_error, first_size] = co_await ::asio::async_read(peer,
            ::asio::buffer(first), ::asio::as_tuple(::asio::use_awaitable));
        if (first_error) co_return;
        noise_responder responder(keypair(2), static_key, certificate);
        auto const second = responder.respond(first);
        REQUIRE(second);
        auto const flight = mode == behavior::short_handshake || mode == behavior::delayed_certificate
            ? byte_span(*second).first(100) : byte_span(*second);
        auto [write_error, written] = co_await ::asio::async_write(peer,
            ::asio::buffer(flight.data(), flight.size()), ::asio::as_tuple(::asio::use_awaitable));
        if (write_error || stopping) co_return;
        if (mode == behavior::short_handshake) {
            std::error_code ignored;
            peer.shutdown(::asio::ip::tcp::socket::shutdown_send, ignored);
            co_return;
        }
        if (mode == behavior::delayed_certificate) {
            pause.expires_after(2100ms);
            auto [pause_error] = co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (pause_error || stopping) co_return;
            auto [tail_error, tail_size] = co_await ::asio::async_write(peer,
                ::asio::buffer(second->data() + 100, second->size() - 100),
                ::asio::as_tuple(::asio::use_awaitable));
            if (tail_error || stopping) co_return;
        }

        noise_transport transport(*responder.keys(), false);
        std::array<uint8_t, 8192> encrypted{};
        while (sent.empty()) {
            auto [read_error, size] = co_await peer.async_read_some(
                ::asio::buffer(encrypted), ::asio::as_tuple(::asio::use_awaitable));
            if (read_error) co_return;
            auto frames = transport.feed(byte_span(encrypted).first(size));
            REQUIRE(frames);
            for (auto& bytes : *frames) sent.push_back(std::move(bytes));
        }
        if (mode == behavior::quiet) {
            pause.expires_after(2s);
            (void)co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
            co_return;
        }
        if (mode == behavior::drop) {
            std::error_code ignored;
            peer.shutdown(::asio::ip::tcp::socket::shutdown_send, ignored);
            co_return;
        }

        if (mode == behavior::coalesced_partial) {
            auto empty = to_data_chunk(message_header{1, 0x90, 0});
            auto large = to_data_chunk(message_header{1, 0x91, 65520});
            large.resize(message_header::size + 65520, 0x5a);
            data_chunk combined;
            for (auto const& plain : {empty, frame(setup_connection_success{2, 0}), large}) {
                auto const encoded = transport.encrypt_frame(plain);
                REQUIRE(encoded);
                combined.insert(combined.end(), encoded->begin(), encoded->end());
            }
            auto const unfinished = transport.encrypt_frame(frame(setup_connection_success{2, 0}));
            REQUIRE(unfinished);
            combined.push_back(unfinished->front());
            (void)co_await ::asio::async_write(peer, ::asio::buffer(combined),
                ::asio::as_tuple(::asio::use_awaitable));
            if (stopping) co_return;
            // A byte halfway through the assembly interval must not grant a
            // fresh interval. The remainder would complete only after the
            // original deadline, but before an incorrectly reset one.
            pause.expires_after(150ms);
            auto [pause_error] = co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (pause_error || stopping) co_return;
            auto [byte_error, byte_size] = co_await ::asio::async_write(peer,
                ::asio::buffer(unfinished->data() + 1, 1), ::asio::as_tuple(::asio::use_awaitable));
            if (byte_error || stopping) co_return;
            pause.expires_after(150ms);
            auto [rest_pause_error] = co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (rest_pause_error || stopping) co_return;
            auto [rest_error, rest_size] = co_await ::asio::async_write(peer,
                ::asio::buffer(unfinished->data() + 2, unfinished->size() - 2),
                ::asio::as_tuple(::asio::use_awaitable));
            if (rest_error || stopping) co_return;
            pause.expires_after(2s);
            (void)co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
            co_return;
        }

        auto plain = frame(setup_connection_success{2, 0});
        if (mode == behavior::oversized_header) {
            plain = to_data_chunk(message_header{0, 1, jd_connection::max_payload_size + 1});
            plain.resize(message_header::size + jd_connection::max_payload_size + 1);
        }
        auto bytes = transport.encrypt_frame(plain);
        REQUIRE(bytes);
        if (mode == behavior::split_header_tag) {
            (*bytes)[noise_transport::encrypted_header_size - 1] ^= 1;
            bytes->resize(noise_transport::encrypted_header_size);
        } else if (mode == behavior::split_payload_tag) {
            bytes->back() ^= 1;
        } else if (mode == behavior::oversized_header) {
            bytes->resize(noise_transport::encrypted_header_size);
        }
        if (mode == behavior::partial_frame) {
            bytes->resize(1);
        }
        auto const split = (mode == behavior::split_header_tag
            || mode == behavior::split_payload_tag || mode == behavior::split_response)
            ? bytes->size() - 1 : bytes->size();
        auto [prefix_error, prefix_size] = co_await ::asio::async_write(peer,
            ::asio::buffer(bytes->data(), split), ::asio::as_tuple(::asio::use_awaitable));
        if (prefix_error || stopping) co_return;
        if (split < bytes->size()) {
            pause.expires_after(10ms);
            (void)co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (stopping) co_return;
            auto [tail_error, tail_size] = co_await ::asio::async_write(peer,
                ::asio::buffer(bytes->data() + split, bytes->size() - split),
                ::asio::as_tuple(::asio::use_awaitable));
            if (tail_error) co_return;
        }
        if (stopping) co_return;
        // Keep the socket alive so a partial-frame test must hit its deadline.
        pause.expires_after(2s);
        (void)co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
    }
};

} // namespace

TEST_CASE("sv2 JD upstream authenticates the pinned authority and exchanges split frames", "[sv2 jd]") {
    upstream_fixture fixture;
    fixture.start_server(behavior::split_response);
    fixture.start_client();
    REQUIRE(fixture.wait_until([&] { return ! fixture.received.empty(); }));
    CHECK(fixture.ready);
    REQUIRE(fixture.sent.size() == 1);
    CHECK(fixture.sent.front() == frame(setup()));
    REQUIRE(fixture.received.size() == 1);
    CHECK(fixture.received.front() == frame(setup_connection_success{2, 0}));
    fixture.client->stop();
    CHECK(fixture.finish() == "upstream stopped");
}

TEST_CASE("sv2 JD upstream refuses a wrong authority or an out of window certificate", "[sv2 jd]") {
    for (int invalid = 0; invalid != 3; ++invalid) {
        CAPTURE(invalid);
        upstream_fixture fixture;
        auto const now = unix_time();
        if (invalid == 1) fixture.set_certificate(now - 120, now - 60);
        if (invalid == 2) fixture.set_certificate(now + 60, now + 120);
        fixture.start_server(behavior::exchange);
        fixture.start_client(1s, invalid == 0
            ? keypair(5).public_key() : fixture.authority.public_key());
        CHECK(fixture.finish() == "upstream Noise authentication failed");
        CHECK_FALSE(fixture.ready);
        CHECK(fixture.received.empty());
    }
}

TEST_CASE("sv2 JD upstream bounds a silent or incomplete handshake", "[sv2 jd]") {
    for (auto const mode : {behavior::silent_handshake, behavior::short_handshake}) {
        upstream_fixture fixture;
        fixture.start_server(mode);
        fixture.start_client(100ms);
        auto const reason = fixture.finish();
        CHECK(reason == (mode == behavior::silent_handshake
            ? "upstream connection or handshake timed out" : "upstream handshake ended early"));
        CHECK_FALSE(fixture.ready);
    }
}

TEST_CASE("sv2 JD upstream checks certificate validity when a slow handshake completes", "[sv2 jd]") {
    upstream_fixture fixture;
    auto const now = unix_time();
    fixture.set_certificate(now - 60, now + 1);
    fixture.start_server(behavior::delayed_certificate);
    fixture.start_client(3s);
    CHECK(fixture.finish() == "upstream Noise authentication failed");
    CHECK_FALSE(fixture.ready);
}

TEST_CASE("sv2 JD upstream refuses split authentication tags and oversized frames", "[sv2 jd]") {
    for (auto const mode : {behavior::split_header_tag, behavior::split_payload_tag,
            behavior::oversized_header}) {
        upstream_fixture fixture;
        fixture.start_server(mode);
        fixture.start_client();
        CHECK(fixture.finish() == "upstream encrypted frame refused");
        CHECK(fixture.ready);
        CHECK(fixture.received.empty());
    }
}

TEST_CASE("sv2 JD upstream bounds frame assembly without timing out a quiet connection", "[sv2 jd]") {
    SECTION("a started frame must finish within one deadline") {
        upstream_fixture fixture;
        fixture.start_server(behavior::partial_frame);
        fixture.start_client(100ms);
        CHECK(fixture.finish() == "upstream frame assembly timed out");
        CHECK(fixture.received.empty());
    }
    SECTION("idle time is left to the protocol request deadlines") {
        upstream_fixture fixture;
        fixture.start_server(behavior::quiet);
        fixture.start_client(100ms);
        REQUIRE(fixture.wait_until([&] { return ! fixture.sent.empty(); }));
        fixture.io.restart();
        fixture.io.run_for(250ms);
        CHECK(fixture.result.wait_for(0s) != std::future_status::ready);
        fixture.client->stop();
        CHECK(fixture.finish() == "upstream stopped");
    }
}

TEST_CASE("sv2 JD upstream accounts coalesced empty and multi chunk frames before a partial frame", "[sv2 jd]") {
    upstream_fixture fixture;
    fixture.start_server(behavior::coalesced_partial);
    fixture.start_client(250ms);
    CHECK(fixture.finish() == "upstream frame assembly timed out");
    REQUIRE(fixture.received.size() == 3);
    CHECK(fixture.received[0] == to_data_chunk(message_header{1, 0x90, 0}));
    CHECK(fixture.received[1] == frame(setup_connection_success{2, 0}));
    auto large = to_data_chunk(message_header{1, 0x91, 65520});
    large.resize(message_header::size + 65520, 0x5a);
    CHECK(fixture.received[2] == large);
}

TEST_CASE("sv2 JD upstream reports a dropped connection and protocol refusals", "[sv2 jd]") {
    SECTION("the peer drops an accepted connection") {
        upstream_fixture fixture;
        fixture.start_server(behavior::drop);
        fixture.start_client();
        CHECK(fixture.finish() == "upstream connection dropped");
        CHECK(fixture.ready);
    }
    SECTION("setup cannot queue its request") {
        upstream_fixture fixture;
        fixture.allow_ready = false;
        fixture.start_server(behavior::exchange);
        fixture.start_client();
        CHECK(fixture.finish() == "upstream setup refused");
    }
    SECTION("the protocol owner refuses an authenticated response") {
        upstream_fixture fixture;
        fixture.allow_frame = false;
        fixture.start_server(behavior::exchange);
        fixture.start_client();
        CHECK(fixture.finish() == "upstream protocol response refused");
        REQUIRE(fixture.received.size() == 1);
    }
}

TEST_CASE("sv2 JD upstream refuses malformed output and bounded queue overflow", "[sv2 jd]") {
    for (int invalid = 0; invalid != 4; ++invalid) {
        CAPTURE(invalid);
        upstream_fixture fixture;
        fixture.client = std::make_shared<jd_connection>(fixture.io.get_executor(),
            "127.0.0.1", fixture.port, fixture.authority.public_key());
        if (invalid == 0) {
            CHECK_FALSE(fixture.client->send({}));
        } else if (invalid == 1) {
            auto bytes = frame(setup());
            bytes.push_back(0);
            CHECK_FALSE(fixture.client->send(std::move(bytes)));
        } else if (invalid == 2) {
            // Outgoing frames may exceed the 1 MiB receive cap, up to the U24 frame.
            auto large = to_data_chunk(message_header{0, 1, jd_connection::max_payload_size + 1});
            large.resize(message_header::size + jd_connection::max_payload_size + 1);
            CHECK(fixture.client->send(std::move(large)));
            CHECK_FALSE(fixture.client->send(data_chunk(message_header::size
                + jd_connection::max_send_payload_size + 1)));
        } else {
            for (size_t i = 0; i < jd_connection::max_queued_frames; ++i) {
                CHECK(fixture.client->send(frame(setup())));
            }
            CHECK_FALSE(fixture.client->send(frame(setup())));
        }
        CHECK_FALSE(fixture.client->send(frame(setup())));
        fixture.run_client();
        CHECK(fixture.finish() == "upstream output queue refused a frame");
        CHECK_FALSE(fixture.ready);
    }
}

TEST_CASE("sv2 JD upstream stops before run and cannot run twice", "[sv2 jd]") {
    for (auto const timeout : {1s, 600s}) {
        upstream_fixture fixture;
        fixture.client = std::make_shared<jd_connection>(fixture.io.get_executor(),
            "127.0.0.1", fixture.port, fixture.authority.public_key(), timeout);
        fixture.client->stop();
        fixture.run_client();
        CHECK(fixture.finish() == "upstream stopped");
        fixture.run_client();
        CHECK(fixture.finish() == "upstream connection already started");
        CHECK_FALSE(fixture.ready);
    }
}

TEST_CASE("sv2 JD upstream reports connection refusal and joins after handler exceptions", "[sv2 jd]") {
    SECTION("an OS assigned listener has stopped") {
        upstream_fixture fixture;
        std::error_code error;
        fixture.acceptor.close(error);
        REQUIRE_FALSE(error);
        fixture.start_client();
        CHECK(fixture.finish() == "upstream connection failed");
        CHECK_FALSE(fixture.ready);
    }
    SECTION("a protocol handler throws while processing a frame") {
        upstream_fixture fixture;
        fixture.throw_frame = true;
        fixture.start_server(behavior::exchange);
        fixture.start_client();
        CHECK(fixture.finish() == "upstream exception: test response failure");
        CHECK_FALSE(fixture.client->send(frame(setup())));
    }
}

TEST_CASE("sv2 JD upstream rejects invalid transport settings before connecting", "[sv2 jd]") {
    for (int invalid = 0; invalid != 5; ++invalid) {
        CAPTURE(invalid);
        upstream_fixture fixture;
        fixture.client = std::make_shared<jd_connection>(fixture.io.get_executor(),
            invalid == 0 ? std::string{} : invalid == 1 ? std::string(256, 'a') : "127.0.0.1",
            invalid == 2 ? uint16_t{} : fixture.port, fixture.authority.public_key(),
            invalid == 3 ? 0ms : invalid == 4 ? 601s : 1s);
        fixture.run_client();
        CHECK(fixture.finish() == "invalid upstream connection settings");
        CHECK_FALSE(fixture.ready);
    }
}
