// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "sync_harness.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <kth/domain/message/get_headers.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/verack.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/network/settings.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sync/orchestrator.hpp>

using namespace kth;
using namespace kth::node;
using namespace kth::node::sync;
using namespace kth::test;

// =============================================================================
// #710: an announcement read as the answer to a getheaders
// =============================================================================
//
// The defect, as demonstrated in #710: a `headers` carries nothing that says
// which `getheaders` it answers, and pairing them by state lets an announcement
// be taken as the answer to an outstanding request. The request completes on
// it, a second getheaders goes out on the same session while the first is
// still unanswered, the first's real answer is taken as the second's, and the
// second's real answer is read as an announcement — its headers thrown away
// and asked for again.
//
// The demonstration in #710 drove `network::request_headers` and the
// session's claim mark directly. Both are what the fix removes, so the same
// sequence is driven here through the boundary that exists before and after:
// a node built from `p2p_node` and `sync_orchestrator`, and a remote peer
// speaking the protocol over a real TCP connection, accepted by the node as an
// inbound peer. Nothing below reaches into the node: what is asserted is what
// the node does with what the peer sends, observed on the wire and in the
// header index.
//
// The sequence, all at the peer's pace:
//   1. the node sends getheaders #1, from tip 5;
//   2. block 6 is mined, and the peer announces it: `headers [6]`;
//   3. the node sends getheaders #2, from 6;
//   4. the peer answers #1 — `headers [6]`, all it had then — and then #2:
//      block 7 has been mined meanwhile, so `headers [7]`.
//
// What the peer sent is 6 and 7. A node that takes every `headers` as the fact
// it is has 7 in its index after step 4 and has no reason to ask for it again.
// A node that paired #1's answer with request #2 read [7] as an announcement:
// it does not have 7, and asks the peer again from 6.

namespace {

constexpr uint32_t regtest_magic = 0xdab5bffa;
constexpr uint32_t wire_version = 70015;

// Written out when it goes, whether the case passed or a REQUIRE ended it: a
// failure is unreadable without what the node said.
struct captured_log {
    std::string name;
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
    std::shared_ptr<spdlog::logger> installed;
    std::shared_ptr<spdlog::logger> previous;

    explicit captured_log(std::string tag)
        : name(std::move(tag))
        , sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(262144))
        , installed(std::make_shared<spdlog::logger>("captured", sink))
        , previous(spdlog::default_logger()) {
        installed->set_level(spdlog::level::trace);
        spdlog::set_default_logger(installed);
    }

    ~captured_log() {
        spdlog::set_default_logger(previous);
        dump();
    }

    captured_log(captured_log const&) = delete;
    captured_log& operator=(captured_log const&) = delete;

    void dump() const {
        auto const path = std::filesystem::temp_directory_path() / ("kth_" + name + "_dump.log");
        std::ofstream out(path, std::ios::trunc);
        for (auto const& line : sink->last_formatted()) {
            out << line;
        }
    }
};

uint16_t free_port() {
    ::asio::io_context ioc;
    ::asio::ip::tcp::acceptor probe(ioc, {::asio::ip::address_v4::loopback(), 0});
    return probe.local_endpoint().port();
}

network::settings node_settings(uint16_t port, std::string const& tag) {
    network::settings settings;
    settings.identifier = regtest_magic;
    settings.protocol_maximum = wire_version;
    settings.inbound_port = port;
    settings.inbound_connections = 8;
    settings.outbound_connections = 0;
    settings.seeds.clear();
    settings.threads = 2;
    settings.hosts_file = "/tmp/kth_" + tag + "_hosts_nonexistent.dat";
    settings.banlist_file = "/tmp/kth_" + tag + "_banlist_nonexistent.dat";
    auto const peers = std::filesystem::temp_directory_path() / ("kth_" + tag + "_peers.dat");
    std::filesystem::remove(peers);
    settings.peers_file = peers;
    return settings;
}

// The other end of the connection: a peer that speaks the protocol by hand,
// with blocking reads bounded by a deadline. Never run on any io_context.
struct remote_peer {
    ::asio::io_context ioc;
    ::asio::ip::tcp::socket socket{ioc};

    bool connect(uint16_t port, std::chrono::milliseconds limit) {
        auto const deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            std::error_code ec;
            socket.connect({::asio::ip::address_v4::loopback(), port}, ec);
            if ( ! ec) return true;
            socket.close(ec);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }

    ~remote_peer() {
        std::error_code ignored;
        socket.close(ignored);
    }

    template <typename Message>
    void send(Message const& message) {
        auto const bytes = domain::message::serialize(wire_version, message, regtest_magic);
        ::asio::write(socket, ::asio::buffer(bytes));
    }

    struct message {
        std::string command;
        data_chunk payload;
    };

    bool read_exact(data_chunk& out, std::chrono::steady_clock::time_point deadline) {
        while (socket.available() < out.size()) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ::asio::read(socket, ::asio::buffer(out));
        return true;
    }

    std::optional<message> next(std::chrono::steady_clock::time_point deadline) {
        data_chunk heading(24);
        if ( ! read_exact(heading, deadline)) return std::nullopt;
        char command[13] = {};
        std::memcpy(command, heading.data() + 4, 12);
        uint32_t length = 0;
        std::memcpy(&length, heading.data() + 16, 4);
        data_chunk payload(length);
        if (length > 0 && ! read_exact(payload, deadline)) return std::nullopt;
        return message{command, std::move(payload)};
    }

    // Version both ways, verack both ways.
    bool handshake(uint32_t start_height, std::chrono::milliseconds limit) {
        domain::message::version version;
        version.set_value(wire_version);
        version.set_services(domain::message::version::service::node_network);
        version.set_timestamp(uint64_t(zulu_time()));
        version.set_nonce(0x7100710071007100ull);
        version.set_user_agent("/kth-test:710/");
        version.set_start_height(start_height);
        version.set_relay(true);
        send(version);

        auto const deadline = std::chrono::steady_clock::now() + limit;
        bool got_version = false;
        bool got_verack = false;
        while ( ! got_version || ! got_verack) {
            auto received = next(deadline);
            if ( ! received) return false;
            if (received->command == domain::message::version::command) {
                got_version = true;
                send(domain::message::verack{});
            } else if (received->command == domain::message::verack::command) {
                got_verack = true;
            }
        }
        return true;
    }

    // The first locator hash of the next getheaders the node sends, skipping
    // everything else it sends. Nothing when none comes in time.
    std::optional<hash_digest> next_getheaders(std::chrono::milliseconds limit) {
        auto const deadline = std::chrono::steady_clock::now() + limit;
        while (auto received = next(deadline)) {
            if (received->command != domain::message::get_headers::command) continue;
            byte_reader reader(received->payload);
            auto parsed = domain::message::get_headers::from_data(reader, wire_version);
            REQUIRE(parsed.has_value());
            REQUIRE_FALSE(parsed->start_hashes().empty());
            return parsed->start_hashes().front();
        }
        return std::nullopt;
    }

    void send_headers(std::vector<domain::chain::block> const& blocks) {
        domain::message::header::list elements;
        for (auto const& blk : blocks) {
            elements.push_back(blk.header());
        }
        auto built = domain::message::headers::create(std::move(elements));
        REQUIRE(built.has_value());
        send(*built);
    }
};

bool wait_until(std::function<bool()> const& done, std::chrono::milliseconds limit) {
    auto const deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

} // namespace

TEST_CASE("#710: an announcement is not taken as the answer to a getheaders",
          "[header_attribution][integration]") {
    // First, and destroyed last: no thread that logs outlives it.
    captured_log log{"attribution_710"};

    chain_fixture fixture{"attribution_710"};
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Headers and bodies level at 5.
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 40 * block_spacing;
    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == 5);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    auto const block6 = mine_block(prev, 6, base_time + 6 * block_spacing, 0, {}, 0);
    auto const block7 = mine_block(block6.hash(), 7, base_time + 7 * block_spacing, 0, {}, 0);
    auto const hash5 = trunk.back().hash();
    auto const hash6 = block6.hash();
    auto const hash7 = block7.hash();

    auto const port = free_port();
    auto const config = node_settings(port, "attribution_710");

    std::optional<hash_digest> first;
    std::optional<hash_digest> second;
    bool has_seven = false;
    std::optional<hash_digest> asked_after;
    bool stopped_cleanly = false;
    {
        p2p_node network(config);
        auto started = ::asio::co_spawn(network.thread_pool().get_executor(),
            [&network]() -> ::asio::awaitable<int> {
                auto const ec = co_await network.start();
                co_return ec.value();
            }, ::asio::use_future);
        REQUIRE(started.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
        REQUIRE(started.get() == 0);

        // The supervisor, the listener and the peers' protocol loops: what a
        // running node runs, and what accepts the peer below.
        auto running = ::asio::co_spawn(network.thread_pool().get_executor(),
            [&network]() -> ::asio::awaitable<int> {
                auto const ec = co_await network.run();
                co_return ec.value();
            }, ::asio::use_future);

        auto orchestrator = ::asio::co_spawn(network.thread_pool().get_executor(),
            sync_orchestrator(fixture.chain(), fixture.organizer(), network,
                domain::config::network::regtest,
                [](std::string const& reason) { spdlog::error("[test] fatal: {}", reason); }),
            ::asio::use_future);

        {
            remote_peer peer;
            REQUIRE(peer.connect(port, std::chrono::seconds(10)));
            REQUIRE(peer.handshake(6, std::chrono::seconds(10)));

            // 1. getheaders #1, from 5.
            first = peer.next_getheaders(std::chrono::seconds(15));

            // 2. block 6 is announced.
            peer.send_headers({block6});

            // 3. getheaders #2, from 6.
            second = peer.next_getheaders(std::chrono::seconds(15));

            // 4. the answer to #1, then the answer to #2.
            peer.send_headers({block6});
            peer.send_headers({block7});

            // Everything the peer sent is in: 7 is in the index, with nothing
            // more asked of the peer.
            has_seven = wait_until([&] {
                return fixture.organizer().index().find(hash7) != blockchain::header_index::null_index;
            }, std::chrono::seconds(5));

            // And whatever the node asks next, it does not ask for what the
            // peer has already sent: no getheaders from below 7.
            asked_after = peer.next_getheaders(std::chrono::seconds(3));
        }   // the peer disconnects

        network.stop();
        stopped_cleanly =
            orchestrator.wait_for(std::chrono::seconds(120)) == std::future_status::ready
            && running.wait_for(std::chrono::seconds(120)) == std::future_status::ready;
        if (stopped_cleanly) {
            orchestrator.get();
            (void)running.get();
        }
    }

    CHECK(stopped_cleanly);
    REQUIRE(first.has_value());
    CHECK(*first == hash5);
    REQUIRE(second.has_value());
    CHECK(*second == hash6);

    // The defect's two observable consequences.
    CHECK(has_seven);
    if (asked_after) {
        CHECK(*asked_after != hash5);
        CHECK(*asked_after != hash6);
    }
}
