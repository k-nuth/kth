// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "child_process_harness.hpp"

#include <chrono>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/as_tuple.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <kth/domain/message/messages.hpp>
#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/timer.hpp>
#include <kth/node/detail/p2p_node_test_seam.hpp>
#include <kth/node/p2p_node.hpp>

using namespace kth;
using namespace kth::network;
using namespace kth::node;
using namespace kth::test;
using namespace std::chrono_literals;

namespace {

using seam = node::detail::p2p_node_test_seam;

template <typename T>
struct completion {
    T value{};
    bool done{false};
    std::exception_ptr exception;
};

template <typename Predicate>
bool pump_until(::asio::io_context& ctx, Predicate predicate, std::chrono::milliseconds timeout = 5s) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while ( ! predicate() && std::chrono::steady_clock::now() < deadline) {
        ctx.restart();
        ctx.run_for(2ms);
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

network::settings admission_settings(size_t limit) {
    network::settings settings(domain::config::network::regtest);
    settings.inbound_port = 0;
    settings.inbound_connections = uint32_t(limit);
    settings.outbound_connections = 0;
    settings.host_pool_capacity = 0;
    settings.threads = 1;
    settings.channel_handshake_seconds = 30;
    settings.peers_file.clear();
    settings.hosts_file.clear();
    settings.banlist_file.clear();
    settings.seeds.clear();
    return settings;
}

// The supervisor owns the shared admission decision after TCP or SAM hands it
// a stream. Keep both client ends open, so a pending handshake retains its slot.
struct admission_node {
    network::settings settings;
    p2p_node node;
    ::asio::io_context ctx;
    ::asio::ip::tcp::acceptor listener{ctx, {::asio::ip::address_v4::loopback(), 0}};
    std::vector<std::shared_ptr<::asio::ip::tcp::socket>> clients;
    bool supervisor_done{false};
    bool supervisor_started{false};
    std::exception_ptr supervisor_exception;

    explicit admission_node(size_t limit)
        : settings(admission_settings(limit)), node(settings)
    {}

    void start() {
        auto state = std::make_shared<completion<code>>();
        ::asio::co_spawn(ctx, [this, state]() -> ::asio::awaitable<void> {
            state->value = co_await node.start();
        }, [state](std::exception_ptr e) { state->exception = e; state->done = true; });
        REQUIRE(pump_until(ctx, [&] { return state->done; }));
        REQUIRE( ! state->exception);
        REQUIRE(state->value == error::success);
        supervisor_started = true;
        ::asio::co_spawn(ctx, seam::supervise(node), [this](std::exception_ptr e) {
            supervisor_exception = e;
            supervisor_done = true;
        });
        REQUIRE(pump_until(ctx, [&] { return seam::supervisor_ready(node); }));
    }

    peer_session::ptr make_peer(bool overlay) {
        auto client = std::make_shared<::asio::ip::tcp::socket>(ctx);
        clients.push_back(client);
        std::optional<infrastructure::config::endpoint> destination;
        if (overlay) destination.emplace(std::string(52, 'a') + ".b32.i2p", 0);
        auto state = std::make_shared<completion<peer_session::ptr>>();
        auto const endpoint = listener.local_endpoint();
        REQUIRE(endpoint.port() != 0);
        ::asio::co_spawn(ctx, [this, client, endpoint, destination, state]() -> ::asio::awaitable<void> {
            co_await client->async_connect(endpoint, ::asio::use_awaitable);
            auto server = co_await listener.async_accept(::asio::use_awaitable);
            state->value = std::make_shared<peer_session>(std::move(server), settings, true, destination);
        }, [state](std::exception_ptr e) { state->exception = e; state->done = true; });
        REQUIRE(pump_until(ctx, [&] { return state->done; }));
        REQUIRE( ! state->exception);
        REQUIRE(state->value);
        return state->value;
    }

    void submit(peer_session::ptr const& peer) {
        auto state = std::make_shared<completion<bool>>();
        ::asio::co_spawn(ctx, seam::submit(node, peer, peer_direction::inbound),
            [state](std::exception_ptr e) { state->exception = e; state->done = true; });
        REQUIRE(pump_until(ctx, [&] { return state->done; }));
        REQUIRE( ! state->exception);
    }

    void finish() {
        node.stop();
        for (auto const& client : clients) {
            std::error_code ignored;
            client->close(ignored);
        }
        if (supervisor_started) {
            REQUIRE(pump_until(ctx, [&] { return supervisor_done; }));
            REQUIRE( ! supervisor_exception);
        }
        node.join();
        ctx.restart();
        ctx.poll();
    }

    ~admission_node() {
        node.stop();
        for (auto const& client : clients) {
            std::error_code ignored;
            client->close(ignored);
        }
        if (supervisor_started) pump_until(ctx, [&] { return supervisor_done; });
        node.join();
    }
};

struct node_sam_bridge {
    ::asio::io_context& ctx;
    ::asio::ip::tcp::acceptor listener;
    ::asio::steady_timer lifetime;
    std::vector<std::shared_ptr<::asio::ip::tcp::socket>> sockets;
    std::vector<std::exception_ptr> exceptions;
    bool available{false};
    bool stopped{false};
    bool serve_done{false};
    bool timeout_done{false};
    size_t active{0};
    size_t unavailable{0};
    size_t controls{0};
    size_t outbound_attempts{0};
    size_t inbound_attempts{0};
    bool banned_refused{false};
    bool duplicate_refused{false};
    bool outbound_advertised{false};
    bool inbound_advertised{false};
    bool pong_after_announcements{false};
    bool send_duplicate{false};
    data_chunk destination = data_chunk(391, 0);
    std::string public_key;
    std::string private_key;
    uint32_t const protocol = domain::message::version::level::maximum;
    uint32_t const magic;

    explicit node_sam_bridge(::asio::io_context& context, uint32_t magic_)
        : ctx(context)
        , listener(ctx, {::asio::ip::address_v4::loopback(), 0})
        , lifetime(ctx)
        , magic(magic_)
    {
        destination[384] = 5;
        destination[386] = 4;
        destination[388] = 7;
        public_key = base64(destination);
        auto secret = destination;
        secret.insert(secret.end(), 288, 0x4a);
        private_key = base64(secret);
        ::asio::co_spawn(ctx, serve(), [this](std::exception_ptr e) {
            if (e) exceptions.push_back(e);
            serve_done = true;
        });
        // Closing the sockets bounds every bridge read, including a missing BCH
        // response. The child watchdog also covers the node's eventual join.
        lifetime.expires_after(30s);
        ::asio::co_spawn(ctx, [this]() -> ::asio::awaitable<void> {
            auto [ec] = co_await lifetime.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if ( ! ec) close();
        }, [this](std::exception_ptr e) {
            if (e) exceptions.push_back(e);
            timeout_done = true;
        });
    }

    static std::string base64(data_chunk const& bytes) {
        auto text = encode_base64(bytes);
        for (auto& c : text) {
            if (c == '+') c = '-';
            else if (c == '/') c = '~';
        }
        return text;
    }

    data_chunk peer_destination(uint8_t identity) const {
        auto bytes = destination;
        bytes[0] = identity;
        return bytes;
    }

    infrastructure::config::authority address(uint8_t identity) const {
        return infrastructure::message::network_address(0, 0,
            infrastructure::message::address_network::i2p, sha256_hash(peer_destination(identity)), 0);
    }

    std::string bridge() const {
        return "127.0.0.1:" + std::to_string(listener.local_endpoint().port());
    }

    ::asio::awaitable<std::string> line(::asio::ip::tcp::socket& socket) {
        std::string text;
        while (text.size() < 65536) {
            char c;
            co_await ::asio::async_read(socket, ::asio::buffer(&c, 1), ::asio::use_awaitable);
            if (c == '\n') co_return text;
            text.push_back(c);
        }
        throw std::runtime_error("Oversized SAM command");
    }

    ::asio::awaitable<void> text(::asio::ip::tcp::socket& socket, std::string message) {
        co_await ::asio::async_write(socket, ::asio::buffer(message), ::asio::use_awaitable);
    }

    template <typename Message>
    ::asio::awaitable<void> send(::asio::ip::tcp::socket& socket, Message message) {
        auto const bytes = domain::message::serialize(protocol, message, magic);
        co_await ::asio::async_write(socket, ::asio::buffer(bytes), ::asio::use_awaitable);
    }

    ::asio::awaitable<void> bch_peer(::asio::ip::tcp::socket& socket, bool inbound) {
        domain::message::version version;
        version.set_value(protocol);
        version.set_services(domain::message::version::service::node_network);
        version.set_timestamp(uint64_t(zulu_time()));
        version.set_nonce(inbound ? 123 : 456);
        version.set_user_agent(std::string("/I2P-test:1/"));
        version.set_relay(true);
        co_await send(socket, version);
        co_await send(socket, domain::message::send_addrv2{});
        co_await send(socket, domain::message::verack{});
        bool learned_sent = false;
        for (;;) {
            data_chunk header(domain::message::heading::satoshi_fixed_size());
            co_await ::asio::async_read(socket, ::asio::buffer(header), ::asio::use_awaitable);
            byte_reader header_reader(header);
            auto const heading = domain::message::heading::from_data(header_reader, protocol);
            if ( ! heading || heading->magic() != magic || heading->payload_size() > 65536) {
                throw std::runtime_error("Invalid BCH heading over SAM stream");
            }
            data_chunk payload(heading->payload_size());
            if ( ! payload.empty()) co_await ::asio::async_read(socket, ::asio::buffer(payload), ::asio::use_awaitable);
            if (heading->command() == domain::message::addrv2::command) {
                byte_reader reader(payload);
                auto const addresses = domain::message::addrv2::from_data(reader, protocol);
                if ( ! addresses) throw std::runtime_error("Invalid I2P self advertisement");
                for (auto const& entry : addresses->addresses()) {
                    auto const converted = entry.to_network_address();
                    if (converted && infrastructure::config::authority(*converted) == address(0)) {
                        if (inbound) inbound_advertised = true;
                        else outbound_advertised = true;
                    }
                }
                if ( ! learned_sent) {
                    auto learned = address(4).to_network_address();
                    learned.set_timestamp(uint32_t(zulu_time()));
                    learned.set_services(1);
                    auto const advertisement = domain::message::addrv2::create({
                        domain::message::addrv2_entry::from_network_address(learned)});
                    if ( ! advertisement) throw std::runtime_error("Cannot create test advertisement");
                    for (size_t i = 0; i < 12; ++i) co_await send(socket, *advertisement);
                    learned = address(5).to_network_address();
                    learned.set_timestamp(uint32_t(zulu_time()));
                    learned.set_services(1);
                    auto const last = domain::message::addrv2::create({
                        domain::message::addrv2_entry::from_network_address(learned)});
                    if ( ! last) throw std::runtime_error("Cannot create final test advertisement");
                    co_await send(socket, *last);
                    co_await send(socket, domain::message::ping{789});
                    learned_sent = true;
                }
            } else if (heading->command() == domain::message::pong::command) {
                byte_reader reader(payload);
                auto const pong = domain::message::pong::from_data(reader, protocol);
                if (pong && pong->nonce() == 789) pong_after_announcements = true;
            }
        }
    }

    ::asio::awaitable<void> connection(std::shared_ptr<::asio::ip::tcp::socket> socket) {
        try {
            if (co_await line(*socket) != "HELLO VERSION MIN=3.1 MAX=3.1") throw std::runtime_error("Missing SAM hello");
            if ( ! available) {
                ++unavailable;
                co_await text(*socket, "HELLO REPLY RESULT=I2P_ERROR\n");
                std::error_code ignored;
                socket->shutdown(::asio::ip::tcp::socket::shutdown_send, ignored);
                co_return;
            }
            co_await text(*socket, "HELLO REPLY RESULT=OK VERSION=3.1\n");
            auto command = co_await line(*socket);
            if (command == "DEST GENERATE SIGNATURE_TYPE=7") {
                co_await text(*socket, "DEST REPLY PUB=" + public_key + " PRIV=" + private_key + "\n");
                command = co_await line(*socket);
            }
            if (command.starts_with("SESSION CREATE STYLE=STREAM ID=")) {
                ++controls;
                co_await text(*socket, "SESSION STATUS RESULT=OK DESTINATION=" + private_key + "\n");
                char c;
                co_await socket->async_read_some(::asio::buffer(&c, 1), ::asio::use_awaitable);
            } else if (command == "NAMING LOOKUP NAME=" + address(1).to_hostname()) {
                co_await text(*socket, "NAMING REPLY RESULT=OK NAME=" + address(1).to_hostname() +
                    " VALUE=" + base64(peer_destination(1)) + "\n");
                command = co_await line(*socket);
                if ( ! command.starts_with("STREAM CONNECT ID=")) throw std::runtime_error("Missing SAM connect");
                if (++outbound_attempts == 1) {
                    co_await text(*socket, "STREAM STATUS RESULT=INVALID_ID\n");
                    std::error_code ignored;
                    socket->shutdown(::asio::ip::tcp::socket::shutdown_send, ignored);
                } else {
                    co_await text(*socket, "STREAM STATUS RESULT=OK\n");
                    co_await bch_peer(*socket, false);
                }
            } else if (command.starts_with("STREAM ACCEPT ID=")) {
                // Let the retry exercise control recovery before delivering an
                // inbound peer; a lost control session must not count as a ban.
                auto const generation = controls;
                while (controls < 2 && ! stopped) {
                    ::asio::steady_timer pause(ctx);
                    pause.expires_after(10ms);
                    co_await pause.async_wait(::asio::use_awaitable);
                }
                if (generation < 2) co_return;
                auto const index = ++inbound_attempts;
                if (index >= 3) {
                    while (( ! send_duplicate || index > 3) && ! stopped) {
                        ::asio::steady_timer pause(ctx);
                        pause.expires_after(10ms);
                        co_await pause.async_wait(::asio::use_awaitable);
                    }
                }
                if (stopped) co_return;
                co_await text(*socket, "STREAM STATUS RESULT=OK\n" + base64(peer_destination(index == 1 ? 2 : 3)) + "\n");
                if (index == 2) {
                    co_await bch_peer(*socket, true);
                } else {
                    char c;
                    auto [ec, count] = co_await socket->async_read_some(::asio::buffer(&c, 1), ::asio::as_tuple(::asio::use_awaitable));
                    if (ec && count == 0) {
                        if (index == 1) banned_refused = true;
                        if (index == 3) duplicate_refused = true;
                    }
                }
            } else throw std::runtime_error("Unexpected SAM command");
        } catch (std::system_error const&) {
            // Session recovery and fixture shutdown close pending streams.
        }
    }

    ::asio::awaitable<void> serve() {
        while ( ! stopped) {
            auto socket = std::make_shared<::asio::ip::tcp::socket>(ctx);
            auto [ec] = co_await listener.async_accept(*socket, ::asio::as_tuple(::asio::use_awaitable));
            if (ec) co_return;
            sockets.push_back(socket);
            ++active;
            ::asio::co_spawn(ctx, connection(socket), [this](std::exception_ptr e) {
                if (e) exceptions.push_back(e);
                --active;
            });
        }
    }

    void close() {
        stopped = true;
        std::error_code ignored;
        listener.close(ignored);
        lifetime.cancel();
        for (auto const& socket : sockets) socket->close(ignored);
    }

    void finish() {
        close();
        REQUIRE(pump_until(ctx, [&] { return serve_done && timeout_done && active == 0; }));
    }

    ~node_sam_bridge() {
        close();
        pump_until(ctx, [&] { return serve_done && timeout_done && active == 0; });
    }
};

struct node_run_driver {
    struct run_state {
        code start_result{error::unknown};
        code run_result{error::unknown};
        bool started{false};
        bool done{false};
        std::exception_ptr exception;
    };
    p2p_node& node;
    ::asio::io_context& ctx;
    node_sam_bridge& bridge;
    std::shared_ptr<run_state> state = std::make_shared<run_state>();

    node_run_driver(p2p_node& target, ::asio::io_context& context, node_sam_bridge& router)
        : node(target), ctx(context), bridge(router)
    {
        ::asio::co_spawn(ctx, [&target, result = state]() -> ::asio::awaitable<void> {
            result->start_result = co_await target.start();
            result->started = true;
            if (result->start_result == error::success) result->run_result = co_await target.run();
        }, [result = state](std::exception_ptr e) { result->exception = e; result->done = true; });
    }

    void finish() {
        node.stop();
        bridge.close();
        REQUIRE(pump_until(ctx, [&] { return state->done; }));
        REQUIRE( ! state->exception);
        REQUIRE(state->run_result == error::success);
        bridge.finish();
        node.join();
    }

    ~node_run_driver() {
        node.stop();
        bridge.close();
        pump_until(ctx, [&] { return state->done; });
        node.join();
    }
};

} // namespace

TEST_CASE("i2p child: TCP and I2P share inbound admission and release slots", "[.i2p-child]") {
    watchdog guard(45s, "I2P shared inbound admission");
    admission_node fixture(1);
    fixture.start();
    auto tcp = fixture.make_peer(false);
    fixture.submit(tcp);
    REQUIRE(pump_until(fixture.ctx, [&] { return seam::inbound_count(fixture.node) == 1; }));
    auto rejected_i2p = fixture.make_peer(true);
    fixture.submit(rejected_i2p);
    REQUIRE(pump_until(fixture.ctx, [&] { return rejected_i2p->stopped(); }));
    CHECK( ! tcp->stopped());
    CHECK(seam::inbound_count(fixture.node) == 1);

    tcp->stop();
    REQUIRE(pump_until(fixture.ctx, [&] { return seam::inbound_count(fixture.node) == 0; }));
    auto i2p = fixture.make_peer(true);
    fixture.submit(i2p);
    REQUIRE(pump_until(fixture.ctx, [&] { return seam::inbound_count(fixture.node) == 1; }));
    CHECK( ! i2p->stopped());
    CHECK(i2p->authority().is_i2p());
    CHECK(i2p->is_inbound());
    auto rejected_tcp = fixture.make_peer(false);
    fixture.submit(rejected_tcp);
    REQUIRE(pump_until(fixture.ctx, [&] { return rejected_tcp->stopped(); }));
    CHECK( ! i2p->stopped());
    fixture.finish();
    CHECK(seam::inbound_count(fixture.node) == 0);
    announce_completion();
}

TEST_CASE("i2p child: a zero inbound limit refuses TCP and I2P", "[.i2p-child]") {
    watchdog guard(45s, "I2P zero inbound limit");
    admission_node fixture(0);
    fixture.start();
    auto tcp = fixture.make_peer(false);
    auto i2p = fixture.make_peer(true);
    fixture.submit(tcp);
    fixture.submit(i2p);
    REQUIRE(pump_until(fixture.ctx, [&] { return tcp->stopped() && i2p->stopped(); }));
    CHECK(seam::inbound_count(fixture.node) == 0);
    CHECK(fixture.node.connection_count() == 0);
    fixture.finish();
    announce_completion();
}

TEST_CASE("TCP and I2P share bounded inbound admission", "[p2p_node][i2p]") {
    require_clean_child(run_child("i2p child: TCP and I2P share inbound admission and release slots"));
    require_clean_child(run_child("i2p child: a zero inbound limit refuses TCP and I2P"));
}

TEST_CASE("i2p child: SAM recovery reaches manual and inbound peers and advertises the node", "[.i2p-child]") {
    watchdog guard(60s, "I2P node integration");
    auto const directory = claimed_dir("kth_i2p_node");
    {
        ::asio::io_context ctx;
        auto settings = admission_settings(1);
        settings.host_pool_capacity = 100;
        settings.channel_handshake_seconds = 10;
        settings.peers_file = directory / "peers.dat";
        settings.i2p_private_key_file = directory / "i2p_private_key";
        node_sam_bridge bridge(ctx, settings.identifier);
        settings.i2psam = bridge.bridge();
        settings.peers.emplace_back(bridge.address(1));
        p2p_node node(settings);
        auto banned = std::make_shared<peer_session>(::asio::ip::tcp::socket(ctx), settings, true,
            infrastructure::config::endpoint(bridge.address(2)));
        node.ban_peer(banned, 60s);
        node_run_driver driver(node, ctx, bridge);

        REQUIRE(pump_until(ctx, [&] {
            return driver.state->started && seam::supervisor_ready(node) && bridge.unavailable != 0;
        }));
        CHECK(driver.state->start_result == error::success);
        CHECK( ! node.stopped());
        CHECK(node.connection_count() == 0);
        CHECK(bridge.controls == 0);

        bridge.available = true;
        REQUIRE(pump_until(ctx, [&] {
            return bridge.outbound_advertised && bridge.inbound_advertised && node.connection_count() == 2 &&
                bridge.pong_after_announcements && node.peer_db().get(bridge.address(5)).has_value();
        }, 15s));
        CHECK(bridge.controls >= 2);
        CHECK(bridge.outbound_attempts >= 2);
        CHECK(bridge.banned_refused);
        CHECK(node.is_banned(bridge.address(2)));
        REQUIRE(node.peer_db().get(bridge.address(3)));
        CHECK(node.peer_db().get(bridge.address(3))->connection_successes >= 1);
        CHECK(seam::inbound_count(node) == 1);
        CHECK(std::filesystem::exists(settings.i2p_private_key_file));

        auto peers = std::make_shared<completion<std::vector<peer_session::ptr>>>();
        ::asio::co_spawn(ctx, [&node, peers]() -> ::asio::awaitable<void> {
            peers->value = co_await node.peers().all();
        }, [peers](std::exception_ptr e) { peers->exception = e; peers->done = true; });
        REQUIRE(pump_until(ctx, [&] { return peers->done; }));
        REQUIRE( ! peers->exception);
        REQUIRE(peers->value.size() == 2);
        CHECK(std::any_of(peers->value.begin(), peers->value.end(), [&](auto const& peer) {
            return peer->is_inbound() && peer->authority() == bridge.address(3);
        }));
        CHECK(std::any_of(peers->value.begin(), peers->value.end(), [&](auto const& peer) {
            return peer->is_outbound() && peer->authority() == bridge.address(1);
        }));

        bridge.send_duplicate = true;
        REQUIRE(pump_until(ctx, [&] { return bridge.duplicate_refused; }));
        CHECK(node.connection_count() == 2);
        driver.finish();
        CHECK(bridge.exceptions.empty());
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    announce_completion();
}

TEST_CASE("SAM recovery connects manual and inbound peers and exchanges overlay advertisements", "[p2p_node][i2p]") {
    require_clean_child(run_child("i2p child: SAM recovery reaches manual and inbound peers and advertises the node"));
}
