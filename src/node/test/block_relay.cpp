// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "sync_harness.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/message/messages.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/node/block_relay.hpp>
#include <kth/node/detail/block_relay.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/infrastructure/math/checksum.hpp>

using namespace kth;
using namespace kth::node;
using namespace kth::test;

namespace {

constexpr uint32_t wire_version = 70015;
constexpr uint32_t regtest_magic = 0xdab5bffa;
using namespace std::chrono_literals;

network::settings relay_settings() {
    network::settings settings;
    settings.identifier = regtest_magic;
    settings.protocol_maximum = wire_version;
    settings.inbound_port = 0;
    settings.inbound_connections = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.seeds.clear();
    auto const base = std::filesystem::temp_directory_path() / "kth_block_relay_unused";
    settings.hosts_file = base.string() + "_hosts";
    settings.peers_file = base.string() + "_peers";
    settings.banlist_file = base.string() + "_bans";
    return settings;
}

template <typename Predicate>
bool wait_until(Predicate done, std::chrono::milliseconds limit) {
    auto const deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return done();
}

network::peer_session::ptr idle_peer(::asio::io_context& io, network::settings const& settings) {
    auto peer = std::make_shared<network::peer_session>(::asio::ip::tcp::socket(io), settings);
    peer->set_negotiated_version(wire_version);
    return peer;
}

domain::message::header::list branch(size_t count, hash_digest parent = null_hash, uint32_t salt = 0) {
    domain::message::header::list out;
    for (size_t i = 0; i < count; ++i) {
        out.emplace_back(domain::chain::header{1, parent, null_hash, 1, regtest_bits, salt + uint32_t(i)});
        parent = domain::chain::hash(out.back());
    }
    return out;
}

struct wire_peer {
    ::asio::io_context io;
    ::asio::io_context client_io;
    ::asio::ip::tcp::socket client{client_io};
    network::peer_session::ptr session;
    ::asio::executor_work_guard<::asio::io_context::executor_type> guard{::asio::make_work_guard(io)};
    std::thread runner;

    explicit wire_peer(network::settings const& settings, bool slow = false) {
        ::asio::ip::tcp::acceptor acceptor(io, {::asio::ip::address_v4::loopback(), 0});
        auto const listening = acceptor.local_endpoint();
        REQUIRE(listening.port() != 0);
        client.open(::asio::ip::tcp::v4());
        if (slow) client.set_option(::asio::socket_base::receive_buffer_size(1024));
        ::asio::ip::tcp::socket socket(io);
        auto accepted = acceptor.async_accept(socket, ::asio::use_future);
        auto connected = client.async_connect(listening, ::asio::use_future);
        client_io.run_for(3s);
        REQUIRE(connected.wait_for(0s) == std::future_status::ready);
        connected.get();
        REQUIRE(wait_until([&] {
            io.poll();
            return accepted.wait_for(0s) == std::future_status::ready;
        }, 3s));
        accepted.get();
        if (slow) socket.set_option(::asio::socket_base::send_buffer_size(1024));
        session = std::make_shared<network::peer_session>(std::move(socket), settings);
        session->set_negotiated_version(wire_version);
        domain::message::version version;
        version.set_relay(false);
        session->set_peer_version(std::make_shared<domain::message::version const>(std::move(version)));
        client.non_blocking(true);
        ::asio::co_spawn(io, [peer = session]() -> ::asio::awaitable<void> {
            (void)co_await peer->run();
        }, ::asio::detached);
        runner = std::thread([this] { io.run(); });
    }

    ~wire_peer() {
        session->stop();
        std::error_code ignored;
        client.close(ignored);
        guard.reset();
        io.stop();
        if (runner.joinable()) runner.join();
    }

    wire_peer(wire_peer const&) = delete;
    wire_peer& operator=(wire_peer const&) = delete;

    void write(data_chunk const& bytes, size_t begin, size_t end) {
        auto const deadline = std::chrono::steady_clock::now() + 3s;
        while (begin < end && std::chrono::steady_clock::now() < deadline) {
            std::error_code ec;
            begin += client.write_some(::asio::buffer(bytes.data() + begin, end - begin), ec);
            if (ec == ::asio::error::would_block || ec == ::asio::error::try_again) {
                std::this_thread::sleep_for(2ms);
                continue;
            }
            REQUIRE_FALSE(ec);
        }
        REQUIRE(begin == end);
    }

    template <typename Message>
    void write(Message const& message) {
        auto const bytes = domain::message::serialize(wire_version, message, regtest_magic);
        write(bytes, 0, bytes.size());
    }

    network::raw_message received() {
        std::optional<network::raw_message> message;
        REQUIRE(wait_until([&] {
            return session->messages().try_receive([&](std::error_code ec, network::raw_message raw) {
                REQUIRE_FALSE(ec);
                message = std::move(raw);
            });
        }, 3s));
        REQUIRE(message);
        return std::move(*message);
    }

    bool read_exact(data_chunk& bytes, std::chrono::steady_clock::time_point deadline) {
        size_t offset = 0;
        while (offset < bytes.size() && std::chrono::steady_clock::now() < deadline) {
            std::error_code ec;
            offset += client.read_some(::asio::buffer(bytes.data() + offset, bytes.size() - offset), ec);
            if (ec == ::asio::error::would_block || ec == ::asio::error::try_again) {
                std::this_thread::sleep_for(2ms);
                continue;
            }
            REQUIRE_FALSE(ec);
        }
        if (offset == bytes.size()) return true;
        // A truncated message must not pass as silence.
        REQUIRE(offset == 0);
        return false;
    }

    std::optional<network::raw_message> next(std::chrono::milliseconds limit = 3s) {
        auto const deadline = std::chrono::steady_clock::now() + limit;
        data_chunk heading(domain::message::heading::satoshi_fixed_size());
        if ( ! read_exact(heading, deadline)) return std::nullopt;
        byte_reader reader(heading);
        auto parsed = domain::message::heading::from_data(reader, wire_version);
        REQUIRE(parsed);
        REQUIRE(parsed->magic() == regtest_magic);
        REQUIRE(parsed->payload_size() <= 4096);
        data_chunk payload(parsed->payload_size());
        REQUIRE(read_exact(payload, deadline));
        return network::raw_message{std::move(*parsed), std::move(payload)};
    }
};

hash_list header_hashes(network::raw_message const& raw) {
    REQUIRE(raw.heading.command() == domain::message::headers::command);
    byte_reader reader(raw.payload);
    auto parsed = domain::message::headers::from_data(reader, wire_version);
    REQUIRE(parsed);
    hash_list hashes;
    parsed->to_hashes(hashes);
    return hashes;
}

hash_list inventory_hashes(network::raw_message const& raw) {
    REQUIRE(raw.heading.command() == domain::message::inventory::command);
    byte_reader reader(raw.payload);
    auto parsed = domain::message::inventory::from_data(reader, wire_version);
    REQUIRE(parsed);
    hash_list hashes;
    for (auto const& entry : parsed->inventories()) {
        REQUIRE(entry.type() == domain::message::inventory_vector::type_id::block);
        hashes.push_back(entry.hash());
    }
    return hashes;
}

// The builder is the node's real connect path. The callback runs only after its
// published view and UTXO set describe the same connected tip.
void connect_with_relay(chain_fixture& fixture, domain::chain::block const& block,
    network::peer_session::ptr const& source, sync::connected_handler callback) {
    auto& chain = fixture.chain();
    ::asio::io_context io;
    sync::block_storage_input_channel input(io.get_executor(), 2);
    sync::chunk_validated_channel output(io.get_executor(), 8);
    std::atomic<uint32_t> contiguous{1};
    std::vector<std::string> fatal;
    bool notified = false;
    ::asio::co_spawn(io, sync::block_storage_task(chain, input, output, 1, fixture.organizer(), &contiguous),
        ::asio::detached);
    ::asio::co_spawn(io, sync::utxo_build_task(chain, contiguous, 1, domain::config::network::regtest,
        [&] { return notified || ! fatal.empty(); },
        [&](std::string const& reason) { fatal.push_back(reason); },
        nullptr,
        [&](uint32_t first, uint32_t last) {
            REQUIRE(first == 1);
            REQUIRE(last == 1);
            REQUIRE_FALSE(chain.transition_in_progress());
            auto const view = chain.chain_view();
            REQUIRE(view);
            REQUIRE(view->connected_tip_height == last);
            REQUIRE(view->tip_hash == block.hash());
            REQUIRE(chain.synchronization().synchronized());
            callback(first, last);
            notified = true;
        }), ::asio::detached);
    REQUIRE(input.try_send(std::error_code{}, sync::downloaded_chunk{
        .start_height = 1,
        .chunk_id = 0,
        .blocks = {to_light(block)},
        .source_peer = source,
        .generation = chain.headers().generation()
    }));
    REQUIRE(input.try_send(std::error_code{}, sync::stop_request{}));
    io.run_for(20s);
    REQUIRE(fatal.empty());
    REQUIRE(notified);
}

} // namespace

TEST_CASE("block relay selects headers only for a known parent and skips known tips", "[block_relay]") {
    auto const settings = relay_settings();
    ::asio::io_context io;
    auto peer = idle_peer(io, settings);
    auto const incoming = branch(2);

    CHECK(announce_blocks(*peer, {}, true) == block_announcement::none);
    CHECK(announce_blocks(*peer, incoming, false) == block_announcement::none);
    CHECK(peer->known_block_count() == 0);
    CHECK(announce_blocks(*peer, incoming, true, peer.get()) == block_announcement::none);
    peer->set_wants_headers(true);
    CHECK(announce_blocks(*peer, incoming, true) == block_announcement::inventory);
    CHECK(announce_blocks(*peer, incoming, true) == block_announcement::none);

    auto headers_peer = idle_peer(io, settings);
    headers_peer->set_wants_headers(true);
    REQUIRE(headers_peer->remember_block(incoming.front().previous_block_hash()));
    CHECK(announce_blocks(*headers_peer, incoming, true) == block_announcement::headers);
    CHECK(announce_blocks(*headers_peer, incoming, true) == block_announcement::none);

    auto knowing_peer = idle_peer(io, settings);
    REQUIRE(knowing_peer->remember_block(domain::chain::hash(incoming.back())));
    CHECK(announce_blocks(*knowing_peer, incoming, true) == block_announcement::none);
    knowing_peer->stop();
    CHECK_FALSE(knowing_peer->try_send(domain::message::ping{1}));
    CHECK(announce_blocks(*knowing_peer, incoming, true) == block_announcement::none);

    auto backed_up = idle_peer(io, settings);
    backed_up->set_wants_headers(true);
    backed_up->remember_block(incoming.front().previous_block_hash());
    for (size_t i = 0; i < network::peer_session::outbound_capacity; ++i) {
        REQUIRE(backed_up->try_send(domain::message::ping{uint64_t(i)}));
    }
    CHECK(announce_blocks(*backed_up, incoming, true) == block_announcement::none);
    CHECK_FALSE(backed_up->stopped());
    CHECK_FALSE(backed_up->knows_block(domain::chain::hash(incoming.back())));
}

TEST_CASE("block inventory includes each unknown block up to eight and only a long branch tip", "[block_relay]") {
    auto const settings = relay_settings();
    auto const incoming = branch(8);
    wire_peer inventory_peer(settings);
    REQUIRE(inventory_peer.session->remember_block(domain::chain::hash(incoming[2])));
    REQUIRE(announce_blocks(*inventory_peer.session, incoming, true) == block_announcement::inventory);
    auto const message = inventory_peer.next();
    REQUIRE(message);
    hash_list expected;
    for (size_t i = 0; i < incoming.size(); ++i) {
        if (i != 2) expected.push_back(domain::chain::hash(incoming[i]));
    }
    CHECK(inventory_hashes(*message) == expected);

    wire_peer eight_headers_peer(settings);
    eight_headers_peer.session->set_wants_headers(true);
    REQUIRE(eight_headers_peer.session->remember_block(incoming.front().previous_block_hash()));
    REQUIRE(announce_blocks(*eight_headers_peer.session, incoming, true) == block_announcement::headers);
    auto const eight_headers = eight_headers_peer.next();
    REQUIRE(eight_headers);
    hash_list all_hashes;
    for (auto const& header : incoming) all_hashes.push_back(domain::chain::hash(header));
    CHECK(header_hashes(*eight_headers) == all_hashes);

    auto const long_branch = branch(9);
    wire_peer headers_peer(settings);
    headers_peer.session->set_wants_headers(true);
    REQUIRE(headers_peer.session->remember_block(long_branch.front().previous_block_hash()));
    REQUIRE(announce_blocks(*headers_peer.session, long_branch, true) == block_announcement::inventory);
    auto const fallback = headers_peer.next();
    REQUIRE(fallback);
    CHECK(inventory_hashes(*fallback) == hash_list{domain::chain::hash(long_branch.back())});

    wire_peer long_inventory_peer(settings);
    REQUIRE(announce_blocks(*long_inventory_peer.session, long_branch, true) == block_announcement::inventory);
    auto const long_inventory = long_inventory_peer.next();
    REQUIRE(long_inventory);
    CHECK(inventory_hashes(*long_inventory) == hash_list{domain::chain::hash(long_branch.back())});
}

TEST_CASE("a reorganization announces the connected new branch after its fork", "[block_relay]") {
    auto const settings = relay_settings();
    auto const old_branch = branch(3);
    auto const new_branch = branch(3, domain::chain::hash(old_branch.front()), 100);
    wire_peer peer(settings);
    peer.session->set_wants_headers(true);
    REQUIRE(peer.session->remember_block(domain::chain::hash(old_branch.front())));
    REQUIRE(peer.session->remember_block(domain::chain::hash(old_branch.back())));
    REQUIRE(announce_blocks(*peer.session, new_branch, true) == block_announcement::headers);
    auto const message = peer.next();
    REQUIRE(message);
    hash_list expected;
    for (auto const& header : new_branch) expected.push_back(domain::chain::hash(header));
    CHECK(header_hashes(*message) == expected);
}

TEST_CASE("block relay waits out initial catchup and retains a branch across connected batches", "[block_relay]") {
    node::detail::block_relay_state initial;
    CHECK_FALSE(initial.connected(1, 100, false));
    CHECK_FALSE(initial.connected(101, 102, true));
    auto const next = initial.connected(103, 103, true);
    REQUIRE(next);
    CHECK(next->first == 103);
    CHECK(next->last == 103);

    node::detail::block_relay_state synced(true);
    CHECK_FALSE(synced.connected(80, 179, false));
    auto const reorg = synced.connected(180, 181, true);
    REQUIRE(reorg);
    CHECK(reorg->first == 80);
    CHECK(reorg->last == 181);
    auto const later = synced.connected(182, 182, true);
    REQUIRE(later);
    CHECK(later->first == 182);
    CHECK(later->last == 182);
}

TEST_CASE("per-peer block knowledge remains bounded and duplicate announcements do not evict it", "[block_relay]") {
    auto const settings = relay_settings();
    ::asio::io_context io;
    auto peer = idle_peer(io, settings);
    auto const headers = branch(network::peer_session::known_block_capacity + 1);
    for (size_t i = 0; i < network::peer_session::known_block_capacity; ++i) {
        REQUIRE(peer->remember_block(domain::chain::hash(headers[i])));
    }
    CHECK_FALSE(peer->remember_block(domain::chain::hash(headers.front())));
    CHECK(peer->known_block_count() == network::peer_session::known_block_capacity);
    REQUIRE(peer->remember_block(domain::chain::hash(headers.back())));
    CHECK_FALSE(peer->knows_block(domain::chain::hash(headers.front())));
    CHECK(peer->knows_block(domain::chain::hash(headers[1])));
    CHECK(peer->knows_block(domain::chain::hash(headers.back())));
    CHECK(peer->known_block_count() == network::peer_session::known_block_capacity);
}

TEST_CASE("connected header snapshots contain the whole short range or nine headers from its tip", "[block_relay]") {
    blockchain::header_index index(32);
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const first = index.add(genesis.hash(), genesis.header());
    REQUIRE(first.inserted);
    index.active_push(first.index);
    auto const incoming = branch(11, genesis.hash());
    for (auto const& header : incoming) {
        auto const added = index.add(domain::chain::hash(header), header);
        REQUIRE(added.inserted);
        index.active_push(added.index);
    }
    auto const short_range = connected_block_headers(index, 3, 5);
    REQUIRE(short_range.size() == 3);
    CHECK(domain::chain::hash(short_range.front()) == domain::chain::hash(incoming[2]));
    CHECK(domain::chain::hash(short_range.back()) == domain::chain::hash(incoming[4]));
    auto const long_range = connected_block_headers(index, 1, 11);
    REQUIRE(long_range.size() == 9);
    CHECK(domain::chain::hash(long_range.front()) == domain::chain::hash(incoming[2]));
    CHECK(domain::chain::hash(long_range.back()) == domain::chain::hash(incoming.back()));
    CHECK(connected_block_headers(index, 10, 12).empty());
}

TEST_CASE("a connected peer block reaches headers and inventory peers without echoing its source", "[block_relay]") {
    auto const settings = relay_settings();
    p2p_node network(settings);
    wire_peer source(settings);
    wire_peer headers_peer(settings);
    wire_peer inventory_peer(settings);
    ::asio::io_context fanout_io;
    network::peer_manager peers(fanout_io.get_executor());
    source.session->set_nonce(1);
    headers_peer.session->set_nonce(2);
    inventory_peer.session->set_nonce(3);
    auto added = ::asio::co_spawn(fanout_io, [&]() -> ::asio::awaitable<code> {
        for (auto const& peer : {source.session, headers_peer.session, inventory_peer.session}) {
            if (auto const ec = co_await peers.add(peer); ec) co_return ec;
        }
        co_return error::success;
    }, ::asio::use_future);
    fanout_io.run_for(3s);
    REQUIRE(added.wait_for(0s) == std::future_status::ready);
    REQUIRE(added.get() == error::success);
    chain_fixture fixture("block_relay_wire", 0);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const parent = domain::chain::block::genesis_regtest().hash();
    auto const block = mine_block(parent, 1, uint32_t(zulu_time()) - block_spacing, 0, {}, 0);

    headers_peer.write(domain::message::send_headers{});
    REQUIRE(network.observe_block_message(headers_peer.session, headers_peer.received()));
    REQUIRE(headers_peer.session->wants_headers());
    headers_peer.session->remember_block(parent);
    source.write(domain::message::block{block});
    auto const raw = source.received();
    CHECK_FALSE(network.observe_block_message(source.session, raw));
    REQUIRE(source.session->knows_block(block.hash()));
    byte_reader reader(raw.payload);
    auto parsed = domain::message::block::from_data(reader, wire_version);
    REQUIRE(parsed);
    REQUIRE(fixture.organizer().add_headers(headers_of({*parsed})).headers_added == 1);
    persist_headers(fixture, {*parsed}, 1);
    auto const incoming = headers_of({*parsed});
    std::future<void> relayed;
    connect_with_relay(fixture, *parsed, source.session, [&](uint32_t, uint32_t) {
        relayed = ::asio::co_spawn(fanout_io, announce_blocks(peers, incoming), ::asio::use_future);
    });
    fanout_io.restart();
    fanout_io.run_for(3s);
    REQUIRE(relayed.valid());
    REQUIRE(relayed.wait_for(0s) == std::future_status::ready);
    relayed.get();
    auto const headers = headers_peer.next();
    auto const inventory = inventory_peer.next();
    REQUIRE(headers);
    REQUIRE(inventory);
    CHECK(header_hashes(*headers) == hash_list{block.hash()});
    CHECK(inventory_hashes(*inventory) == hash_list{block.hash()});
    CHECK_FALSE(source.next(150ms));
    network.stop();
    network.join();
}

TEST_CASE("sendheaders split across reads still requires an empty payload", "[block_relay]") {
    auto const settings = relay_settings();
    p2p_node network(settings);
    wire_peer peer(settings);
    data_chunk const invalid_payload{1};
    domain::message::heading invalid_heading{regtest_magic, domain::message::send_headers::command,
        1, bitcoin_checksum(byte_span{invalid_payload.data(), invalid_payload.size()})};
    auto invalid = invalid_heading.to_data();
    invalid.push_back(1);
    peer.write(invalid, 0, invalid.size() - 1);
    std::this_thread::sleep_for(20ms);
    CHECK_FALSE(peer.session->messages().try_receive([](std::error_code, network::raw_message) {}));
    peer.write(invalid, invalid.size() - 1, invalid.size());
    REQUIRE(network.observe_block_message(peer.session, peer.received()));
    CHECK_FALSE(peer.session->wants_headers());

    auto const valid = domain::message::serialize(wire_version, domain::message::send_headers{}, regtest_magic);
    peer.write(valid, 0, 12);
    std::this_thread::sleep_for(20ms);
    CHECK_FALSE(peer.session->messages().try_receive([](std::error_code, network::raw_message) {}));
    peer.write(valid, 12, valid.size());
    REQUIRE(network.observe_block_message(peer.session, peer.received()));
    CHECK(peer.session->wants_headers());
    network.stop();
    network.join();
}

TEST_CASE("a peer that never reads cannot delay block connection or another announcement", "[block_relay]") {
    auto const settings = relay_settings();
    wire_peer slow(settings, true);
    wire_peer reader_peer(settings);
    auto queued = std::make_shared<std::atomic<size_t>>(0);
    auto filling = ::asio::co_spawn(slow.io,
        [peer = slow.session, queued]() -> ::asio::awaitable<void> {
            for (size_t i = 0; i < network::peer_session::outbound_capacity + 2; ++i) {
                if (co_await peer->send_raw(data_chunk(256 * 1024, 0)) != error::success) co_return;
                queued->fetch_add(1);
            }
        }, ::asio::use_future);
    REQUIRE(wait_until([&] { return queued->load() >= network::peer_session::outbound_capacity + 1; }, 5s));
    REQUIRE(filling.wait_for(100ms) == std::future_status::timeout);

    chain_fixture fixture("block_relay_slow", 0);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const block = mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - block_spacing, 0, {}, 0);
    REQUIRE(fixture.organizer().add_headers(headers_of({block})).headers_added == 1);
    persist_headers(fixture, {block}, 1);
    auto const incoming = headers_of({block});
    connect_with_relay(fixture, block, {}, [&](uint32_t, uint32_t) {
        CHECK(announce_blocks(*slow.session, incoming, true) == block_announcement::none);
        CHECK_FALSE(slow.session->stopped());
        CHECK_FALSE(slow.session->knows_block(block.hash()));
        CHECK(announce_blocks(*reader_peer.session, incoming, true) == block_announcement::inventory);
    });
    auto const message = reader_peer.next();
    REQUIRE(message);
    CHECK(inventory_hashes(*message) == hash_list{block.hash()});

    // The slow peer is kept, so its filling sends end only once it stops.
    slow.session->stop();
    REQUIRE(filling.wait_for(3s) == std::future_status::ready);
    filling.get();
}
