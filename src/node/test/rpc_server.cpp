// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <expected>
#include <functional>
#include <optional>
#include <stop_token>
#include <string_view>
#include <thread>
#include <vector>

#include <future>
#include <system_error>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <asio/this_coro.hpp>
#include <variant>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>

#include <kth/blockchain/pools/mempool.hpp>
#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/rpc/server.hpp>
#include <kth/node/settings.hpp>
#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;

namespace {

// The real JSON-RPC server on a loopback port the system assigns, so a test
// can send the exact bytes an HTTP client puts on the wire and read back what
// the server answers.
struct rpc_server_fixture {
    explicit rpc_server_fixture(char const* tag)
        : chain(tag) {
        settings.bind = "127.0.0.1";
        settings.port = 0;
        settings.user = "test";
        settings.password = "test";
    }

    ~rpc_server_fixture() {
        if (server) server->stop();
        if (runner.joinable()) runner.join();
    }

    void finish() {
        server->stop();
        if (runner.joinable()) runner.join();
        if (server_done.valid()) server_done.get();
    }

    [[nodiscard]] bool start() {
        if ( ! chain.created() || ! chain.start()) return false;
        server = std::make_unique<node::rpc::server>(settings, chain.chain(), io.get_executor(),
            nullptr, std::chrono::steady_clock::now(), methods);
        server_done = ::asio::co_spawn(io, server->run(), ::asio::use_future);
        auto started = ::asio::co_spawn(io, await_startup(), ::asio::use_future);
        runner = std::thread([this] { io.run(); });
        // Only the synchronous Catch2 driver crosses the executor boundary.
        // Readiness, failure and the deadline suspend coroutines on the I/O loop.
        auto const result = started.get();
        startup_error = result.first;
        port = result.second;
        return !startup_error && port != 0;
    }

    ::asio::awaitable<std::pair<std::error_code, uint16_t>> listening_result() {
        try {
            auto const bound = co_await server->wait_until_listening();
            co_return std::make_pair(std::error_code{}, bound);
        } catch (std::system_error const& e) {
            co_return std::make_pair(e.code(), uint16_t{});
        }
    }

    ::asio::awaitable<std::pair<std::error_code, uint16_t>> await_startup() {
        using namespace ::asio::experimental::awaitable_operators;
        ::asio::steady_timer deadline(io, std::chrono::seconds(5));
        auto result = co_await (listening_result() || deadline.async_wait(::asio::use_awaitable));
        if (result.index() == 0) co_return std::get<0>(result);
        co_return std::make_pair(std::error_code(::asio::error::timed_out), uint16_t{});
    }

    // Send the bytes as given and read until the server closes the connection.
    [[nodiscard]] std::string exchange(std::string const& request) const {
        ::asio::io_context client;
        ::asio::ip::tcp::socket socket(client);
        socket.connect({::asio::ip::make_address("127.0.0.1"), port});
        ::asio::write(socket, ::asio::buffer(request));
        // Nothing more is sent. A server that lost a request then sees EOF and
        // closes, so a missing response fails the count below instead of
        // leaving this read waiting.
        socket.shutdown(::asio::ip::tcp::socket::shutdown_send);
        std::string reply;
        std::error_code ec;
        ::asio::read(socket, ::asio::dynamic_buffer(reply), ec);
        return reply;
    }

    // A bounded conversation on one connection. The second write happens only
    // after the first response's full Content-Length body has been received.
    static ::asio::awaitable<std::string> read_response(::asio::ip::tcp::socket& socket) {
        std::string reply;
        co_await ::asio::async_read_until(socket, ::asio::dynamic_buffer(reply),
            "\r\n\r\n", ::asio::use_awaitable);
        auto const head_end = reply.find("\r\n\r\n") + 4;
        auto const field = reply.find("Content-Length: ");
        if (field == std::string::npos || field >= head_end) {
            throw std::runtime_error("response lacks Content-Length");
        }
        auto const length = std::stoul(reply.substr(field + 16));
        if (reply.size() < head_end + length) {
            co_await ::asio::async_read(socket, ::asio::dynamic_buffer(reply),
                ::asio::transfer_exactly(head_end + length - reply.size()),
                ::asio::use_awaitable);
        }
        if (reply.size() != head_end + length) {
            throw std::runtime_error("unsolicited response bytes");
        }
        co_return reply;
    }

    static ::asio::awaitable<std::string> sequence(
        ::asio::ip::tcp::socket& socket, uint16_t port,
        std::string const& first, std::string const& second) {
        co_await socket.async_connect(
            {::asio::ip::make_address("127.0.0.1"), port}, ::asio::use_awaitable);
        co_await ::asio::async_write(socket, ::asio::buffer(first), ::asio::use_awaitable);
        auto reply = co_await read_response(socket);
        co_await ::asio::async_write(socket, ::asio::buffer(second), ::asio::use_awaitable);
        socket.shutdown(::asio::ip::tcp::socket::shutdown_send);
        reply += co_await read_response(socket);
        std::string tail;
        auto [ec, size] = co_await ::asio::async_read(socket, ::asio::dynamic_buffer(tail),
            ::asio::as_tuple(::asio::use_awaitable));
        if (ec != ::asio::error::eof || size != 0) {
            throw std::runtime_error("unexpected bytes or missing EOF after second response");
        }
        co_return reply;
    }

    static ::asio::awaitable<std::string> timed_sequence(
        uint16_t port, std::string first, std::string second) {
        using namespace ::asio::experimental::awaitable_operators;
        auto executor = co_await ::asio::this_coro::executor;
        ::asio::ip::tcp::socket socket(executor);
        ::asio::steady_timer deadline(executor, std::chrono::seconds(5));
        auto result = co_await (sequence(socket, port, first, second)
            || deadline.async_wait(::asio::use_awaitable));
        if (result.index() != 0) {
            throw std::system_error(::asio::error::timed_out);
        }
        co_return std::get<0>(std::move(result));
    }

    [[nodiscard]] std::string exchange_after_response(std::string first, std::string second) const {
        ::asio::io_context client;
        auto done = ::asio::co_spawn(client,
            timed_sequence(port, std::move(first), std::move(second)), ::asio::use_future);
        client.run();
        return done.get();
    }

    test::chain_fixture chain;
    node::rpc_settings settings;
    // Methods a test adds to the server's own.
    std::function<void(node::rpc::dispatcher&)> methods;
    ::asio::io_context io;
    std::unique_ptr<node::rpc::server> server;
    std::thread runner;
    std::future<void> server_done;
    std::error_code startup_error;
    uint16_t port = 0;
};

// A POST of `body` with Basic auth for test:test.
std::string post(std::string const& body, std::string_view connection) {
    return "POST / HTTP/1.1\r\n"
           "Host: 127.0.0.1\r\n"
           "Authorization: Basic dGVzdDp0ZXN0\r\n"
           "Content-Type: application/json\r\n"
           "Connection: " + std::string(connection) + "\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "\r\n" + body;
}

// A POST of `method` without parameters.
std::string call(int id, std::string_view method, std::string_view connection) {
    return post(R"({"jsonrpc":"1.0","id":)" + std::to_string(id)
        + R"(,"method":")" + std::string(method) + R"(","params":[]})", connection);
}

// A POST of getblockcount.
std::string request(int id, std::string_view connection) {
    return call(id, "getblockcount", connection);
}

// The bodies of the HTTP responses in a reply stream, framed by Content-Length.
std::vector<std::string> bodies(std::string_view reply) {
    std::vector<std::string> out;
    constexpr std::string_view length_field = "Content-Length: ";
    while ( ! reply.empty()) {
        auto const end = reply.find("\r\n\r\n");
        if (end == std::string_view::npos) break;
        auto const head = reply.substr(0, end);
        auto const field = head.find(length_field);
        if (field == std::string_view::npos) break;
        auto const length = std::stoul(std::string(head.substr(field + length_field.size())));
        auto const start = end + 4;
        if (reply.size() < start + length) break;
        out.emplace_back(reply.substr(start, length));
        reply.remove_prefix(start + length);
    }
    return out;
}

// A keep-alive getblocktemplate, long-polling when `longpollid` is not empty.
std::string template_request(std::string_view longpollid) {
    auto const params = longpollid.empty() ? std::string("[]")
        : R"([{"longpollid":")" + std::string(longpollid) + R"("}])";
    return post(R"({"jsonrpc":"1.0","id":9,"method":"getblocktemplate","params":)" + params + "}",
        "keep-alive");
}

// The longpollid in a getblocktemplate reply, empty when it has none.
std::string longpollid_of(std::string_view reply) {
    constexpr std::string_view field = R"("longpollid":")";
    auto const begin = reply.find(field);
    if (begin == std::string_view::npos) return {};
    auto const start = begin + field.size();
    return std::string(reply.substr(start, reply.find('"', start) - start));
}

// `work`, unless `limit` passes first: then timed_out, so a reply that never
// comes fails the test instead of leaving it waiting.
template <typename T>
::asio::awaitable<T> bounded(::asio::awaitable<T> work, std::chrono::steady_clock::duration limit) {
    using namespace ::asio::experimental::awaitable_operators;
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer deadline(executor, limit);
    auto result = co_await (std::move(work) || deadline.async_wait(::asio::use_awaitable));
    if (result.index() != 0) {
        throw std::system_error(::asio::error::timed_out);
    }
    co_return std::get<0>(std::move(result));
}

// Whether anything arrives on `socket` within `wait`; a held long poll sends
// nothing. It peeks, so what does arrive is still there to read.
::asio::awaitable<bool> replies_within(::asio::ip::tcp::socket& socket,
    std::chrono::steady_clock::duration wait) {
    using namespace ::asio::experimental::awaitable_operators;
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor, wait);
    std::array<char, 1> byte{};
    auto const result = co_await (
        socket.async_receive(::asio::buffer(byte), ::asio::socket_base::message_peek,
            ::asio::as_tuple(::asio::use_awaitable))
        || timer.async_wait(::asio::as_tuple(::asio::use_awaitable)));
    co_return result.index() == 0;
}

// Ask for a template on a new connection, then send its longpollid back. True
// once that long poll is held: nothing comes back within `wait`.
::asio::awaitable<bool> hold_long_poll(::asio::ip::tcp::socket& socket, uint16_t port,
    std::chrono::steady_clock::duration wait) {
    co_await socket.async_connect({::asio::ip::make_address("127.0.0.1"), port},
        ::asio::use_awaitable);
    auto const first = template_request({});
    co_await ::asio::async_write(socket, ::asio::buffer(first), ::asio::use_awaitable);
    auto const id = longpollid_of(co_await rpc_server_fixture::read_response(socket));
    if (id.empty()) {
        co_return false;
    }
    auto const poll = template_request(id);
    co_await ::asio::async_write(socket, ::asio::buffer(poll), ::asio::use_awaitable);
    co_return ! co_await replies_within(socket, wait);
}

// One request on a new connection; the reply as it came off the wire.
::asio::awaitable<std::string> ask(uint16_t port, std::string message) {
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::ip::tcp::socket socket(executor);
    co_await socket.async_connect({::asio::ip::make_address("127.0.0.1"), port},
        ::asio::use_awaitable);
    co_await ::asio::async_write(socket, ::asio::buffer(message), ::asio::use_awaitable);
    co_return co_await rpc_server_fixture::read_response(socket);
}

// Everything left on `socket` until the server closes it.
::asio::awaitable<std::string> read_rest(::asio::ip::tcp::socket& socket) {
    std::string rest;
    auto const result = co_await ::asio::async_read(socket, ::asio::dynamic_buffer(rest),
        ::asio::as_tuple(::asio::use_awaitable));
    auto const& ec = std::get<0>(result);
    if (ec != ::asio::error::eof && ec != ::asio::error::connection_reset) {
        throw std::system_error(ec);
    }
    co_return rest;
}

// Run a client coroutine on the test's thread until it ends.
template <typename T>
T run_client(::asio::io_context& client, ::asio::awaitable<T> work) {
    auto done = ::asio::co_spawn(client, std::move(work), ::asio::use_future);
    client.restart();
    client.run();
    return done.get();
}

// Change the mempool without announcing it. A held long poll then answers at
// its minimum wait at the latest, so a test that stops the server cannot wait
// on it for ever, even if the stop does not reach it.
void bound_hold(test::chain_fixture& chain) {
    auto const tx = std::make_shared<domain::message::transaction>(
        domain::chain::transaction{1u, 7u, {}, {}});
    REQUIRE(chain.chain().mempool_ref().add(blockchain::mempool_entry{
        tx, 0u, uint32_t(tx->serialized_size(true)), 0u, 0u}));
}

using node::rpc::doorbell;
using node::rpc::wake;
using wakes = ::asio::experimental::select_channel<wake>;

constexpr auto patience = std::chrono::seconds(5);

// Run `work` on `io`, the context of the sockets it uses, until it ends or
// `limit` passes. Nullopt for the limit: the test thread never waits on a
// server for ever.
template <typename T>
std::optional<T> within(::asio::io_context& io, ::asio::awaitable<T> work,
    std::chrono::steady_clock::duration limit = patience) {
    using namespace ::asio::experimental::awaitable_operators;
    auto done = ::asio::co_spawn(io,
        [&]() -> ::asio::awaitable<std::optional<T>> {
            ::asio::steady_timer deadline(io, limit);
            auto result = co_await (std::move(work)
                || deadline.async_wait(::asio::as_tuple(::asio::use_awaitable)));
            if (result.index() != 0) co_return std::nullopt;
            co_return std::get<0>(std::move(result));
        }, ::asio::use_future);
    io.restart();
    io.run();
    return done.get();
}

// The next value of a channel a server's handler writes to, from the test's
// thread; nullopt when none came within `limit`.
template <typename T>
std::optional<T> take(::asio::experimental::select_channel<T>& channel,
    std::chrono::steady_clock::duration limit = patience) {
    using namespace ::asio::experimental;
    ::asio::io_context io;
    auto done = ::asio::co_spawn(io,
        [&]() -> ::asio::awaitable<std::optional<T>> {
            auto const executor = co_await ::asio::this_coro::executor;
            auto completion = co_await make_select(receive(channel),
                after(std::chrono::steady_clock::now() + limit)
            ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
            auto& [ec, selected] = completion;
            if (ec || selected.index != 0) co_return std::nullopt;
            co_return std::move(*selected.template value<0>());
        }, ::asio::use_future);
    io.run();
    return done.get();
}

// Everything the server sends on `socket` until it closes the connection.
::asio::awaitable<std::string> read_to_end(::asio::ip::tcp::socket& socket) {
    std::string stream;
    auto [ec, size] = co_await ::asio::async_read(socket, ::asio::dynamic_buffer(stream),
        ::asio::as_tuple(::asio::use_awaitable));
    if (ec != ::asio::error::eof) {
        throw std::system_error(ec);
    }
    co_return stream;
}

// A server with one more method, "hold": a request that waits for the mining
// work to change, the way a long-polled template request does. `entered`
// rings when a hold has taken its watch and is about to wait; `woken` gets
// why each one stopped. The reply names the reason too.
struct held_requests {
    explicit held_requests(char const* tag)
        : fixture(tag) {
        fixture.methods = [this](node::rpc::dispatcher& d) {
            d.add("hold", [this](node::rpc::method_context& ctx, node::rpc::request const&)
                    -> ::asio::awaitable<std::expected<std::string, node::rpc::rpc_error>> {
                auto const watched = node::rpc::watch_work(ctx);
                (void)entered.try_send(std::monostate{});
                // Far beyond any test's patience, yet a hold nothing woke
                // still ends and lets the server be joined.
                auto const why = co_await node::rpc::wait_for_work(ctx, watched,
                    std::chrono::steady_clock::now() + std::chrono::seconds(30));
                (void)woken.try_send(wake(why));
                co_return std::string(name(why));
            });
        };
    }

    static std::string_view name(wake why) {
        switch (why) {
            case wake::work_changed: return R"("work_changed")";
            case wake::deadline: return R"("deadline")";
            case wake::connection_closed: return R"("connection_closed")";
        }
        return R"("unknown")";
    }

    // Connect and send a hold; true once the server's handler is waiting.
    [[nodiscard]] bool hold(::asio::ip::tcp::socket& socket, std::string const& bytes) {
        socket.connect({::asio::ip::make_address("127.0.0.1"), fixture.port});
        ::asio::write(socket, ::asio::buffer(bytes));
        return take(entered).has_value();
    }

    // Change the mempool the way an admission does.
    void change_mempool(uint32_t tag) {
        auto const tx = std::make_shared<domain::message::transaction>(
            domain::chain::transaction{1u, tag, {}, {}});
        REQUIRE(fixture.chain.chain().mempool_ref().add(blockchain::mempool_entry{
            tx, 0u, uint32_t(tx->serialized_size(true)), 0u, 0u}));
    }

    // Declared before the fixture: the server's handlers write to them until
    // the fixture has stopped and joined the server.
    ::asio::experimental::select_channel<std::monostate> entered{128};
    wakes woken{128};
    rpc_server_fixture fixture;
};

} // namespace

TEST_CASE("rpc server answers every request pipelined on one connection", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_pipelined");
    REQUIRE(fixture.start());

    // Three requests in one write, the way keep-alive clients pipeline them.
    auto const reply = fixture.exchange(
        request(1, "keep-alive") + request(2, "keep-alive") + request(3, "close"));

    auto const answers = bodies(reply);
    REQUIRE(answers.size() == 3);
    CHECK(answers[0].find(R"("id":1)") != std::string::npos);
    CHECK(answers[1].find(R"("id":2)") != std::string::npos);
    CHECK(answers[2].find(R"("id":3)") != std::string::npos);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server reports bind failure to its startup waiter", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_startup_failure");
    fixture.settings.bind = "not-an-ip-address";
    REQUIRE_FALSE(fixture.start());
    CHECK(fixture.startup_error == ::asio::error::invalid_argument);
    CHECK(fixture.startup_error != ::asio::error::timed_out);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server joins an admitted connection during shutdown", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_join_connection");
    REQUIRE(fixture.start());
    ::asio::io_context client;
    ::asio::ip::tcp::socket socket(client);
    socket.connect({::asio::ip::make_address("127.0.0.1"), fixture.port});
    auto const first = request(1, "keep-alive");
    ::asio::write(socket, ::asio::buffer(first));
    std::string reply;
    ::asio::read_until(socket, ::asio::dynamic_buffer(reply), "\r\n\r\n");
    auto const header_end = reply.find("\r\n\r\n") + 4;
    auto const field = reply.find("Content-Length: ");
    REQUIRE(field != std::string::npos);
    auto const length = std::stoul(reply.substr(field + 16));
    if (reply.size() < header_end + length) {
        ::asio::read(socket, ::asio::dynamic_buffer(reply),
            ::asio::transfer_exactly(header_end + length - reply.size()));
    }
    REQUIRE(bodies(reply).size() == 1);
    // An answered request proves admission, rather than just a backlog entry.
    std::string const partial = "POST / HTTP/1.1\r\nHost: localhost\r\n";
    ::asio::write(socket, ::asio::buffer(partial));
    CHECK_NOTHROW(fixture.finish());
    std::array<char, 1> byte{};
    std::error_code ec;
    CHECK(socket.read_some(::asio::buffer(byte), ec) == 0);
    CHECK((ec == ::asio::error::eof || ec == ::asio::error::connection_reset));
}

TEST_CASE("rpc server wakes a startup waiter stopped before run", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_pre_run_stop");
    REQUIRE(fixture.chain.created());
    REQUIRE(fixture.chain.start());
    fixture.server = std::make_unique<node::rpc::server>(
        fixture.settings, fixture.chain.chain(), fixture.io.get_executor());
    auto startup = ::asio::co_spawn(fixture.io, fixture.await_startup(), ::asio::use_future);
    // There is deliberately no run(): stop must settle readiness on its own.
    fixture.server->stop();
    fixture.io.run();
    auto const result = startup.get();
    CHECK(result.first);
    CHECK(result.first != ::asio::error::timed_out);
    CHECK(result.second == 0);
}

TEST_CASE("rpc server accepts request lines that end in a bare LF", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_bare_lf");
    REQUIRE(fixture.start());

    // The request as ckpool-family pool software writes it: LF line endings.
    std::string const body = R"({"id":0,"method": "getblockcount"})" "\n";
    auto const reply = fixture.exchange(
        "POST / HTTP/1.1\n"
        "Authorization: Basic dGVzdDp0ZXN0\n"
        "Host: 127.0.0.1:28432\n"
        "Content-type: application/json\n"
        "Connection: close\n"
        "Content-Length: " + std::to_string(body.size()) + "\n"
        "\n" + body);

    REQUIRE(reply.starts_with("HTTP/1.1 200 OK\r\n"));
    auto const answers = bodies(reply);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].find(R"("result":0)") != std::string::npos);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server refuses a CR that does not end its line", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_lone_cr");
    REQUIRE(fixture.start());

    std::string const body = R"({"id":0,"method": "getblockcount"})";
    auto const head_end = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;

    // A request line ended by a lone CR, then CRLF headers: mixed framing in
    // which the CR ends no line.
    auto const request_line = fixture.exchange(
        "POST / HTTP/1.1\r"
        "Authorization: Basic dGVzdDp0ZXN0\r\n"
        "Connection: close\r\n" + head_end);
    CHECK(request_line.starts_with("HTTP/1.1 400 Bad Request\r\n"));

    // The same inside a header line.
    auto const header_line = fixture.exchange(
        "POST / HTTP/1.1\r\n"
        "Host: 127.0.0.1\rAuthorization: Basic dGVzdDp0ZXN0\r\n"
        "Connection: close\r\n" + head_end);
    CHECK(header_line.starts_with("HTTP/1.1 400 Bad Request\r\n"));

    // CRLF throughout is answered, so the refusals above are about the CR.
    auto const crlf = fixture.exchange(
        "POST / HTTP/1.1\r\n"
        "Authorization: Basic dGVzdDp0ZXN0\r\n"
        "Connection: close\r\n" + head_end);
    CHECK(crlf.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server validates CR after leading empty lines", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_leading_lines");
    REQUIRE(fixture.start());
    auto malformed = request(1, "close");
    malformed.replace(malformed.find("\r\n"), 2, "\r");
    for (auto const prefix : {"\r\n\r\n", "\n\n"}) {
        auto const reply = fixture.exchange(std::string(prefix) + malformed);
        CHECK(reply.starts_with("HTTP/1.1 400 Bad Request\r\n"));
    }
    auto const valid = fixture.exchange("\r\n\r\n" + request(2, "close"));
    CHECK(valid.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server pipelines LF heads without changing body bytes", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_lf_body");
    REQUIRE(fixture.start());
    auto lf_request = [](std::string const& body, std::string_view connection) {
        return "POST / HTTP/1.1\nAuthorization: Basic dGVzdDp0ZXN0\nConnection: "
            + std::string(connection) + "\nContent-Length: "
            + std::to_string(body.size()) + "\n\n" + body;
    };
    // A lone CR is valid JSON whitespace in the body, not a head terminator.
    std::string const body = "\r" + std::string(R"({"id":7,"method":"getblockcount","params":[]})") + "\r";
    auto const reply = fixture.exchange("\n\n" + lf_request(body, "keep-alive")
        + lf_request(R"({"id":8,"method":"getblockcount","params":[]})", "close"));
    auto const answers = bodies(reply);
    REQUIRE(answers.size() == 2);
    CHECK(answers[0].find(R"("id":7)") != std::string::npos);
    CHECK(answers[0].find(R"("result":0)") != std::string::npos);
    CHECK(answers[1].find(R"("id":8)") != std::string::npos);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server refuses an unfinished CR at head EOF", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_head_eof");
    REQUIRE(fixture.start());
    auto const reply = fixture.exchange("POST / HTTP/1.1\r\nAuthorization: Basic dGVzdDp0ZXN0\r\nContent-Length: 0\r\n\r");
    CHECK(reply.starts_with("HTTP/1.1 400 Bad Request\r\n"));
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server replies end their headers with Content-Length and their body with LF", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_reply_framing");
    REQUIRE(fixture.start());

    // The header line right before the blank line, as a line reader sees it.
    auto last_header = [](std::string_view reply) {
        auto const end = reply.find("\r\n\r\n");
        auto const head = reply.substr(0, end);
        return std::string(head.substr(head.rfind("\r\n") + 2));
    };

    auto const ok = fixture.exchange(request(1, "close"));
    auto const answers = bodies(ok);
    REQUIRE(answers.size() == 1);
    CHECK(last_header(ok).starts_with("Content-Length: "));
    CHECK(answers[0].ends_with("}\n"));

    // An error reply with an extra header is framed the same way.
    auto unauthorized = request(2, "close");
    unauthorized.erase(unauthorized.find("Authorization:"), std::string("Authorization: Basic dGVzdDp0ZXN0\r\n").size());
    auto const denied = fixture.exchange(unauthorized);
    REQUIRE(denied.starts_with("HTTP/1.1 401 "));
    CHECK(last_header(denied).starts_with("Content-Length: "));
    REQUIRE(bodies(denied).size() == 1);
    CHECK(bodies(denied)[0].ends_with("}\n"));
}

TEST_CASE("rpc server accepts a later keep-alive request", "[rpc server]") {
    rpc_server_fixture fixture("rpc_server_sequential_keep_alive");
    REQUIRE(fixture.start());
    auto const reply = fixture.exchange_after_response(
        request(41, "keep-alive"), request(42, "close"));
    auto const answers = bodies(reply);
    REQUIRE(answers.size() == 2);
    CHECK(answers[0].find(R"("id":41)") != std::string::npos);
    CHECK(answers[1].find(R"("id":42)") != std::string::npos);
    CHECK(answers[0].find(R"("result":0)") != std::string::npos);
    CHECK(answers[1].find(R"("result":0)") != std::string::npos);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server answers other connections while a long poll is held", "[rpc server][rpc longpoll]") {
    rpc_server_fixture fixture("rpc_server_longpoll_others");
    // A limit of zero keeps the chain synchronized whatever its tip's age, so
    // the node serves templates.
    fixture.chain.settings().notify_limit_hours = 0;
    REQUIRE(fixture.start());

    ::asio::io_context client;
    ::asio::ip::tcp::socket held(client);
    REQUIRE(run_client(client, bounded(hold_long_poll(held, fixture.port,
        std::chrono::milliseconds(300)), std::chrono::seconds(5))));

    auto const other = run_client(client, bounded(ask(fixture.port, request(5, "close")),
        std::chrono::seconds(5)));
    auto const answers = bodies(other);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].find(R"("id":5)") != std::string::npos);
    CHECK(answers[0].find(R"("result":0)") != std::string::npos);
    CHECK_FALSE(run_client(client, replies_within(held, std::chrono::milliseconds(200))));

    bound_hold(fixture.chain);
    CHECK_NOTHROW(fixture.finish());
}

TEST_CASE("rpc server stop ends a held long poll at once", "[rpc server][rpc longpoll]") {
    rpc_server_fixture fixture("rpc_server_longpoll_stop");
    fixture.chain.settings().notify_limit_hours = 0;
    REQUIRE(fixture.start());

    ::asio::io_context client;
    ::asio::ip::tcp::socket held(client);
    REQUIRE(run_client(client, bounded(hold_long_poll(held, fixture.port,
        std::chrono::milliseconds(300)), std::chrono::seconds(5))));
    bound_hold(fixture.chain);

    // The server drains its connections before it returns, the held one
    // included. Unless the stop reaches the hold, that waits out the minimum
    // wait, a minute away.
    auto const began = std::chrono::steady_clock::now();
    CHECK_NOTHROW(fixture.finish());
    CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(10));

    // The connection is closed. If an answer came first, it is the shutdown
    // error, not a template.
    auto const rest = run_client(client, bounded(read_rest(held), std::chrono::seconds(5)));
    for (auto const& body : bodies(rest)) {
        CHECK(body.find(R"("code":-9)") != std::string::npos);
    }
    CHECK(rest.find(R"("previousblockhash")") == std::string::npos);
TEST_CASE("rpc server ends a held request whose client went away", "[rpc server][rpc held]") {
    held_requests held("rpc_server_held_abandoned");
    REQUIRE(held.fixture.start());

    // As many abandoned requests as the server admits connections. Each one
    // that stayed held would keep its connection, and the last would leave no
    // room for anyone.
    ::asio::io_context client;
    for (size_t i = 0; i < 64; ++i) {
        ::asio::ip::tcp::socket socket(client);
        REQUIRE(held.hold(socket, call(int(i), "hold", "keep-alive")));
        std::error_code ignored;
        socket.close(ignored);
        auto const why = take(held.woken);
        REQUIRE(why.has_value());
        REQUIRE(*why == wake::connection_closed);
    }

    ::asio::ip::tcp::socket socket(client);
    socket.connect({::asio::ip::make_address("127.0.0.1"), held.fixture.port});
    ::asio::write(socket, ::asio::buffer(request(7, "close")));
    auto const reply = within(client, rpc_server_fixture::read_response(socket));
    REQUIRE(reply.has_value());
    auto const answers = bodies(*reply);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].find(R"("id":7)") != std::string::npos);
    CHECK_NOTHROW(held.fixture.finish());
}

TEST_CASE("rpc server answers a held request whose client only stopped sending", "[rpc server][rpc held]") {
    held_requests held("rpc_server_held_half_closed");
    REQUIRE(held.fixture.start());

    ::asio::io_context client;
    ::asio::ip::tcp::socket socket(client);
    REQUIRE(held.hold(socket, call(1, "hold", "close")));
    socket.shutdown(::asio::ip::tcp::socket::shutdown_send);

    // The wait ends, and the client, still reading, gets its answer.
    auto const reply = within(client, rpc_server_fixture::read_response(socket));
    REQUIRE(reply.has_value());
    auto const answers = bodies(*reply);
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].find(R"("result":"connection_closed")") != std::string::npos);
    CHECK_NOTHROW(held.fixture.finish());
}

TEST_CASE("rpc server wakes every held request when the mining work changes", "[rpc server][rpc held]") {
    held_requests held("rpc_server_held_work");
    REQUIRE(held.fixture.start());

    ::asio::io_context client;
    ::asio::ip::tcp::socket first(client);
    ::asio::ip::tcp::socket second(client);

    auto const both_answer = [&] {
        for (auto* socket : {&first, &second}) {
            auto const reply = within(client, rpc_server_fixture::read_response(*socket));
            REQUIRE(reply.has_value());
            auto const answers = bodies(*reply);
            REQUIRE(answers.size() == 1);
            CHECK(answers[0].find(R"("result":"work_changed")") != std::string::npos);
        }
    };

    SECTION("a change of the mempool") {
        REQUIRE(held.hold(first, call(1, "hold", "keep-alive")));
        REQUIRE(held.hold(second, call(2, "hold", "keep-alive")));
        held.change_mempool(7u);
        both_answer();
    }

    SECTION("the end of a transition") {
        REQUIRE(held.hold(first, call(1, "hold", "keep-alive")));
        REQUIRE(held.hold(second, call(2, "hold", "keep-alive")));
        REQUIRE(held.fixture.chain.chain().begin_transition());
        held.fixture.chain.chain().end_transition();
        both_answer();
    }

    SECTION("one change after another, each for the requests held then") {
        REQUIRE(held.hold(first, call(1, "hold", "keep-alive")));
        held.change_mempool(7u);
        auto const reply = within(client, rpc_server_fixture::read_response(first));
        REQUIRE(reply.has_value());

        // Held again on the same connections: the first change is spent.
        ::asio::write(first, ::asio::buffer(call(3, "hold", "keep-alive")));
        REQUIRE(take(held.entered).has_value());
        REQUIRE(held.hold(second, call(4, "hold", "keep-alive")));
        std::array<char, 1> byte{};
        first.non_blocking(true);
        std::error_code ec;
        first.receive(::asio::buffer(byte), ::asio::socket_base::message_peek, ec);
        CHECK(ec == ::asio::error::would_block);
        first.non_blocking(false);

        held.change_mempool(8u);
        both_answer();
    }
    CHECK_NOTHROW(held.fixture.finish());
}

TEST_CASE("rpc server keeps the request that follows a held one on its connection", "[rpc server][rpc held]") {
    held_requests held("rpc_server_held_pipelined");
    REQUIRE(held.fixture.start());

    // The second request arrives while the first is held, so it is read by
    // whatever watches the connection meanwhile. The client then stops
    // sending, and that is what ends the hold: the end of the stream comes
    // after the request, so the request has been read by then.
    ::asio::io_context client;
    ::asio::ip::tcp::socket socket(client);
    REQUIRE(held.hold(socket, call(1, "hold", "keep-alive")));
    ::asio::write(socket, ::asio::buffer(request(2, "close")));
    socket.shutdown(::asio::ip::tcp::socket::shutdown_send);

    // Both replies may come in one segment, so the stream is read whole: the
    // second request asks the server to close after answering.
    auto const stream = within(client, read_to_end(socket));
    REQUIRE(stream.has_value());
    auto const answers = bodies(*stream);
    REQUIRE(answers.size() == 2);
    CHECK(answers[0].find(R"("id":1)") != std::string::npos);
    CHECK(answers[0].find(R"("result":"connection_closed")") != std::string::npos);
    CHECK(answers[1].find(R"("id":2)") != std::string::npos);
    CHECK(answers[1].find(R"("result":0)") != std::string::npos);
    CHECK_NOTHROW(held.fixture.finish());
}

TEST_CASE("rpc server stop ends a held request", "[rpc server][rpc held]") {
    held_requests held("rpc_server_held_stop");
    REQUIRE(held.fixture.start());

    ::asio::io_context client;
    ::asio::ip::tcp::socket socket(client);
    REQUIRE(held.hold(socket, call(1, "hold", "keep-alive")));

    // The server joins its connections before it returns. A held request that
    // the stop did not reach would keep it from returning at all.
    auto const began = std::chrono::steady_clock::now();
    CHECK_NOTHROW(held.fixture.finish());
    CHECK(std::chrono::steady_clock::now() - began < patience);
    auto const why = take(held.woken);
    REQUIRE(why.has_value());
    CHECK(*why == wake::connection_closed);
}

TEST_CASE("rpc server stop ends a held request it no longer reads behind", "[rpc server][rpc held]") {
    held_requests held("rpc_server_held_stop_read_ahead");
    REQUIRE(held.fixture.start());

    // More pipelined requests behind the hold than the server reads ahead:
    // past that it stops reading the connection, so no read is there to
    // report the socket the stop closes.
    ::asio::io_context client;
    ::asio::ip::tcp::socket socket(client);
    REQUIRE(held.hold(socket, call(1, "hold", "keep-alive")));
    std::string behind;
    while (behind.size() < 3 * 8192) {
        behind += request(2, "keep-alive");
    }
    ::asio::write(socket, ::asio::buffer(behind));

    // Requests on another connection, each answered after the server's
    // strand has run: by the last one it has read ahead as far as it will.
    for (int i = 0; i < 8; ++i) {
        ::asio::ip::tcp::socket other(client);
        other.connect({::asio::ip::make_address("127.0.0.1"), held.fixture.port});
        ::asio::write(other, ::asio::buffer(request(10 + i, "close")));
        REQUIRE(within(client, read_to_end(other)).has_value());
    }

    auto const began = std::chrono::steady_clock::now();
    CHECK_NOTHROW(held.fixture.finish());
    CHECK(std::chrono::steady_clock::now() - began < patience);
    auto const why = take(held.woken);
    REQUIRE(why.has_value());
    CHECK(*why == wake::connection_closed);
}

TEST_CASE("a change after a request's watch ends its wait, however late it waits", "[rpc held]") {
    node::rpc::work_bell bell;
    test::chain_fixture chain("rpc_held_watch_first");
    REQUIRE(chain.created());
    REQUIRE(chain.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context ctx{chain.chain(), jobs};
    ctx.work = &bell;
    ::asio::io_context waiting;
    auto const far = std::chrono::steady_clock::now() + std::chrono::hours(1);

    SECTION("rung between the watch and the wait") {
        auto const watched = node::rpc::watch_work(ctx);
        bell.ring();
        auto const why = within(waiting, node::rpc::wait_for_work(ctx, watched, far));
        REQUIRE(why.has_value());
        CHECK(*why == wake::work_changed);
    }

    SECTION("a ring nobody watched wakes no later request") {
        bell.ring();
        auto const watched = node::rpc::watch_work(ctx);
        auto const why = within(waiting, node::rpc::wait_for_work(ctx, watched,
            std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
        REQUIRE(why.has_value());
        CHECK(*why == wake::deadline);
    }

    SECTION("a connection already gone ends the wait before it starts") {
        doorbell closed{1};
        closed.close();
        ctx.closed = &closed;
        auto const watched = node::rpc::watch_work(ctx);
        bell.ring();
        auto const why = within(waiting, node::rpc::wait_for_work(ctx, watched, far));
        REQUIRE(why.has_value());
        CHECK(*why == wake::connection_closed);
    }

    SECTION("without a server only the deadline wakes") {
        node::rpc::method_context bare{chain.chain(), jobs};
        auto const why = within(waiting, node::rpc::wait_for_work(bare, node::rpc::watch_work(bare),
            std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
        REQUIRE(why.has_value());
        CHECK(*why == wake::deadline);
    }
}
