// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The node's side of the SOCKS5 proxy, an optional feature: the settings as the
// parser takes them, and connect() through a proxy.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <test_helpers.hpp>

#include <catch2/generators/catch_generators.hpp>

#include <kth/network.hpp>
#include <kth/node.hpp>
#include <kth/node/p2p_node.hpp>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

using namespace kth;
using namespace kth::network;
using namespace kth::node;
using namespace std::chrono_literals;

namespace {

int process_id() {
#if defined(_WIN32)
    return ::_getpid();
#else
    return ::getpid();
#endif
}

// Writes a flat config file for the duration of a test.
struct config_file {
    explicit config_file(std::string const& tag, std::string const& text)
        : path_(std::filesystem::temp_directory_path() / ("kth_cfg_" + tag + "_" + std::to_string(process_id()) + ".cfg"))
    {
        std::ofstream(path_) << text << "\n";
    }
    ~config_file() { std::error_code ec; std::filesystem::remove(path_, ec); }
    std::filesystem::path const& path() const { return path_; }
private:
    std::filesystem::path path_;
};

template<typename Predicate>
void run_until_p2p(::asio::io_context& ctx, Predicate pred, std::chrono::milliseconds timeout = 1000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while ( ! pred() && std::chrono::steady_clock::now() < deadline) {
        ctx.run_for(10ms);
    }
}

network::settings make_test_settings_p2p() {
    network::settings settings;
    settings.identifier = 0xe8f3e1e3;  // BCH testnet magic
    settings.protocol_maximum = 70015;
    settings.validate_checksum = true;
    settings.channel_inactivity_minutes = 1;
    settings.channel_expiration_minutes = 5;
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.connect_timeout_seconds = 5;
    settings.channel_handshake_seconds = 5;
    // Paths that do not exist: nothing real is loaded or saved.
    settings.hosts_file = std::filesystem::temp_directory_path() / ("kth_test_hosts_nonexistent.dat");
    settings.banlist_file = std::filesystem::temp_directory_path() / ("kth_test_banlist_nonexistent.dat");
    return settings;
}


// The node tests do not link src/network/test. This is the success path of the
// proxy there: method 0x00, then a CONNECT reply, then shutdown of the write
// side. connect() waits for a handshake after the duplicate and ban gates, and
// this proxy does not speak the peer protocol. The shutdown is after the
// counted CONNECT reply, so the SOCKS client still reads that reply and the
// handshake read is the one that sees end-of-file.
struct socks5_success_proxy {
    ::asio::io_context& ctx;
    ::asio::ip::tcp::acceptor listener;
    // accept or negotiate is blocked on this socket, and it is not in `held`
    // yet. close() runs only while serve() is suspended.
    std::shared_ptr<::asio::ip::tcp::socket> current;
    std::vector<::asio::ip::tcp::socket> held;
    std::atomic<int> negotiations{0};
    std::exception_ptr server_exception;
    std::atomic<bool> finished{false};

    explicit socks5_success_proxy(::asio::io_context& context)
        : ctx(context)
        , listener(context, {::asio::ip::address_v4::loopback(), 0})
    {}

    std::string proxy() const {
        return "127.0.0.1:" + std::to_string(listener.local_endpoint().port());
    }

    void start() {
        ::asio::co_spawn(ctx, serve(), [this](std::exception_ptr exception) {
            server_exception = exception;
            finished.store(true);
        });
    }

    void close() {
        std::error_code ignored;
        if (current) {
            current->close(ignored);
        }
        for (auto& socket : held) {
            socket.close(ignored);
        }
        listener.cancel(ignored);
    }

    ::asio::awaitable<bool> read_bytes(::asio::ip::tcp::socket& socket,
        std::vector<uint8_t>& bytes, size_t size) {
        auto const offset = bytes.size();
        bytes.resize(offset + size);
        auto [ec, count] = co_await ::asio::async_read(socket,
            ::asio::buffer(bytes.data() + offset, size), ::asio::as_tuple(::asio::use_awaitable));
        co_return ! ec && count == size;
    }

    ::asio::awaitable<bool> negotiate(::asio::ip::tcp::socket& socket) {
        std::vector<uint8_t> greeting;
        if ( ! co_await read_bytes(socket, greeting, 2)) {
            co_return false;
        }
        if ( ! co_await read_bytes(socket, greeting, greeting[1])) {
            co_return false;
        }
        std::vector<uint8_t> const method{0x05, 0x00};
        auto [method_ec, method_count] = co_await ::asio::async_write(socket,
            ::asio::buffer(method), ::asio::as_tuple(::asio::use_awaitable));
        if (method_ec || method_count != method.size()) {
            co_return false;
        }
        std::vector<uint8_t> request;
        if ( ! co_await read_bytes(socket, request, 4)) {
            co_return false;
        }
        size_t address_size = 0;
        switch (request[3]) {
            case 1:
                address_size = 4;
                break;
            case 4:
                address_size = 16;
                break;
            case 3:
                if ( ! co_await read_bytes(socket, request, 1)) {
                    co_return false;
                }
                address_size = request.back();
                break;
            default:
                co_return false;
        }
        if ( ! co_await read_bytes(socket, request, address_size + 2)) {
            co_return false;
        }
        std::vector<uint8_t> const reply{0x05, 0x00, 0x00, 0x01, 127, 0, 0, 1, 0, 0};
        auto [reply_ec, reply_count] = co_await ::asio::async_write(socket,
            ::asio::buffer(reply), ::asio::as_tuple(::asio::use_awaitable));
        if (reply_ec || reply_count != reply.size()) {
            co_return false;
        }
        std::error_code ignored;
        socket.shutdown(::asio::ip::tcp::socket::shutdown_send, ignored);
        co_return true;
    }

    ::asio::awaitable<void> serve() {
        for (;;) {
            auto accepted = std::make_shared<::asio::ip::tcp::socket>(ctx);
            current = accepted;
            auto [ec] = co_await listener.async_accept(
                *accepted, ::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                co_return;
            }
            if (co_await negotiate(*accepted)) {
                negotiations.fetch_add(1);
            }
            held.push_back(std::move(*accepted));
            current.reset();
        }
    }
};

// Outbound and inbound stay at 0, and seeds and manual peers are cleared, so
// the running node does not dial on its own. An empty peers file keeps a ban
// in memory: save() returns without opening peers.dat. Two seconds is the
// handshake budget for a connect that gets past the gates; the waits below
// are longer than that.
network::settings proxy_connect_settings(std::string const& proxy) {
    auto settings = make_test_settings_p2p();
    settings.outbound_connections = 0;
    settings.inbound_connections = 0;
    settings.inbound_port = 0;
    settings.seeds.clear();
    settings.peers.clear();
    settings.peers_file.clear();
    settings.channel_handshake_seconds = 2;
    settings.proxy = proxy;
    return settings;
}

// What run_inbound() says it decided: true once it listens, false once it says
// it will not. The node's log is the only place that decision shows before a
// connection is tried, and trying one before the decision tells nothing.
struct inbound_decision_sink : spdlog::sinks::base_sink<std::mutex> {
    std::promise<bool> decision;
    bool decided{false};

protected:
    void sink_it_(spdlog::details::log_msg const& message) override {
        if (decided) {
            return;
        }
        std::string_view const text(message.payload.data(), message.payload.size());
        if (text.find("[p2p_node] Listening on port") != std::string_view::npos) {
            decided = true;
            decision.set_value(true);
        } else if (text.find("[p2p_node] Inbound connections disabled") != std::string_view::npos) {
            decided = true;
            decision.set_value(false);
        }
    }

    void flush_() override {}
};

struct inbound_decision_log {
    std::shared_ptr<inbound_decision_sink> sink{std::make_shared<inbound_decision_sink>()};
    std::shared_ptr<spdlog::logger> previous{spdlog::default_logger()};

    inbound_decision_log() {
        auto logger = std::make_shared<spdlog::logger>("inbound_decision", sink);
        logger->set_level(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    }

    ~inbound_decision_log() {
        spdlog::set_default_logger(previous);
    }

    inbound_decision_log(inbound_decision_log const&) = delete;
    inbound_decision_log& operator=(inbound_decision_log const&) = delete;
};

uint16_t free_port() {
    ::asio::io_context ctx;
    ::asio::ip::tcp::acceptor probe(ctx, {::asio::ip::address_v4::loopback(), 0});
    return probe.local_endpoint().port();
}

bool accepts_connections(uint16_t port) {
    ::asio::io_context ctx;
    ::asio::ip::tcp::socket socket(ctx);
    std::error_code ec;
    socket.connect({::asio::ip::address_v4::loopback(), port}, ec);
    return ! ec;
}

void finish_proxy(socks5_success_proxy& proxy, ::asio::io_context& ctx) {
    proxy.close();
    run_until_p2p(ctx, [&]{ return proxy.finished.load(); }, 1000ms);
}

} // namespace
TEST_CASE("outbound proxy settings retain defaults and explicit command line values", "[configuration tests][parser][socks5]") {
    node::parser defaults(domain::config::network::regtest);
    CHECK(defaults.configured.network.proxy.empty());
    CHECK(defaults.configured.network.proxy_randomize);

    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "regtest", "--net.proxy", "[::1]:9050",
        "--net.proxy_randomize", "false"};
    std::ostringstream error;
    REQUIRE(parser.parse(7, argv, error));
    CHECK(parser.configured.network.proxy == "[::1]:9050");
    CHECK_FALSE(parser.configured.network.proxy_randomize);
}

TEST_CASE("WebSocket discovery is refused behind a proxy", "[configuration tests][parser][socks5]") {
    // It resolves the names peers announce with the system's resolver.
    node::parser parser(domain::config::network::regtest);
    char const* argv[]{"kth", "--network", "regtest", "--net.proxy", "127.0.0.1:9050",
        "--net.ws_discovery", "true"};
    std::ostringstream error;
    CHECK_FALSE(parser.parse(7, argv, error));
    CHECK(error.str().find("net.ws_discovery is not allowed with net.proxy") != std::string::npos);

    node::parser alone(domain::config::network::regtest);
    char const* only_discovery[]{"kth", "--network", "regtest", "--net.ws_discovery", "true"};
    std::ostringstream none;
    CHECK(alone.parse(5, only_discovery, none));
}

TEST_CASE("outbound proxy settings load from dotted config keys and reject invalid booleans", "[configuration tests][parser][socks5]") {
    config_file file("proxy", "net.proxy=127.0.0.1:9050\nnet.proxy_randomize=false");
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parser.parse_from_file(file.path(), error));
    CHECK(parser.configured.network.proxy == "127.0.0.1:9050");
    CHECK_FALSE(parser.configured.network.proxy_randomize);

    node::parser invalid(domain::config::network::regtest);
    char const* argv[]{"kth", "--net.proxy_randomize", "sometimes"};
    CHECK_FALSE(invalid.parse(3, argv, error));
}

TEST_CASE("p2p_node connect rejects a banned numeric destination", "[p2p_node][socks5]") {
    ::asio::io_context ctx;
    socks5_success_proxy proxy(ctx);
    auto const settings = proxy_connect_settings(proxy.proxy());
    p2p_node node(settings);
    proxy.start();

    std::atomic<bool> started{false};
    code start_result = error::unknown;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        start_result = co_await node.start();
        started = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return started.load(); }, 2000ms);

    auto const banned = infrastructure::config::authority::parse_from("192.0.2.1", 8333);
    if (banned) {
        node.peer_db().ban(*banned, 60s);
    }

    std::atomic<bool> done{false};
    int negotiations = 0;
    std::expected<peer_session::ptr, code> connect_result;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        connect_result = co_await node.connect("192.0.2.1", 8333);
        negotiations = proxy.negotiations.load();
        done = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return done.load(); }, 4000ms);

    node.stop();
    finish_proxy(proxy, ctx);

    REQUIRE(started);
    REQUIRE(start_result == error::success);
    REQUIRE(node.stopped());
    REQUIRE(proxy.finished.load());
    REQUIRE( ! proxy.server_exception);
    REQUIRE(banned.has_value());
    REQUIRE(node.is_banned(*banned));
    REQUIRE(done);
    // address_blocked follows a completed SOCKS negotiation, so the count is
    // sampled with the result.
    REQUIRE(negotiations >= 1);
    REQUIRE( ! connect_result.has_value());
    REQUIRE(connect_result.error() == error::address_blocked);
}

TEST_CASE("p2p_node connect rejects a numeric destination already in the manager", "[p2p_node][socks5]") {
    ::asio::io_context ctx;
    socks5_success_proxy proxy(ctx);
    auto const settings = proxy_connect_settings(proxy.proxy());
    p2p_node node(settings);
    proxy.start();

    std::atomic<bool> started{false};
    code start_result = error::unknown;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        start_result = co_await node.start();
        started = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return started.load(); }, 2000ms);

    std::atomic<bool> done{false};
    bool numeric = false;
    code add_result = error::unknown;
    size_t admitted = 0;
    int negotiations = 0;
    std::expected<peer_session::ptr, code> connect_result;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        auto session = std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, false,
            infrastructure::config::endpoint("192.0.2.2", 8333));
        numeric = session->has_numeric_authority();
        add_result = co_await node.peers().add(session);
        admitted = node.connection_count();
        connect_result = co_await node.connect("192.0.2.2", 8333);
        negotiations = proxy.negotiations.load();
        done = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return done.load(); }, 4000ms);

    node.stop();
    finish_proxy(proxy, ctx);

    REQUIRE(started);
    REQUIRE(start_result == error::success);
    REQUIRE(node.stopped());
    REQUIRE(proxy.finished.load());
    REQUIRE( ! proxy.server_exception);
    REQUIRE(done);
    REQUIRE(numeric);
    REQUIRE(add_result == error::success);
    REQUIRE(admitted == 1);
    // A literal IP already in the manager is refused before any dial, so the
    // proxy is not asked for it.
    REQUIRE(negotiations == 0);
    REQUIRE( ! connect_result.has_value());
    REQUIRE(connect_result.error() == error::address_in_use);
}

TEST_CASE("p2p_node connect rejects a duplicate hostname and still dials when the unspecified address is banned", "[p2p_node][socks5]") {
    ::asio::io_context ctx;
    socks5_success_proxy proxy(ctx);
    auto const settings = proxy_connect_settings(proxy.proxy());
    p2p_node node(settings);
    proxy.start();

    // The gates return before the supervisor. A hostname that is not a
    // duplicate still waits there for a handshake result, so the supervisor
    // has to be running or that wait never ends. The proxy closes its write
    // side so the handshake read ends.
    std::atomic<bool> started{false};
    std::atomic<bool> run_returned{false};
    code start_result = error::unknown;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        start_result = co_await node.start();
        started = true;
        if (start_result == error::success) {
            co_await node.run();
        }
        run_returned = true;
    }, ::asio::detached);
    run_until_p2p(ctx, [&]{ return started.load(); }, 2000ms);

    std::atomic<bool> done{false};
    size_t count_before = 99;
    int first_negotiations = 0;
    bool hostname = false;
    code add_result = error::unknown;
    size_t admitted = 0;
    int second_negotiations = 0;
    bool unspecified_banned = false;
    int third_negotiations = 0;
    std::expected<peer_session::ptr, code> first;
    std::expected<peer_session::ptr, code> second;
    std::expected<peer_session::ptr, code> third;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        constexpr auto host = "duplicate-peer.invalid";
        count_before = node.connection_count();
        first = co_await node.connect(host, 8333);
        first_negotiations = proxy.negotiations.load();

        // connect() returns the session only after a handshake, which this
        // proxy cannot complete. The duplicate gate compares canonical
        // destinations, so admit a session for the name just requested.
        auto session = std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, false,
            infrastructure::config::endpoint(host, 8333));
        hostname = ! session->has_numeric_authority()
            && session->destination().host() == std::string(host);
        add_result = co_await node.peers().add(session);
        admitted = node.connection_count();
        second = co_await node.connect(host, 8333);
        second_negotiations = proxy.negotiations.load();

        node.peer_db().ban(infrastructure::config::authority::any(), 60s);
        unspecified_banned = node.is_banned(infrastructure::config::authority::any());
        third = co_await node.connect("other-peer.invalid", 8333);
        third_negotiations = proxy.negotiations.load();
        done = true;
    }, ::asio::detached);
    // Two handshakes, each bounded by channel_handshake_seconds above.
    run_until_p2p(ctx, [&]{ return done.load(); }, 8000ms);

    node.stop();
    // stop() leaves the outbound idle timer armed. run() returns when that
    // timer ends. Pump until then, so the destructor's join finds the pool idle.
    run_until_p2p(ctx, [&]{ return run_returned.load(); }, 8000ms);
    finish_proxy(proxy, ctx);

    REQUIRE(started);
    REQUIRE(start_result == error::success);
    REQUIRE(node.stopped());
    REQUIRE(run_returned);
    REQUIRE(proxy.finished.load());
    REQUIRE( ! proxy.server_exception);
    REQUIRE(done);

    REQUIRE(count_before == 0);
    REQUIRE(first_negotiations >= 1);
    REQUIRE( ! first.has_value());
    REQUIRE(first.error() != error::address_in_use);

    REQUIRE(hostname);
    REQUIRE(add_result == error::success);
    REQUIRE(admitted == 1);
    REQUIRE(second_negotiations >= 2);
    REQUIRE( ! second.has_value());
    REQUIRE(second.error() == error::address_in_use);

    REQUIRE(unspecified_banned);
    REQUIRE(third_negotiations >= 3);
    REQUIRE( ! third.has_value());
    REQUIRE(third.error() != error::address_blocked);
    REQUIRE(third.error() != error::address_in_use);
}

// A listener answers on the node's own address, the one a proxy is there to
// keep from its peers.
TEST_CASE("a node behind a proxy does not listen for inbound connections", "[p2p_node][socks5]") {
    bool const behind_proxy = GENERATE(true, false);
    CAPTURE(behind_proxy);

    auto settings = proxy_connect_settings(behind_proxy ? "127.0.0.1:9050" : "");
    settings.inbound_port = free_port();
    settings.inbound_connections = 8;

    inbound_decision_log log;
    auto decision = log.sink->decision.get_future();

    bool listens = false;
    bool reachable = false;
    bool decided = false;
    bool stopped_cleanly = false;
    {
        p2p_node node(settings);
        auto started = ::asio::co_spawn(node.thread_pool().get_executor(),
            [&node]() -> ::asio::awaitable<int> {
                auto const ec = co_await node.start();
                co_return ec.value();
            }, ::asio::use_future);
        REQUIRE(started.wait_for(30s) == std::future_status::ready);
        REQUIRE(started.get() == 0);

        auto running = ::asio::co_spawn(node.thread_pool().get_executor(),
            [&node]() -> ::asio::awaitable<int> {
                auto const ec = co_await node.run();
                co_return ec.value();
            }, ::asio::use_future);

        decided = decision.wait_for(30s) == std::future_status::ready;
        if (decided) {
            listens = decision.get();
            reachable = accepts_connections(settings.inbound_port);
        }

        node.stop();
        stopped_cleanly = running.wait_for(60s) == std::future_status::ready;
    }

    REQUIRE(decided);
    CHECK(stopped_cleanly);
    CHECK(listens == ! behind_proxy);
    CHECK(reachable == ! behind_proxy);
}

// The parser refuses the pair, and settings built without it reach run().
TEST_CASE("a node behind a proxy does not run with WebSocket discovery", "[p2p_node][socks5]") {
    auto settings = proxy_connect_settings("127.0.0.1:9050");
    settings.ws_discovery = true;

    p2p_node node(settings);
    auto started = ::asio::co_spawn(node.thread_pool().get_executor(),
        [&node]() -> ::asio::awaitable<int> {
            auto const ec = co_await node.start();
            co_return ec.value();
        }, ::asio::use_future);
    REQUIRE(started.wait_for(30s) == std::future_status::ready);
    REQUIRE(started.get() == 0);

    auto running = ::asio::co_spawn(node.thread_pool().get_executor(),
        [&node]() -> ::asio::awaitable<code> {
            co_return co_await node.run();
        }, ::asio::use_future);
    REQUIRE(running.wait_for(30s) == std::future_status::ready);
    CHECK(running.get() == error::operation_failed);
    node.stop();
}
