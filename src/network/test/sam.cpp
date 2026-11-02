// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/domain/message/heading.hpp>
#include <kth/network/sam.hpp>
#include <kth/network/peer_session.hpp>

using namespace kth;
using namespace kth::network;
using namespace std::chrono_literals;

namespace {

using socket_type = ::asio::ip::tcp::socket;

data_chunk bch_message() {
    settings const config(domain::config::network::regtest);
    return domain::message::heading(config.identifier, "verack", 0, bitcoin_checksum(byte_span{})).to_data();
}

std::string i2p_base64(data_chunk const& bytes) {
    auto result = encode_base64(bytes);
    for (auto& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '~';
    }
    return result;
}

struct sam_key_file {
    kth::path directory;
    kth::path file;
    sam_key_file() {
        std::random_device random;
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() /
                ("kth_sam_" + std::to_string(random()) + "_" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                directory = candidate;
                file = directory / "i2p_private_key";
                return;
            }
        }
        throw std::runtime_error("Cannot create SAM test directory");
    }
    ~sam_key_file() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};

template <typename T>
struct sam_completion {
    std::optional<std::expected<T, code>> result;
    std::exception_ptr exception;
    bool done{false};
};

template <typename T>
::asio::awaitable<void> collect_sam(awaitable_expected<T> operation,
    std::shared_ptr<sam_completion<T>> state)
{
    state->result.emplace(co_await std::move(operation));
}

struct fake_sam {
    ::asio::io_context ctx;
    ::asio::ip::tcp::acceptor listener{ctx, {::asio::ip::address_v4::loopback(), 0}};
    std::vector<std::shared_ptr<socket_type>> sockets;
    std::vector<std::string> commands;
    std::vector<std::exception_ptr> exceptions;
    data_chunk destination = data_chunk(387, 0);
    std::string public_key;
    std::string private_key;
    std::string fail_at;
    std::optional<std::string> peer_line;
    std::string failure{"RESULT=I2P_ERROR MESSAGE=\"bridge failure\""};
    bool split{false};
    bool failed{false};
    bool run_done{false};
    bool echoed{false};
    bool advanced_after_failure{false};
    size_t generated{0};
    size_t accepted{0};
    size_t controls{0};
    size_t connections_done{0};
    std::chrono::steady_clock::duration budget{2s};
    std::unique_ptr<sam_session> session;

    fake_sam() {
        destination[0] = 0xfb;
        destination[1] = 0xff;
        destination[2] = 0xff;
        // A key certificate and Ed25519 signing type, as returned by SIGNATURE_TYPE=7.
        destination[384] = 5;
        destination[386] = 4;
        destination.insert(destination.end(), {0, 7, 0, 0});
        public_key = i2p_base64(destination);
        auto secret = destination;
        secret.insert(secret.end(), 288, 0x4a);
        private_key = i2p_base64(secret);
        ::asio::co_spawn(ctx, serve(), [this](std::exception_ptr e) {
            if (e) exceptions.push_back(e);
        });
    }

    ~fake_sam() { finish(); }

    std::string bridge() const {
        return "127.0.0.1:" + std::to_string(listener.local_endpoint().port());
    }

    infrastructure::config::authority peer() const {
        return infrastructure::message::network_address(0, 0,
            infrastructure::message::address_network::i2p, sha256_hash(destination), 0);
    }

    ::asio::awaitable<std::string> line(socket_type& socket) {
        std::string result;
        while (result.size() < 65536) {
            char c;
            co_await ::asio::async_read(socket, ::asio::buffer(&c, 1), ::asio::use_awaitable);
            if (c == '\n') {
                commands.push_back(result);
                co_return result;
            }
            result.push_back(c);
        }
        throw std::runtime_error("Oversized client command");
    }

    ::asio::awaitable<void> write(socket_type& socket, std::string text) {
        if (split && text.size() > 2) {
            auto const half = text.size() / 2;
            co_await ::asio::async_write(socket, ::asio::buffer(text.data(), half), ::asio::use_awaitable);
            ::asio::steady_timer pause(ctx);
            pause.expires_after(1ms);
            co_await pause.async_wait(::asio::use_awaitable);
            co_await ::asio::async_write(socket, ::asio::buffer(text.data() + half, text.size() - half), ::asio::use_awaitable);
        } else {
            co_await ::asio::async_write(socket, ::asio::buffer(text), ::asio::use_awaitable);
        }
    }

    ::asio::awaitable<bool> reply(socket_type& socket, std::string const& stage, std::string prefix,
        std::string value)
    {
        if (fail_at != stage) {
            co_await write(socket, prefix + " " + value + "\n");
            co_return true;
        }
        failed = true;
        if (failure == "silent") {
            char byte;
            auto [ec, count] = co_await socket.async_read_some(::asio::buffer(&byte, 1), ::asio::as_tuple(::asio::use_awaitable));
            advanced_after_failure = ! ec && count > 0;
            co_return false;
        }
        if (failure == "truncated") {
            co_await write(socket, prefix + " RESULT=O");
        } else if (failure == "oversized") {
            co_await write(socket, std::string(65536, 'X') + "\n");
        } else if (failure == "wrong-prefix") {
            co_await write(socket, "WRONG PREFIX RESULT=OK\n");
        } else {
            co_await write(socket, prefix + " " + failure + "\n");
        }
        std::error_code ignored;
        socket.shutdown(socket_type::shutdown_send, ignored);
        char byte;
        auto [ec, count] = co_await socket.async_read_some(::asio::buffer(&byte, 1), ::asio::as_tuple(::asio::use_awaitable));
        advanced_after_failure = ! ec && count > 0;
        co_return false;
    }

    ::asio::awaitable<void> connection(std::shared_ptr<socket_type> socket) {
        try {
            auto hello = co_await line(*socket);
            if (hello != "HELLO VERSION MIN=3.1 MAX=3.1") throw std::runtime_error("Wrong SAM hello");
            if ( ! co_await reply(*socket, "hello", "HELLO REPLY", "RESULT=OK VERSION=3.1 MESSAGE=\"ready for streams\"")) co_return;
            auto command = co_await line(*socket);
            if (command == "DEST GENERATE SIGNATURE_TYPE=7") {
                ++generated;
                if ( ! co_await reply(*socket, "generate", "DEST REPLY", "PUB=" + public_key + " PRIV=" + private_key)) co_return;
                command = co_await line(*socket);
            }
            if (command.starts_with("SESSION CREATE ")) {
                ++controls;
                if ( ! co_await reply(*socket, "session", "SESSION STATUS", "RESULT=OK DESTINATION=" + private_key)) co_return;
                char c;
                co_await socket->async_read_some(::asio::buffer(&c, 1), ::asio::as_tuple(::asio::use_awaitable));
            } else if (command.starts_with("NAMING LOOKUP NAME=")) {
                if ( ! co_await reply(*socket, "lookup", "NAMING REPLY", "RESULT=OK NAME=" + peer().to_hostname() + " VALUE=" + public_key)) co_return;
                auto const connect = co_await line(*socket);
                if ( ! connect.starts_with("STREAM CONNECT ID=") ||
                    ! connect.ends_with(" DESTINATION=" + public_key + " SILENT=false")) throw std::runtime_error("Wrong STREAM CONNECT");
                if ( ! co_await reply(*socket, "connect", "STREAM STATUS", "RESULT=OK")) co_return;
                data_chunk bytes(bch_message().size());
                auto [ec, count] = co_await ::asio::async_read(*socket, ::asio::buffer(bytes), ::asio::as_tuple(::asio::use_awaitable));
                if (ec) co_return;
                echoed = count == bytes.size() && bytes == bch_message();
                co_await ::asio::async_write(*socket, ::asio::buffer(bytes), ::asio::use_awaitable);
                std::error_code ignored;
                socket->shutdown(socket_type::shutdown_send, ignored);
            } else if (command.starts_with("STREAM ACCEPT ID=") && command.ends_with(" SILENT=false")) {
                if ( ! co_await reply(*socket, "accept", "STREAM STATUS", "RESULT=OK")) co_return;
                if (peer_line == "silent") {
                    char byte;
                    co_await socket->async_read_some(::asio::buffer(&byte, 1), ::asio::as_tuple(::asio::use_awaitable));
                    co_return;
                }
                if (peer_line == "truncated") co_await write(*socket, public_key.substr(0, 10));
                else if (peer_line == "oversized") co_await write(*socket, std::string(65536, 'X') + "\n");
                else {
                    auto const bytes = bch_message();
                    co_await write(*socket, peer_line.value_or(public_key) + "\n" + std::string(bytes.begin(), bytes.end()));
                }
                std::error_code ignored;
                socket->shutdown(socket_type::shutdown_send, ignored);
            } else throw std::runtime_error("Unexpected SAM command");
        } catch (std::system_error const&) {
            // Closing the fixture cancels incomplete reads and writes.
        }
    }

    ::asio::awaitable<void> serve() {
        for (;;) {
            auto socket = std::make_shared<socket_type>(ctx);
            auto [ec] = co_await listener.async_accept(*socket, ::asio::as_tuple(::asio::use_awaitable));
            if (ec) co_return;
            ++accepted;
            sockets.push_back(socket);
            ::asio::co_spawn(ctx, connection(socket), [this](std::exception_ptr e) {
                if (e) exceptions.push_back(e);
                ++connections_done;
            });
        }
    }

    template <typename Predicate>
    bool wait(Predicate predicate, std::chrono::steady_clock::duration limit = {}) {
        if (limit == std::chrono::steady_clock::duration::zero()) limit = budget;
        auto const end = std::chrono::steady_clock::now() + limit;
        while ( ! predicate() && std::chrono::steady_clock::now() < end) {
            ctx.restart();
            ctx.run_for(5ms);
        }
        return predicate();
    }

    void start(kth::path const& key, bool incoming = true,
        std::chrono::steady_clock::duration timeout = 500ms)
    {
        budget = std::max(std::chrono::steady_clock::duration(2s), timeout + 1s);
        session = std::make_unique<sam_session>(ctx.get_executor(), bridge(), key, incoming, timeout);
        ::asio::co_spawn(ctx, session->run(), [this](std::exception_ptr e) {
            if (e) exceptions.push_back(e);
            run_done = true;
        });
    }

    template <typename T>
    std::expected<T, code> run(awaitable_expected<T> operation) {
        auto state = std::make_shared<sam_completion<T>>();
        ::asio::co_spawn(ctx, collect_sam(std::move(operation), state), [state](std::exception_ptr e) {
            state->exception = e;
            state->done = true;
        });
        auto const done = wait([&] { return state->done; });
        REQUIRE(done);
        REQUIRE( ! state->exception);
        REQUIRE(state->result.has_value());
        REQUIRE(exceptions.empty());
        return std::move(*state->result);
    }

    void finish() {
        if (session) session->stop();
        std::error_code ignored;
        listener.close(ignored);
        for (auto const& socket : sockets) socket->close(ignored);
        ctx.restart();
        ctx.run_for(100ms);
    }
};

awaitable_expected<void> exchange(socket_type& socket) {
    auto const sent = bch_message();
    co_await ::asio::async_write(socket, ::asio::buffer(sent), ::asio::use_awaitable);
    std::error_code ignored;
    socket.shutdown(socket_type::shutdown_send, ignored);
    data_chunk received(sent.size());
    auto [ec, count] = co_await ::asio::async_read(socket, ::asio::buffer(received), ::asio::as_tuple(::asio::use_awaitable));
    if (ec || count != received.size() || received != sent) co_return std::unexpected(error::bad_stream);
    co_return std::expected<void, code>{};
}

awaitable_expected<void> receive(socket_type& socket) {
    data_chunk received(bch_message().size());
    auto [ec, count] = co_await ::asio::async_read(socket, ::asio::buffer(received), ::asio::as_tuple(::asio::use_awaitable));
    if (ec || count != received.size() || received != bch_message()) co_return std::unexpected(error::bad_stream);
    co_return std::expected<void, code>{};
}

} // namespace

TEST_CASE("SAM persists one private destination and restricts its file", "[sam]") {
    sam_key_file key;
    std::string saved;
    {
        fake_sam bridge;
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
        CHECK(bridge.generated == 1);
        CHECK(bridge.session->local_address() == bridge.peer());
        CHECK(std::filesystem::status(key.file).permissions() ==
            (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));
        std::ifstream input(key.file, std::ios::binary);
        saved.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        CHECK(saved == bridge.private_key);
        REQUIRE(bridge.commands.size() >= 3);
        CHECK(bridge.commands[2].find(" DESTINATION=" + saved + " SIGNATURE_TYPE=7 i2cp.leaseSetEncType=4,0") != std::string::npos);
        bridge.finish();
        CHECK(bridge.run_done);
        CHECK(bridge.exceptions.empty());
    }
    fake_sam bridge;
    bridge.start(key.file);
    REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
    CHECK(bridge.generated == 0);
    REQUIRE(bridge.commands.size() >= 2);
    CHECK(bridge.commands[1].find(" DESTINATION=" + saved + " ") != std::string::npos);
}

TEST_CASE("SAM streams preserve BCH bytes and inbound identity across split replies", "[sam]") {
    sam_key_file key;
    fake_sam bridge;
    bridge.split = true;
    bridge.start(key.file);
    REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
    auto connected = bridge.run(bridge.session->connect(bridge.peer().to_hostname()));
    REQUIRE(connected);
    REQUIRE(bridge.run(exchange(*connected)));
    CHECK(bridge.echoed);
    auto accepted = bridge.run(bridge.session->accept());
    REQUIRE(accepted);
    CHECK(accepted->peer == bridge.peer());
    REQUIRE(bridge.run(receive(accepted->socket)));
    settings config(domain::config::network::regtest);
    auto peer = std::make_shared<peer_session>(std::move(accepted->socket), config, true,
        infrastructure::config::endpoint(accepted->peer));
    CHECK(peer->authority() == bridge.peer());
    CHECK(peer->destination().host() == bridge.peer().to_hostname());
    CHECK(peer->is_inbound());
    CHECK(std::count_if(bridge.commands.begin(), bridge.commands.end(), [](auto const& command) {
        return command == "HELLO VERSION MIN=3.1 MAX=3.1";
    }) == 3);
}

TEST_CASE("SAM transient destinations do not create a private key file", "[sam]") {
    sam_key_file key;
    fake_sam bridge;
    bridge.start(key.file, false);
    REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
    CHECK(bridge.generated == 0);
    CHECK( ! std::filesystem::exists(key.file));
    REQUIRE(bridge.commands.size() == 2);
    CHECK(bridge.commands[1].find(" DESTINATION=TRANSIENT ") != std::string::npos);
    REQUIRE( ! bridge.run(bridge.session->accept()));
    auto socket = bridge.run(bridge.session->connect(bridge.peer().to_hostname()));
    REQUIRE(socket);
    REQUIRE(bridge.run(exchange(*socket)));
}

TEST_CASE("I2P connector uses SAM before the SOCKS proxy and keeps destination identity", "[sam]") {
    sam_key_file key;
    fake_sam bridge;
    bridge.start(key.file);
    REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
    settings config(domain::config::network::regtest);
    config.i2psam = bridge.bridge();
    config.proxy = "invalid-proxy";
    auto const host = bridge.peer().to_hostname();
    auto connected = bridge.run(async_connect(bridge.ctx.get_executor(), host, 0, config, 1s, bridge.session.get()));
    REQUIRE(connected);
    CHECK((*connected)->authority() == bridge.peer());
    CHECK((*connected)->destination().host() == host);
    CHECK(bridge.accepted == 2);
    (*connected)->stop();
}

TEST_CASE("SAM rejects failed results at every exchange", "[sam]") {
    auto const stage = GENERATE("hello", "generate", "session", "lookup", "connect", "accept");
    auto const result = GENERATE("CANT_REACH_PEER", "TIMEOUT", "INVALID_KEY", "INVALID_ID", "I2P_ERROR",
        "NOVERSION", "DUPLICATED_ID", "DUPLICATED_DEST", "KEY_NOT_FOUND");
    sam_key_file key;
    fake_sam bridge;
    bridge.fail_at = stage;
    bridge.failure = std::string("RESULT=") + result;
    bridge.split = true;
    bridge.start(key.file);
    if (bridge.fail_at == "hello" || bridge.fail_at == "generate" || bridge.fail_at == "session") {
        REQUIRE(bridge.wait([&] { return bridge.failed; }));
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
    } else {
        REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
        if (bridge.fail_at == "accept") CHECK( ! bridge.run(bridge.session->accept()));
        else CHECK( ! bridge.run(bridge.session->connect(bridge.peer().to_hostname())));
    }
    bridge.finish();
    CHECK(bridge.run_done);
    CHECK(bridge.exceptions.empty());
    CHECK( ! bridge.advanced_after_failure);
}

TEST_CASE("SAM bounds truncated oversized and silent replies at every exchange", "[sam]") {
    auto const stage = GENERATE("hello", "generate", "session", "lookup", "connect", "accept");
    auto const failure = GENERATE("truncated", "oversized", "silent");
    sam_key_file key;
    fake_sam bridge;
    bridge.fail_at = stage;
    bridge.failure = failure;
    auto const timeout = bridge.failure == "silent" ? 100ms : 5000ms;
    auto const began = std::chrono::steady_clock::now();
    bridge.start(key.file, true, timeout);
    if (bridge.fail_at == "hello" || bridge.fail_at == "generate" || bridge.fail_at == "session") {
        REQUIRE(bridge.wait([&] { return bridge.failed; }));
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
        if (bridge.failure != "silent") CHECK(std::chrono::steady_clock::now() - began < timeout);
    } else {
        REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
        if (bridge.fail_at == "accept") {
            auto const result = bridge.run(bridge.session->accept());
            REQUIRE( ! result);
            if (bridge.failure == "silent") CHECK(result.error() == error::channel_timeout);
            else CHECK(result.error() != error::channel_timeout);
        } else {
            auto const result = bridge.run(bridge.session->connect(bridge.peer().to_hostname()));
            REQUIRE( ! result);
            if (bridge.failure == "silent") CHECK(result.error() == error::channel_timeout);
            else CHECK(result.error() != error::channel_timeout);
        }
    }
    bridge.finish();
    CHECK(bridge.run_done);
    CHECK(bridge.exceptions.empty());
    CHECK( ! bridge.advanced_after_failure);
}

TEST_CASE("SAM validates destination certificates and lookup identity", "[sam]") {
    sam_key_file key;
    fake_sam bridge;
    SECTION("short private key on disk fails closed") {
        std::ofstream output(key.file);
        output << "AAAA";
        output.close();
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
        CHECK(bridge.generated == 0);
    }
    SECTION("lookup for another destination is refused") {
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
        auto const requested = bridge.peer().to_hostname();
        bridge.destination[0] = 1;
        bridge.public_key = i2p_base64(bridge.destination);
        CHECK( ! bridge.run(bridge.session->connect(requested)));
        CHECK(std::none_of(bridge.commands.begin(), bridge.commands.end(), [](auto const& command) {
            return command.starts_with("STREAM CONNECT");
        }));
    }
}

TEST_CASE("SAM rejects malformed reply fields even when split across reads", "[sam]") {
    auto const reply = GENERATE("VERSION=3.1", "RESULT=OK VERSION=3.0", "RESULT=OK VERSION=",
        "RESULT=OK VERSION=3.1 RESULT=OK", "RESULT=OK VERSION=3.1 BARE",
        "RESULT=OK VERSION=3.1 =bad", "RESULT=OK VERSION=3.1 BARE BAD=x",
        "RESULT=OK VERSION=3.1 MESSAGE=\"unterminated", "RESULT=OK VERSION=3.1 MESSAGE=\"fine\"suffix",
        "RESULT=OK VERSION=3.1 \x01", "RESULT=OK VERSION=3.1 \x7f", "wrong-prefix");
    sam_key_file key;
    fake_sam bridge;
    bridge.fail_at = "hello";
    bridge.failure = reply;
    bridge.split = true;
    bridge.start(key.file);
    REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
    CHECK( ! bridge.session->ready());
    bridge.finish();
    CHECK(bridge.run_done);
    CHECK(bridge.exceptions.empty());
    CHECK( ! bridge.advanced_after_failure);
}

TEST_CASE("SAM refuses invalid destination data and key file failures", "[sam]") {
    sam_key_file key;
    fake_sam bridge;
    SECTION("empty key is not replaced") {
        std::ofstream output(key.file);
        output.close();
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
        CHECK(bridge.generated == 0);
    }
    SECTION("oversized key file is refused") {
        std::ofstream output(key.file);
        output << std::string(65536, 'A');
        output.close();
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
        CHECK(bridge.generated == 0);
    }
    SECTION("a directory is not a key file") {
        std::filesystem::create_directory(key.file);
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
    }
    SECTION("missing parent refuses private key persistence") {
        bridge.start(key.directory / "missing" / "key");
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
        CHECK(bridge.generated == 1);
    }
    SECTION("generated public destination must match its private key") {
        bridge.destination[0] = 2;
        bridge.public_key = i2p_base64(bridge.destination);
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
        CHECK( ! std::filesystem::exists(key.file));
    }
    SECTION("private destination needs private bytes") {
        bridge.private_key = bridge.public_key;
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
    }
    SECTION("truncated destination certificate is refused") {
        bridge.destination[385] = 0xff;
        bridge.public_key = i2p_base64(bridge.destination);
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
    }
    SECTION("I2P base64 alphabet is strict") {
        bridge.private_key[0] = '+';
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
    }
    SECTION("invalid base64 padding is refused") {
        bridge.private_key = "A===";
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
    }
    SECTION("noncanonical base64 padding bits are refused") {
        REQUIRE(bridge.private_key.ends_with("=="));
        bridge.private_key[bridge.private_key.size() - 3] = 'h';
        bridge.start(key.file);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
    }
    SECTION("transient status requires its generated destination") {
        bridge.fail_at = "session";
        bridge.failure = "RESULT=OK";
        bridge.start(key.file, false);
        REQUIRE(bridge.wait([&] { return bridge.connections_done > 0; }));
        CHECK( ! bridge.session->ready());
    }
    bridge.finish();
    CHECK(bridge.run_done);
    CHECK(bridge.exceptions.empty());
}

TEST_CASE("SAM refuses invalid inbound destination lines", "[sam]") {
    auto const input = GENERATE("", "AAAA", "STREAM STATUS RESULT=I2P_ERROR", "AAAA+===",
        "silent", "truncated", "oversized");
    sam_key_file key;
    fake_sam bridge;
    bridge.peer_line = input;
    bridge.split = true;
    bridge.start(key.file, true, bridge.peer_line == "silent" ? 100ms : 5000ms);
    REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
    auto const result = bridge.run(bridge.session->accept());
    REQUIRE( ! result);
    if (bridge.peer_line == "silent") CHECK(result.error() == error::channel_timeout);
    else CHECK(result.error() != error::channel_timeout);
}

TEST_CASE("SAM invalid session status recreates a control connection that is still open", "[sam]") {
    auto const stage = GENERATE("connect", "accept");
    auto const result = GENERATE("INVALID_ID", "I2P_ERROR");
    sam_key_file key;
    fake_sam bridge;
    bridge.start(key.file);
    REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
    auto const own = bridge.session->local_address();
    bridge.fail_at = stage;
    bridge.failure = std::string("RESULT=") + result;
    if (bridge.fail_at == "accept") CHECK( ! bridge.run(bridge.session->accept()));
    else CHECK( ! bridge.run(bridge.session->connect(bridge.peer().to_hostname())));
    bridge.fail_at.clear();
    REQUIRE(bridge.wait([&] { return bridge.controls >= 2 && bridge.session->ready(); }, 3s));
    CHECK(bridge.generated == 1);
    CHECK(bridge.session->local_address() == own);
}

TEST_CASE("SAM validates bridge endpoints and rejects use before readiness or after stop", "[sam]") {
    sam_key_file key;
    fake_sam bridge;
    bridge.session = std::make_unique<sam_session>(bridge.ctx.get_executor(), bridge.bridge(), key.file, true, 100ms);
    CHECK( ! bridge.run(bridge.session->connect(bridge.peer().to_hostname())));
    CHECK( ! bridge.run(bridge.session->connect("example.invalid")));
    CHECK( ! bridge.run(bridge.session->accept()));
    bridge.session->stop();
    bridge.session->stop();
    CHECK( ! bridge.run(bridge.session->connect(bridge.peer().to_hostname())));
    CHECK( ! bridge.run(bridge.session->accept()));
    CHECK( ! bridge.session->ready());
}

TEST_CASE("SAM run is admitted once", "[sam]") {
    sam_key_file key;
    fake_sam bridge;
    bridge.start(key.file);
    REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
    auto second = std::make_shared<sam_completion<void>>();
    ::asio::co_spawn(bridge.ctx, bridge.session->run(), [second](std::exception_ptr e) {
        second->exception = e;
        second->done = true;
    });
    REQUIRE(bridge.wait([&] { return second->done; }));
    CHECK( ! second->exception);
    CHECK(bridge.controls == 1);
    CHECK(bridge.session->ready());
}

TEST_CASE("SAM retries a lost control session with the same key and stops promptly", "[sam]") {
    sam_key_file key;
    fake_sam bridge;
    bridge.start(key.file);
    REQUIRE(bridge.wait([&] { return bridge.session->ready(); }));
    auto const own = bridge.session->local_address();
    std::error_code ignored;
    bridge.sockets.front()->close(ignored);
    REQUIRE(bridge.wait([&] { return bridge.controls >= 2 && bridge.session->ready(); }, 3s));
    CHECK(bridge.generated == 1);
    CHECK(bridge.session->local_address() == own);
    bridge.finish();
    CHECK(bridge.run_done);
    CHECK(bridge.exceptions.empty());
}
