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

#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/network_address.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/network/sam.hpp>
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

// What stands in front of the pool on the fixture's listener. After a proxy or
// a bridge hands the stream over, the bytes that follow are the pool's own.
enum class front { none, socks5, socks5_refusing, socks5_silent, sam };

// An I2P destination as a SAM bridge writes it: 391 bytes whose certificate
// names the key types, in I2P's base64 alphabet.
data_chunk i2p_destination(uint8_t identity) {
    data_chunk bytes(391, 0);
    bytes[0] = identity;
    bytes[384] = 5;
    bytes[386] = 4;
    bytes[388] = 7;
    return bytes;
}

std::string i2p_base64(data_chunk const& bytes) {
    auto text = encode_base64(bytes);
    for (auto& c : text) {
        if (c == '+') c = '-';
        else if (c == '/') c = '~';
    }
    return text;
}

// A Tor v3 onion address of a made-up service key.
std::string onion_name(uint8_t identity) {
    hash_digest key{};
    key.front() = identity;
    return infrastructure::config::authority(infrastructure::message::network_address(0, 0,
        infrastructure::message::address_network::torv3, key, 3336)).to_hostname();
}

std::string i2p_name(data_chunk const& destination) {
    return infrastructure::config::authority(infrastructure::message::network_address(0, 0,
        infrastructure::message::address_network::i2p, sha256_hash(destination), 0)).to_hostname();
}

// The acceptor reports its bound port after listen(). Every asynchronous wait
// is pumped only to a deadline; fixture cleanup also has a fixed deadline.
struct upstream_fixture {
    ::asio::io_context io;
    ::asio::ip::tcp::acceptor acceptor{io};
    ::asio::ip::tcp::socket peer{io};
    // A SAM bridge's control connection, which stays open under its session.
    ::asio::ip::tcp::socket control{io};
    ::asio::steady_timer pause{io};
    front via = front::none;
    // What the proxy or the bridge was asked to reach.
    std::string asked_host;
    uint16_t asked_port = 0;
    // The user name the proxy was given, when credentials were offered.
    std::string proxy_user;
    bool transient_session = false;
    data_chunk const pool_destination = i2p_destination(1);
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
        control.close(ignored);
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

    // The client asks for `host:port` along `route`. With a proxy or a bridge in
    // front, that address is only a name: nothing listens there.
    void start_client_through(jd_route route, std::string host, uint16_t target,
        std::chrono::milliseconds timeout = 3s) {
        client = std::make_shared<jd_connection>(io.get_executor(), std::move(host),
            target, authority.public_key(), timeout, std::move(route));
        run_client();
    }

    [[nodiscard]] std::string listener() const {
        return "127.0.0.1:" + std::to_string(port);
    }

    ::asio::awaitable<std::optional<std::string>> line(::asio::ip::tcp::socket& socket) {
        std::string text;
        while (text.size() < 4096) {
            char c = 0;
            auto [error, size] = co_await ::asio::async_read(socket, ::asio::buffer(&c, 1),
                ::asio::as_tuple(::asio::use_awaitable));
            if (error) co_return std::nullopt;
            if (c == '\n') co_return text;
            text.push_back(c);
        }
        co_return std::nullopt;
    }

    ::asio::awaitable<bool> say(::asio::ip::tcp::socket& socket, std::string text) {
        auto [error, size] = co_await ::asio::async_write(socket, ::asio::buffer(text),
            ::asio::as_tuple(::asio::use_awaitable));
        co_return ! error;
    }

    ::asio::awaitable<bool> take(::asio::ip::tcp::socket& socket, uint8_t* bytes, size_t count) {
        auto [error, size] = co_await ::asio::async_read(socket, ::asio::buffer(bytes, count),
            ::asio::as_tuple(::asio::use_awaitable));
        co_return ! error;
    }

    ::asio::awaitable<bool> sam_hello(::asio::ip::tcp::socket& socket) {
        auto const hello = co_await line(socket);
        co_return hello && *hello == "HELLO VERSION MIN=3.1 MAX=3.1"
            && co_await say(socket, "HELLO REPLY RESULT=OK VERSION=3.1\n");
    }

    // The session a bridge is asked for first. The client's must be one made
    // for it and kept nowhere.
    ::asio::awaitable<bool> sam_session_created() {
        if ( ! co_await sam_hello(control)) co_return false;
        auto const create = co_await line(control);
        if ( ! create || ! create->starts_with("SESSION CREATE STYLE=STREAM ID=")) co_return false;
        transient_session = create->find(" DESTINATION=TRANSIENT ") != std::string::npos;
        auto secret = i2p_destination(9);
        secret.insert(secret.end(), 288, 0x4a);
        co_return co_await say(control, "SESSION STATUS RESULT=OK DESTINATION=" + i2p_base64(secret) + "\n");
    }

    // The proxy's or the bridge's part on the accepted stream. True once the
    // stream is the pool's.
    ::asio::awaitable<bool> hand_over() {
        if (via == front::sam) {
            auto const name = i2p_name(pool_destination);
            if ( ! co_await sam_hello(peer)) co_return false;
            auto const lookup = co_await line(peer);
            if ( ! lookup) co_return false;
            if (*lookup != "NAMING LOOKUP NAME=" + name) {
                // What a bridge answers for a name it cannot find.
                (void)co_await say(peer, "NAMING REPLY RESULT=KEY_NOT_FOUND NAME="
                    + lookup->substr(lookup->rfind('=') + 1) + "\n");
                co_return false;
            }
            if ( ! co_await say(peer, "NAMING REPLY RESULT=OK NAME=" + name + " VALUE="
                    + i2p_base64(pool_destination) + "\n")) co_return false;
            auto const connect = co_await line(peer);
            if ( ! connect || ! connect->starts_with("STREAM CONNECT ID=")) co_return false;
            asked_host = name;
            co_return co_await say(peer, "STREAM STATUS RESULT=OK\n");
        }
        if (via == front::socks5_silent) {
            // Accepts and never answers, as a proxy whose circuit never opens.
            pause.expires_after(2s);
            (void)co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
            co_return false;
        }
        // SOCKS5 (RFC 1928): the greeting with its methods, then one CONNECT.
        std::array<uint8_t, 2> greeting{};
        if ( ! co_await take(peer, greeting.data(), greeting.size()) || greeting[0] != 0x05) co_return false;
        std::array<uint8_t, 255> methods{};
        if ( ! co_await take(peer, methods.data(), greeting[1])) co_return false;
        // Username and password (RFC 1929) when the client offers them, as Tor
        // answers; otherwise no authentication.
        auto const credentials = greeting[1] == 2 && methods[1] == 0x02;
        if ( ! co_await say(peer, std::string{char(0x05), char(credentials ? 0x02 : 0x00)})) co_return false;
        if (credentials) {
            std::array<uint8_t, 2> head{};
            if ( ! co_await take(peer, head.data(), head.size()) || head[0] != 0x01) co_return false;
            proxy_user.assign(head[1], char(0));
            if ( ! co_await take(peer, reinterpret_cast<uint8_t*>(proxy_user.data()), head[1])) co_return false;
            uint8_t secret_size = 0;
            if ( ! co_await take(peer, &secret_size, 1)) co_return false;
            std::array<uint8_t, 255> secret{};
            if ( ! co_await take(peer, secret.data(), secret_size)) co_return false;
            if ( ! co_await say(peer, std::string{char(0x01), char(0x00)})) co_return false;
        }
        std::array<uint8_t, 4> request{};
        if ( ! co_await take(peer, request.data(), request.size())) co_return false;
        if (request[0] != 0x05 || request[1] != 0x01 || request[2] != 0x00) co_return false;
        if (request[3] == 0x03) {
            uint8_t length = 0;
            if ( ! co_await take(peer, &length, 1)) co_return false;
            std::string name(length, '\0');
            if ( ! co_await take(peer, reinterpret_cast<uint8_t*>(name.data()), length)) co_return false;
            asked_host = std::move(name);
        } else if (request[3] == 0x01) {
            std::array<uint8_t, 4> address{};
            if ( ! co_await take(peer, address.data(), address.size())) co_return false;
            asked_host = ::asio::ip::address_v4(address).to_string();
        } else {
            co_return false;
        }
        std::array<uint8_t, 2> target{};
        if ( ! co_await take(peer, target.data(), target.size())) co_return false;
        asked_port = uint16_t((uint16_t{target[0]} << 8) | target[1]);
        // Reply code 5 is "connection refused"; the bound address is unused.
        std::string reply{char(0x05), char(via == front::socks5_refusing ? 0x05 : 0x00), char(0x00),
            char(0x01), char(0), char(0), char(0), char(0), char(0), char(0)};
        if ( ! co_await say(peer, std::move(reply))) co_return false;
        co_return via == front::socks5;
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
        if (via == front::sam) {
            auto [control_error] = co_await acceptor.async_accept(control,
                ::asio::as_tuple(::asio::use_awaitable));
            if (control_error || ! co_await sam_session_created()) co_return;
        }
        auto [accept_error] = co_await acceptor.async_accept(peer,
            ::asio::as_tuple(::asio::use_awaitable));
        if (accept_error) co_return;
        if (via != front::none && ! co_await hand_over()) co_return;
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

// -----------------------------------------------------------------------------
// Routes: a SOCKS5 proxy, and I2P through a SAM bridge
// -----------------------------------------------------------------------------

TEST_CASE("sv2 JD upstream reaches the pool through a SOCKS5 proxy and leaves its name to the proxy",
          "[sv2 jd][socks5]") {
    upstream_fixture fixture;
    fixture.via = front::socks5;
    fixture.start_server(behavior::split_response);
    // No resolver knows this name (RFC 2606), so only the proxy can be asked
    // for it: a local lookup would end the connection before it began.
    fixture.start_client_through(jd_route{fixture.listener(), false, nullptr}, "pool.invalid", 3336);
    REQUIRE(fixture.wait_until([&] { return ! fixture.received.empty(); }));
    CHECK(fixture.ready);
    CHECK(fixture.asked_host == "pool.invalid");
    CHECK(fixture.asked_port == 3336);
    CHECK(fixture.proxy_user.empty());
    REQUIRE(fixture.sent.size() == 1);
    CHECK(fixture.sent.front() == frame(setup()));
    CHECK(fixture.received.front() == frame(setup_connection_success{2, 0}));
    fixture.client->stop();
    CHECK(fixture.finish() == "upstream stopped");
}

TEST_CASE("sv2 JD upstream offers the proxy fresh credentials when the node randomizes them",
          "[sv2 jd][socks5]") {
    upstream_fixture fixture;
    fixture.via = front::socks5;
    fixture.start_server(behavior::split_response);
    fixture.start_client_through(jd_route{fixture.listener(), true, nullptr}, "pool.invalid", 3336);
    REQUIRE(fixture.wait_until([&] { return ! fixture.received.empty(); }));
    CHECK(fixture.asked_host == "pool.invalid");
    // Tor puts connections with different credentials on different circuits.
    CHECK(fixture.proxy_user.size() == 64);
    fixture.client->stop();
    CHECK(fixture.finish() == "upstream stopped");
}

TEST_CASE("sv2 JD upstream never goes around a proxy that refuses", "[sv2 jd][socks5]") {
    upstream_fixture fixture;
    fixture.via = front::socks5_refusing;
    fixture.start_server(behavior::exchange);
    // The pool's own address is open and would answer a direct connection.
    ::asio::ip::tcp::acceptor direct(fixture.io, {::asio::ip::address_v4::loopback(), 0});
    auto const pool_port = direct.local_endpoint().port();
    fixture.start_client_through(jd_route{fixture.listener(), false, nullptr}, "127.0.0.1", pool_port);
    CHECK(fixture.finish() == "upstream connection through the proxy failed");
    CHECK(fixture.asked_host == "127.0.0.1");
    CHECK(fixture.asked_port == pool_port);
    CHECK_FALSE(fixture.ready);
    std::error_code error;
    direct.non_blocking(true, error);
    REQUIRE_FALSE(error);
    ::asio::ip::tcp::socket nobody(fixture.io);
    direct.accept(nobody, error);
    CHECK(error == ::asio::error::would_block);
}

TEST_CASE("sv2 JD upstream reports a proxy that is not there", "[sv2 jd][socks5]") {
    upstream_fixture fixture;
    // A port that was open a moment ago and is closed now.
    uint16_t closed = 0;
    {
        ::asio::ip::tcp::acceptor gone(fixture.io, {::asio::ip::address_v4::loopback(), 0});
        closed = gone.local_endpoint().port();
    }
    fixture.start_client_through(jd_route{"127.0.0.1:" + std::to_string(closed), false, nullptr},
        "pool.invalid", 3336);
    CHECK(fixture.finish() == "upstream connection through the proxy failed");
    CHECK_FALSE(fixture.ready);
}

TEST_CASE("sv2 JD upstream refuses an onion or I2P address it has no route for", "[sv2 jd][socks5][i2p]") {
    auto const i2p = i2p_name(i2p_destination(1));
    struct expectation {
        std::string host;
        uint16_t port;
        char const* reason;
    };
    for (auto const& [host, target, reason] : {
            expectation{onion_name(1), 3336, "an .onion upstream needs net.proxy"},
            expectation{"POOL.ONION.", 3336, "an .onion upstream needs net.proxy"},
            expectation{i2p, 0, "an .i2p upstream needs net.i2psam"},
            expectation{"Pool.B32.I2P", 0, "an .i2p upstream needs net.i2psam"},
            // An I2P stream has no port, and nothing else is reached without one.
            expectation{i2p, 3336, "invalid upstream connection settings"},
            expectation{onion_name(1), 0, "invalid upstream connection settings"}}) {
        CAPTURE(host, target);
        upstream_fixture fixture;
        fixture.start_client_through(jd_route{}, host, target);
        CHECK(fixture.finish() == reason);
        CHECK_FALSE(fixture.ready);
    }
}

TEST_CASE("sv2 JD upstream leaves an onion name to the proxy", "[sv2 jd][socks5]") {
    upstream_fixture fixture;
    fixture.via = front::socks5;
    fixture.start_server(behavior::split_response);
    auto const pool = onion_name(7);
    fixture.start_client_through(jd_route{fixture.listener(), true, nullptr}, pool, 3336);
    REQUIRE(fixture.wait_until([&] { return ! fixture.received.empty(); }));
    CHECK(fixture.asked_host == pool);
    CHECK(fixture.asked_port == 3336);
    fixture.client->stop();
    CHECK(fixture.finish() == "upstream stopped");
}

TEST_CASE("sv2 JD upstream gives a proxy its dial allowance and no longer", "[sv2 jd][socks5]") {
    upstream_fixture fixture;
    fixture.via = front::socks5_silent;
    fixture.start_server(behavior::exchange);
    jd_route route{fixture.listener(), false, nullptr};
    route.proxy_dial_timeout = 300ms;
    auto const begin = std::chrono::steady_clock::now();
    // The connection's own deadline is shorter than the allowance: the proxy
    // is not cut off by it.
    fixture.start_client_through(route, "pool.invalid", 3336, 100ms);
    CHECK(fixture.finish() == "upstream connection through the proxy failed");
    auto const took = std::chrono::steady_clock::now() - begin;
    CHECK(took >= 300ms);
    CHECK(took < 2s);
    CHECK_FALSE(fixture.ready);
}

TEST_CASE("sv2 JD upstream waits for an I2P session only as long as its dial allowance", "[sv2 jd][i2p]") {
    upstream_fixture fixture;
    // The bridge is not there: the session never becomes ready.
    uint16_t closed = 0;
    {
        ::asio::ip::tcp::acceptor gone(fixture.io, {::asio::ip::address_v4::loopback(), 0});
        closed = gone.local_endpoint().port();
    }
    auto session = std::make_shared<network::sam_session>(fixture.io.get_executor(),
        "127.0.0.1:" + std::to_string(closed), kth::path{}, false, 5s);
    auto running = ::asio::co_spawn(fixture.io, [session]() -> ::asio::awaitable<void> {
        co_await session->run();
    }, ::asio::use_future);
    jd_route route{{}, true, session.get()};
    route.i2p_dial_timeout = 300ms;
    fixture.start_client_through(route, i2p_name(i2p_destination(1)), 0, 200ms);
    CHECK(fixture.finish() == "upstream connection or handshake timed out");
    CHECK_FALSE(fixture.ready);
    session->stop();
    REQUIRE(fixture.wait_until([&] { return running.wait_for(0s) == std::future_status::ready; }));
}

TEST_CASE("sv2 JD upstream reaches an I2P pool through a session made for it", "[sv2 jd][i2p]") {
    upstream_fixture fixture;
    fixture.via = front::sam;
    fixture.start_server(behavior::split_response);
    // The session is started together with the client, as the node does: the
    // connection waits for it inside its own deadline.
    auto session = std::make_shared<network::sam_session>(fixture.io.get_executor(), fixture.listener(),
        kth::path{}, false, 5s);
    auto running = ::asio::co_spawn(fixture.io, [session]() -> ::asio::awaitable<void> {
        co_await session->run();
    }, ::asio::use_future);
    auto const pool = i2p_name(fixture.pool_destination);
    fixture.start_client_through(jd_route{{}, true, session.get()}, pool, 0, 5s);
    REQUIRE(fixture.wait_until([&] { return ! fixture.received.empty(); }, 5s));
    CHECK(fixture.ready);
    CHECK(fixture.transient_session);
    CHECK(fixture.asked_host == pool);
    CHECK(fixture.received.front() == frame(setup_connection_success{2, 0}));
    fixture.client->stop();
    CHECK(fixture.finish() == "upstream stopped");
    session->stop();
    std::error_code ignored;
    fixture.control.close(ignored);
    REQUIRE(fixture.wait_until([&] { return running.wait_for(0s) == std::future_status::ready; }));
}

TEST_CASE("sv2 JD upstream reports an I2P pool the bridge cannot reach", "[sv2 jd][i2p]") {
    upstream_fixture fixture;
    fixture.via = front::sam;
    fixture.start_server(behavior::exchange);
    auto session = std::make_shared<network::sam_session>(fixture.io.get_executor(), fixture.listener(),
        kth::path{}, false, 5s);
    auto running = ::asio::co_spawn(fixture.io, [session]() -> ::asio::awaitable<void> {
        co_await session->run();
    }, ::asio::use_future);
    // Another destination than the one the bridge knows: its lookup gets no
    // answer the client accepts.
    fixture.start_client_through(jd_route{{}, true, session.get()}, i2p_name(i2p_destination(2)), 0, 5s);
    CHECK(fixture.finish() == "upstream connection through I2P failed");
    CHECK_FALSE(fixture.ready);
    session->stop();
    std::error_code ignored;
    fixture.control.close(ignored);
    fixture.peer.close(ignored);
    REQUIRE(fixture.wait_until([&] { return running.wait_for(0s) == std::future_status::ready; }));
}
