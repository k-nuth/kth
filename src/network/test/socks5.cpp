// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <test_helpers.hpp>

#include <kth/network/peer_session.hpp>
#include <kth/network/peer_manager.hpp>
#include <kth/network/socks5.hpp>

#include <asio/as_tuple.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

using namespace kth;
using namespace kth::network;
using namespace std::chrono_literals;

namespace {

template <typename T>
struct socks5_completion {
    std::optional<std::expected<T, code>> result;
    std::exception_ptr exception;
    bool done{false};
};

template <typename T>
::asio::awaitable<void> collect_socks5_result(
    awaitable_expected<T> operation, std::shared_ptr<socks5_completion<T>> state) {
    state->result.emplace(co_await std::move(operation));
}

struct socks5_reply {
    data_chunk method{5, 0};
    data_chunk auth{1, 0};
    data_chunk connect{5, 0, 0, 1, 127, 0, 0, 1, 0, 0};
    bool split{false};
    int stop_after{3};
    int stall_at{0};
};

struct fake_socks5 {
    ::asio::cancellation_signal cancellation;
    ::asio::io_context ctx;
    ::asio::ip::tcp::acceptor listener{ctx, {::asio::ip::address_v4::loopback(), 0}};
    ::asio::ip::tcp::socket socket{ctx};
    data_chunk greeting;
    data_chunk credentials;
    data_chunk request;
    bool accepted{false};
    bool server_done{false};
    std::exception_ptr server_exception;

    std::string proxy() const {
        return "127.0.0.1:" + std::to_string(listener.local_endpoint().port());
    }

    ::asio::awaitable<bool> read(data_chunk& bytes, size_t size) {
        auto const offset = bytes.size();
        bytes.resize(offset + size);
        auto [ec, count] = co_await ::asio::async_read(socket,
            ::asio::buffer(bytes.data() + offset, size), ::asio::as_tuple(::asio::use_awaitable));
        co_return ! ec && count == size;
    }

    ::asio::awaitable<bool> write(data_chunk const& bytes, bool split) {
        if ( ! split) {
            auto [ec, count] = co_await ::asio::async_write(socket,
                ::asio::buffer(bytes), ::asio::as_tuple(::asio::use_awaitable));
            co_return ! ec && count == bytes.size();
        }
        for (auto const& byte : bytes) {
            auto [ec, count] = co_await ::asio::async_write(socket,
                ::asio::buffer(&byte, 1), ::asio::as_tuple(::asio::use_awaitable));
            if (ec || count != 1) co_return false;
            ::asio::steady_timer pause(ctx);
            pause.expires_after(1ms);
            auto [timer_ec] = co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (timer_ec) co_return false;
        }
        co_return true;
    }

    void finish_reply() {
        std::error_code ignored;
        // A truncated reply must become EOF rather than an unbounded read.
        socket.shutdown(::asio::ip::tcp::socket::shutdown_send, ignored);
    }

    ::asio::awaitable<void> serve(socks5_reply reply) {
        auto [ec] = co_await listener.async_accept(socket, ::asio::as_tuple(::asio::use_awaitable));
        if (ec) co_return;
        accepted = true;
        if ( ! co_await read(greeting, 2)) co_return;
        if ( ! co_await read(greeting, greeting[1])) co_return;
        if (reply.stall_at == 1) co_return;
        if ( ! co_await write(reply.method, reply.split)) co_return;
        if (reply.stop_after == 1) {
            finish_reply();
            co_return;
        }
        if (reply.method.size() == 2 && reply.method[1] == 2) {
            if ( ! co_await read(credentials, 2)) co_return;
            if ( ! co_await read(credentials, credentials[1])) co_return;
            if ( ! co_await read(credentials, 1)) co_return;
            auto const password_size = credentials.back();
            if ( ! co_await read(credentials, password_size)) co_return;
            if (reply.stall_at == 2) co_return;
            if ( ! co_await write(reply.auth, reply.split)) co_return;
            if (reply.stop_after == 2) {
                finish_reply();
                co_return;
            }
        }
        if ( ! co_await read(request, 4)) co_return;
        size_t address_size = 0;
        switch (request[3]) {
            case 1: address_size = 4; break;
            case 4: address_size = 16; break;
            case 3:
                if ( ! co_await read(request, 1)) co_return;
                address_size = request.back();
                break;
            default: co_return;
        }
        if ( ! co_await read(request, address_size + 2)) co_return;
        if (reply.stall_at == 3) co_return;
        if ( ! co_await write(reply.connect, reply.split)) co_return;
        finish_reply();
    }

    void start(socks5_reply reply = {}) {
        std::error_code ignored;
        socket.close(ignored);
        socket = ::asio::ip::tcp::socket(ctx);
        greeting.clear();
        credentials.clear();
        request.clear();
        accepted = false;
        server_done = false;
        server_exception = nullptr;
        ctx.restart();
        ::asio::co_spawn(ctx, serve(std::move(reply)), [this](std::exception_ptr exception) {
            server_exception = exception;
            server_done = true;
        });
    }

    template <typename T>
    std::expected<T, code> run(awaitable_expected<T> operation) {
        auto state = std::make_shared<socks5_completion<T>>();
        ::asio::co_spawn(ctx, collect_socks5_result(std::move(operation), state),
            ::asio::bind_cancellation_slot(cancellation.slot(), [state](std::exception_ptr exception) {
                state->exception = exception;
                state->done = true;
            }));
        auto const deadline = std::chrono::steady_clock::now() + 2s;
        while ( ! state->done && std::chrono::steady_clock::now() < deadline) {
            ctx.restart();
            ctx.run_for(10ms);
        }
        auto const finished = state->done;
        std::error_code ignored;
        listener.cancel(ignored);
        socket.close(ignored);
        ctx.restart();
        ctx.run_for(50ms);
        REQUIRE(finished);
        REQUIRE( ! state->exception);
        REQUIRE( ! server_exception);
        REQUIRE(state->result.has_value());
        return std::move(*state->result);
    }

    std::expected<::asio::ip::tcp::socket, code> connect(
        std::string const& host, bool randomize = false,
        std::chrono::steady_clock::duration timeout = 1s, uint16_t port = 8333) {
        auto const proxy_address = proxy();
        return run(async_socks5_connect(ctx.get_executor(), proxy_address, host, port, randomize, timeout));
    }
};

} // namespace

TEST_CASE("SOCKS5 CONNECT preserves destination address and port", "[socks5]") {
    fake_socks5 proxy;
    std::string host;
    data_chunk expected{5, 1, 0};
    SECTION("domain name") {
        host = "peer-without-local-dns.invalid";
        expected.push_back(3);
        expected.push_back(static_cast<uint8_t>(host.size()));
        expected.insert(expected.end(), host.begin(), host.end());
    }
    SECTION("IPv4") {
        host = "192.0.2.17";
        expected.insert(expected.end(), {1, 192, 0, 2, 17});
    }
    SECTION("IPv6") {
        host = "2001:db8::17";
        expected.insert(expected.end(), {4, 0x20, 1, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x17});
    }
    SECTION("bracketed IPv6") {
        host = "[2001:db8::17]";
        expected.insert(expected.end(), {4, 0x20, 1, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x17});
    }
    SECTION("maximum length domain") {
        host = std::string(251, 'a') + ".com";
        expected.insert(expected.end(), {3, 255});
        expected.insert(expected.end(), host.begin(), host.end());
    }
    expected.insert(expected.end(), {0x20, 0x8d});
    proxy.start();
    auto result = proxy.connect(host);
    REQUIRE(result.has_value());
    REQUIRE(result->remote_endpoint().address().is_v4());
    REQUIRE(proxy.accepted);
    REQUIRE(proxy.greeting == data_chunk{5, 1, 0});
    REQUIRE(proxy.request == expected);
}

TEST_CASE("SOCKS5 randomization sends fresh RFC1929 credentials when selected", "[socks5]") {
    fake_socks5 proxy;
    socks5_reply reply;
    reply.method = {5, 2};
    reply.split = true;
    proxy.start(reply);
    auto first = proxy.connect("first.invalid", true);
    REQUIRE(first.has_value());
    REQUIRE(proxy.greeting == data_chunk{5, 2, 0, 2});
    auto const credentials = proxy.credentials;
    REQUIRE(credentials.size() >= 5);
    REQUIRE(credentials[0] == 1);
    auto const username_size = size_t(credentials[1]);
    REQUIRE(username_size > 0);
    REQUIRE(credentials.size() > username_size + 2);
    auto const password_size = size_t(credentials[username_size + 2]);
    REQUIRE(password_size > 0);
    REQUIRE(credentials.size() == username_size + password_size + 3);

    proxy.start(reply);
    auto second = proxy.connect("second.invalid", true);
    REQUIRE(second.has_value());
    REQUIRE(proxy.greeting == data_chunk{5, 2, 0, 2});
    REQUIRE(proxy.credentials != credentials);
}

TEST_CASE("SOCKS5 randomization accepts no authentication when selected", "[socks5]") {
    fake_socks5 proxy;
    socks5_reply reply;
    reply.split = true;
    proxy.start(reply);
    auto result = proxy.connect("peer.invalid", true);
    REQUIRE(result.has_value());
    REQUIRE(proxy.greeting == data_chunk{5, 2, 0, 2});
    REQUIRE(proxy.credentials.empty());
    REQUIRE(proxy.request == data_chunk{5, 1, 0, 3, 12,
        'p', 'e', 'e', 'r', '.', 'i', 'n', 'v', 'a', 'l', 'i', 'd', 0x20, 0x8d});
    REQUIRE(proxy.server_done);
}

TEST_CASE("SOCKS5 consumes fragmented replies for every bound address type", "[socks5]") {
    fake_socks5 proxy;
    socks5_reply reply;
    reply.split = true;
    SECTION("IPv4") {}
    SECTION("IPv6") {
        reply.connect = {5, 0, 0, 4};
        reply.connect.resize(22, 0);
    }
    SECTION("domain") {
        reply.connect = {5, 0, 0, 3, 3, 'b', 'n', 'd', 0, 0};
    }
    proxy.start(reply);
    auto result = proxy.connect("peer.invalid");
    REQUIRE(result.has_value());
    REQUIRE(proxy.server_done);
}

TEST_CASE("SOCKS5 refuses malformed and unoffered negotiation", "[socks5]") {
    fake_socks5 proxy;
    socks5_reply reply;
    reply.stop_after = 1;
    reply.split = true;
    bool randomize = false;
    SECTION("wrong SOCKS version") { reply.method = {4, 0}; }
    SECTION("no acceptable method") { reply.method = {5, 255}; }
    SECTION("unoffered authentication") { reply.method = {5, 2}; }
    SECTION("randomization does not accept a wrong SOCKS version") {
        randomize = true;
        reply.method = {4, 0};
    }
    SECTION("randomization does not accept refusal") {
        randomize = true;
        reply.method = {5, 255};
    }
    SECTION("randomization does not accept an unoffered method") {
        randomize = true;
        reply.method = {5, 1};
    }
    proxy.start(reply);
    auto result = proxy.connect("peer.invalid", randomize);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::operation_failed);
    REQUIRE(proxy.credentials.empty());
    REQUIRE(proxy.request.empty());
}

TEST_CASE("SOCKS5 refuses failed or malformed username password authentication", "[socks5]") {
    fake_socks5 proxy;
    socks5_reply reply;
    reply.method = {5, 2};
    reply.stop_after = 2;
    reply.split = true;
    SECTION("wrong authentication version") { reply.auth = {2, 0}; }
    SECTION("credentials refused") { reply.auth = {1, 1}; }
    proxy.start(reply);
    auto result = proxy.connect("peer.invalid", true);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::operation_failed);
    REQUIRE( ! proxy.credentials.empty());
    REQUIRE(proxy.request.empty());
}

TEST_CASE("SOCKS5 refuses unsuccessful or malformed CONNECT replies across reads", "[socks5]") {
    fake_socks5 proxy;
    socks5_reply reply;
    reply.split = true;
    SECTION("proxy refusal") { reply.connect[1] = 5; }
    SECTION("wrong reply version") { reply.connect[0] = 4; }
    SECTION("nonzero reserved byte") { reply.connect[2] = 1; }
    SECTION("unknown address type") { reply.connect[3] = 2; }
    SECTION("empty bound domain") { reply.connect = {5, 0, 0, 3, 0}; }
    proxy.start(reply);
    auto result = proxy.connect("peer.invalid");
    REQUIRE( ! result);
    REQUIRE(result.error() == error::operation_failed);
    REQUIRE( ! proxy.request.empty());
}

TEST_CASE("SOCKS5 truncated replies fail instead of waiting indefinitely", "[socks5]") {
    fake_socks5 proxy;
    socks5_reply reply;
    bool randomize = false;
    SECTION("method") { reply.method = {5}; reply.stop_after = 1; }
    SECTION("authentication") {
        randomize = true;
        reply.method = {5, 2};
        reply.auth = {1};
        reply.stop_after = 2;
    }
    SECTION("CONNECT header") { reply.connect = {5, 0}; }
    SECTION("IPv4 bound address") { reply.connect = {5, 0, 0, 1, 127}; }
    SECTION("IPv6 bound address") { reply.connect = {5, 0, 0, 4, 0, 0}; }
    SECTION("bound domain length") { reply.connect = {5, 0, 0, 3}; }
    SECTION("bound domain body") { reply.connect = {5, 0, 0, 3, 3, 'b'}; }
    proxy.start(reply);
    auto result = proxy.connect("peer.invalid", randomize);
    REQUIRE( ! result);
    REQUIRE(result.error() != error::channel_timeout);
}

TEST_CASE("SOCKS5 bounds every negotiation phase by the connection deadline", "[socks5]") {
    fake_socks5 proxy;
    socks5_reply reply;
    bool randomize = false;
    SECTION("method selection") { reply.stall_at = 1; }
    SECTION("authentication") { reply.stall_at = 2; randomize = true; reply.method = {5, 2}; }
    SECTION("CONNECT reply") { reply.stall_at = 3; }
    proxy.start(reply);
    auto result = proxy.connect("peer.invalid", randomize, 100ms);
    REQUIRE(proxy.accepted);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::channel_timeout);
}

TEST_CASE("SOCKS5 cancellation ends a stalled exchange without a session", "[socks5]") {
    fake_socks5 proxy;
    socks5_reply reply;
    reply.stall_at = 1;
    proxy.start(reply);
    ::asio::steady_timer cancel(proxy.ctx);
    cancel.expires_after(100ms);
    cancel.async_wait([&](std::error_code ec) {
        if ( ! ec) proxy.cancellation.emit(::asio::cancellation_type::terminal);
    });
    auto result = proxy.connect("peer.invalid", false, 10s);
    REQUIRE(proxy.accepted);
    REQUIRE( ! result);
}

TEST_CASE("SOCKS5 proxy TCP refusal fails without negotiation", "[socks5]") {
    fake_socks5 proxy;
    // Reserve an ephemeral TCP port without listening on it.
    ::asio::ip::tcp::socket unavailable(proxy.ctx, {::asio::ip::address_v4::loopback(), 0});
    auto const address = "127.0.0.1:" + std::to_string(unavailable.local_endpoint().port());
    std::string const hostname = "peer.invalid";
    auto result = proxy.run(async_socks5_connect(proxy.ctx.get_executor(), address, hostname, 8333, false, 1s));
    REQUIRE( ! result);
    REQUIRE(result.error() != error::channel_timeout);
    REQUIRE( ! proxy.accepted);
}

TEST_CASE("SOCKS5 validates destinations and numeric proxy endpoints before connecting", "[socks5]") {
    fake_socks5 proxy;
    auto address = proxy.proxy();
    std::string host = "peer.invalid";
    uint16_t port = 8333;
    SECTION("empty proxy") { address.clear(); }
    SECTION("missing proxy port") { address = "127.0.0.1"; }
    SECTION("proxy hostname") { address = "localhost:9050"; }
    SECTION("zero proxy port") { address = "127.0.0.1:0"; }
    SECTION("nondecimal proxy port") { address = "127.0.0.1:abc"; }
    SECTION("out of range proxy port") { address = "127.0.0.1:65536"; }
    SECTION("unbracketed IPv6 proxy") { address = "::1:9050"; }
    SECTION("empty destination") { host.clear(); }
    SECTION("long destination") { host = std::string(256, 'a'); }
    SECTION("embedded NUL destination") { host = std::string("a\0b", 3); }
    SECTION("scoped IPv6 destination") { host = "fe80::1%1"; }
    SECTION("zero destination port") { port = 0; }
    auto result = proxy.run(async_socks5_connect(proxy.ctx.get_executor(), address, host, port, false, 1s));
    REQUIRE( ! result);
    REQUIRE(result.error() == error::illegal_value);
    REQUIRE( ! proxy.accepted);
}

TEST_CASE("configured proxy carries an unresolvable peer hostname", "[socks5][peer_session]") {
    fake_socks5 proxy;
    proxy.start();
    network::settings settings(domain::config::network::regtest);
    settings.proxy = proxy.proxy();
    settings.proxy_randomize = false;
    std::string const hostname = "peer-without-local-dns.invalid";
    auto result = proxy.run(network::async_connect(proxy.ctx.get_executor(), hostname, 18444, settings, 1s));
    REQUIRE(result.has_value());
    REQUIRE(proxy.request.size() == hostname.size() + 7);
    REQUIRE(proxy.request[3] == 3);
    REQUIRE(std::string(proxy.request.begin() + 5, proxy.request.end() - 2) == hostname);
}

TEST_CASE("proxy refusal never falls back to a direct peer connection", "[socks5][peer_session]") {
    fake_socks5 proxy;
    ::asio::ip::tcp::acceptor direct(proxy.ctx, {::asio::ip::address_v4::loopback(), 0});
    ::asio::ip::tcp::socket direct_socket(proxy.ctx);
    bool connected_directly = false;
    direct.async_accept(direct_socket, [&](std::error_code ec) { connected_directly = ! ec; });
    socks5_reply reply;
    reply.connect[1] = 5;
    proxy.start(reply);
    network::settings settings(domain::config::network::regtest);
    settings.proxy = proxy.proxy();
    settings.proxy_randomize = false;
    std::string const hostname = "127.0.0.1";
    auto result = proxy.run(network::async_connect(proxy.ctx.get_executor(), hostname,
        direct.local_endpoint().port(), settings, 1s));
    std::error_code ignored;
    direct.close(ignored);
    proxy.ctx.restart();
    proxy.ctx.run_for(50ms);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::operation_failed);
    REQUIRE(proxy.request.size() == 10);
    REQUIRE( ! connected_directly);
}

TEST_CASE("proxy permissions follow the destination rather than the proxy IP", "[socks5][peer_session]") {
    fake_socks5 proxy;
    network::settings settings(domain::config::network::regtest);
    settings.proxy = proxy.proxy();
    settings.proxy_randomize = false;
    std::string hostname = "192.0.2.1";
    std::string whitelist = "noban@127.0.0.1";
    bool expected_noban = false;
    SECTION("proxy IP grants no permissions to a numeric peer") {}
    SECTION("numeric destination can be whitelisted") {
        whitelist = "noban@192.0.2.1";
        expected_noban = true;
    }
    SECTION("proxy IP grants no permissions to a hostname") {
        hostname = "peer.invalid";
    }
    auto const entry = parse_whitelist(whitelist);
    REQUIRE(entry.has_value());
    settings.whitelist.push_back(*entry);
    proxy.start();
    auto result = proxy.run(network::async_connect(
        proxy.ctx.get_executor(), hostname, 8333, settings, 1s));
    REQUIRE(result.has_value());
    CHECK((*result)->has_permission(permission_flags::noban) == expected_noban);
}

TEST_CASE("two outbound peers through one proxy retain distinct numeric identities", "[socks5][peer_session]") {
    fake_socks5 proxy;
    network::settings settings(domain::config::network::regtest);
    settings.proxy = proxy.proxy();
    settings.proxy_randomize = false;
    std::string const first_host = "192.0.2.1";
    std::string const second_host = "192.0.2.2";
    proxy.start();
    auto first = proxy.run(network::async_connect(proxy.ctx.get_executor(), first_host, 8333, settings, 1s));
    REQUIRE(first.has_value());
    proxy.start();
    auto second = proxy.run(network::async_connect(proxy.ctx.get_executor(), second_host, 8333, settings, 1s));
    REQUIRE(second.has_value());
    auto const first_authority = infrastructure::config::authority::parse_from(first_host, 8333);
    auto const second_authority = infrastructure::config::authority::parse_from(second_host, 8333);
    REQUIRE(first_authority.has_value());
    REQUIRE(second_authority.has_value());
    REQUIRE((*first)->authority() == *first_authority);
    REQUIRE((*second)->authority() == *second_authority);
    REQUIRE((*first)->has_numeric_authority());
    REQUIRE((*second)->has_numeric_authority());
    REQUIRE((*first)->authority() != (*second)->authority());
}

TEST_CASE("proxied hostnames keep separate identities and duplicate checks", "[socks5][peer_session][peer_manager]") {
    fake_socks5 proxy;
    network::settings settings(domain::config::network::regtest);
    settings.proxy = proxy.proxy();
    settings.proxy_randomize = false;
    std::string const first_host = "FIRST.invalid.";
    std::string const second_host = "second.invalid";
    proxy.start();
    auto first = proxy.run(network::async_connect(proxy.ctx.get_executor(), first_host, 8333, settings, 1s));
    REQUIRE(first.has_value());
    proxy.start();
    auto second = proxy.run(network::async_connect(proxy.ctx.get_executor(), second_host, 8333, settings, 1s));
    REQUIRE(second.has_value());
    REQUIRE( ! (*first)->has_numeric_authority());
    REQUIRE( ! (*second)->has_numeric_authority());
    REQUIRE((*first)->destination().host() == "first.invalid");
    REQUIRE((*second)->destination().host() == second_host);
    REQUIRE((*first)->destination() != (*second)->destination());

    auto duplicate = std::make_shared<peer_session>(::asio::ip::tcp::socket(proxy.ctx), settings,
        false, infrastructure::config::endpoint("first.invalid", 8333));
    (*first)->set_nonce(1);
    (*second)->set_nonce(2);
    duplicate->set_nonce(3);
    peer_manager manager(proxy.ctx.get_executor());
    code first_admission;
    code second_admission;
    code duplicate_admission;
    bool found_first = false;
    bool found_second = false;
    bool found_missing = true;
    bool found_unspecified = true;
    bool found_after_stop = true;
    size_t count = 0;
    auto inspect = [&]() -> awaitable_expected<void> {
        first_admission = co_await manager.add(*first);
        second_admission = co_await manager.add(*second);
        duplicate_admission = co_await manager.add(duplicate);
        infrastructure::config::endpoint const first_destination("FIRST.INVALID.", 8333);
        infrastructure::config::endpoint const missing_destination("missing.invalid", 8333);
        auto const unspecified = infrastructure::config::authority::any();
        found_first = co_await manager.exists_by_destination(first_destination);
        found_second = co_await manager.exists_by_destination((*second)->destination());
        found_missing = co_await manager.exists_by_destination(missing_destination);
        found_unspecified = co_await manager.exists_by_authority(unspecified);
        count = co_await manager.count();
        manager.stop_all();
        found_after_stop = co_await manager.exists_by_destination(first_destination);
        co_return std::expected<void, code>{};
    };
    auto inspected = proxy.run(inspect());
    REQUIRE(inspected.has_value());
    REQUIRE(first_admission == error::success);
    REQUIRE(second_admission == error::success);
    REQUIRE(duplicate_admission == error::address_in_use);
    REQUIRE(found_first);
    REQUIRE(found_second);
    REQUIRE( ! found_missing);
    REQUIRE( ! found_unspecified);
    REQUIRE( ! found_after_stop);
    REQUIRE(count == 2);
}
