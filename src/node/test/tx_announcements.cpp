// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/message/fee_filter.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/node/detail/tx_announcements.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/network/settings.hpp>
#include <kth/network/tx_inventory.hpp>

using namespace kth;
using namespace kth::node::detail;
using namespace std::chrono_literals;

namespace {

using clock_type = tx_announcement_schedule::clock;

domain::message::transaction::const_ptr make_tx(uint32_t locktime) {
    return std::make_shared<domain::message::transaction const>(
        domain::message::transaction(1, locktime, domain::chain::input::list{}, domain::chain::output::list{}));
}

// A pooled entry: `fee` satoshis over 1000 bytes, so its fee per kB is `fee`.
blockchain::mempool_entry entry(uint64_t id, uint64_t fee = 1000) {
    return blockchain::mempool_entry{make_tx(uint32_t(id)), fee, 1000, 1, 0, id};
}

hash_digest txid(uint64_t id) {
    return make_tx(uint32_t(id))->hash();
}

hash_list txids(std::initializer_list<uint64_t> ids) {
    hash_list out;
    for (auto const id : ids) {
        out.push_back(txid(id));
    }
    return out;
}

admitted_batch batch(std::initializer_list<uint64_t> ids, uint64_t last) {
    admitted_batch out;
    for (auto const id : ids) {
        out.entries.push_back(entry(id));
    }
    out.last = last;
    return out;
}

} // namespace

// -----------------------------------------------------------------------------
// What a peer knows
// -----------------------------------------------------------------------------

TEST_CASE("rolling bloom filter: holds what was inserted, and forgets the oldest generations",
          "[tx_announcement][bloom]") {
    network::rolling_bloom_filter filter(10, 0.000001);
    CHECK_FALSE(filter.contains(txid(1)));
    filter.insert(txid(1));
    CHECK(filter.contains(txid(1)));
    // Five per generation: the first is kept through the next two.
    for (uint64_t i = 2; i <= 10; ++i) {
        filter.insert(txid(i));
    }
    CHECK(filter.contains(txid(1)));
    // A fourth generation replaces the first.
    for (uint64_t i = 11; i <= 16; ++i) {
        filter.insert(txid(i));
    }
    CHECK_FALSE(filter.contains(txid(1)));
    CHECK(filter.contains(txid(16)));
    filter.reset();
    CHECK_FALSE(filter.contains(txid(16)));
}

TEST_CASE("rolling bloom filter: BCHN's 50,000 at one in a million keeps false positives rare",
          "[tx_announcement][bloom]") {
    network::tx_inventory inventory;
    for (uint64_t i = 0; i < network::tx_inventory::known_capacity; ++i) {
        inventory.add_known(txid(i));
    }
    for (uint64_t i = 0; i < network::tx_inventory::known_capacity; ++i) {
        REQUIRE(inventory.known(txid(i)));
    }
    size_t false_positives = 0;
    for (uint64_t i = 1'000'000; i < 1'100'000; ++i) {
        false_positives += inventory.known(txid(i)) ? 1 : 0;
    }
    CHECK(false_positives <= 2u);
}

// -----------------------------------------------------------------------------
// One peer's schedule
// -----------------------------------------------------------------------------

TEST_CASE("tx announcement: BCHN's announcement size, 7 per second per MB over half a second",
          "[tx_announcement][schedule]") {
    CHECK(max_announcements(32'000'000) == 112u);
    CHECK(max_announcements(1'000'000) == 4u);   // 3.5, rounded up
    CHECK(max_announcements(256'000'000) == 896u);
}

TEST_CASE("tx announcement schedule: nothing is due until something is admitted past the cursor",
          "[tx_announcement][schedule]") {
    auto const t0 = clock_type::now();
    SECTION("outbound: its own delay") {
        tx_announcement_schedule schedule(false, 5);
        CHECK(schedule.due() == clock_type::time_point::max());
        schedule.on_admitted({5, t0 + 1s}, t0, 100ms);
        CHECK(schedule.due() == clock_type::time_point::max());
        schedule.on_admitted({6, t0 + 1s}, t0, 100ms);
        CHECK(schedule.due() == t0 + 100ms);
        // Already due: a later admission does not move it.
        schedule.on_admitted({7, t0 + 1s}, t0 + 50ms, 100ms);
        CHECK(schedule.due() == t0 + 100ms);
    }
    SECTION("inbound: the inbound peers' schedule") {
        tx_announcement_schedule schedule(true, 5);
        schedule.on_admitted({6, t0 + 1s}, t0, 100ms);
        CHECK(schedule.due() == t0 + 1s);
    }
}

TEST_CASE("tx announcement schedule: in order of admission, skipping what the peer knows or filters",
          "[tx_announcement][schedule]") {
    auto const t0 = clock_type::now();
    tx_announcement_schedule schedule(false, 0);
    network::tx_inventory inventory;
    inventory.add_known(txid(2));
    inventory.set_fee_filter(1500);

    admitted_batch slice;
    slice.entries = {entry(1, 2000), entry(2, 2000), entry(3, 1000), entry(4, 1500)};
    slice.last = 4;
    schedule.on_admitted({4, t0}, t0, 1ms);
    std::vector<hash_digest> out;
    schedule.take(slice, true, inventory, 10, out);
    // 2 is known; 3 pays less than the peer's filter.
    CHECK(out == txids({1, 4}));
    CHECK(schedule.cursor() == 4u);
    // What was announced is known now, and may be served; what the peer
    // only knew may not.
    CHECK(inventory.known(txid(1)));
    CHECK(inventory.known(txid(4)));
    CHECK(inventory.announced(txid(1)));
    CHECK(inventory.announced(txid(4)));
    CHECK_FALSE(inventory.announced(txid(2)));
    schedule.on_announced(t0, 1ms);
    CHECK(schedule.due() == clock_type::time_point::max());
}

TEST_CASE("tx announcement schedule: past one announcement's worth, the rest waits for the next",
          "[tx_announcement][schedule]") {
    auto const t0 = clock_type::now();
    tx_announcement_schedule schedule(false, 0);
    network::tx_inventory inventory;
    schedule.on_admitted({5, t0}, t0, 1ms);
    std::vector<hash_digest> out;
    schedule.take(batch({1, 2, 3, 4, 5}, 5), true, inventory, 2, out);
    CHECK(out == txids({1, 2}));
    CHECK(schedule.cursor() == 2u);
    schedule.on_announced(t0, 7ms);
    CHECK(schedule.due() == t0 + 7ms);

    out.clear();
    schedule.take(batch({3, 4, 5}, 5), true, inventory, 2, out);
    CHECK(out == txids({3, 4}));
    out.clear();
    schedule.take(batch({5}, 5), true, inventory, 2, out);
    CHECK(out == txids({5}));
    schedule.on_announced(t0, 7ms);
    CHECK(schedule.due() == clock_type::time_point::max());
}

TEST_CASE("tx announcement schedule: what left the pool does not stay owed",
          "[tx_announcement][schedule]") {
    auto const t0 = clock_type::now();
    tx_announcement_schedule schedule(false, 0);
    network::tx_inventory inventory;
    schedule.on_admitted({9, t0}, t0, 1ms);
    std::vector<hash_digest> out;
    // 4 to 9 were evicted or mined: the order ends at 3, and the cursor at 9.
    schedule.take(batch({2, 3}, 9), true, inventory, 10, out);
    CHECK(out == txids({2, 3}));
    CHECK(schedule.cursor() == 9u);
    schedule.on_announced(t0, 1ms);
    CHECK(schedule.due() == clock_type::time_point::max());
}

// -----------------------------------------------------------------------------
// The peer's service
// -----------------------------------------------------------------------------

namespace {

// The mempool's order, as the service reads it.
struct order {
    std::map<uint64_t, blockchain::mempool_entry> entries;
    uint64_t last{0};
    size_t limit{100};
    size_t reads{0};

    void admit(uint64_t id, uint64_t fee = 1000) {
        entries[id] = entry(id, fee);
        last = std::max(last, id);
    }

    tx_announcement_sources sources() {
        return {
            .last = [this]() -> ::asio::awaitable<uint64_t> { co_return last; },
            .admitted_after = [this](uint64_t after, size_t count) -> ::asio::awaitable<admitted_batch> {
                ++reads;
                admitted_batch out;
                for (auto it = entries.upper_bound(after); it != entries.end() && out.entries.size() < count; ++it) {
                    out.entries.push_back(it->second);
                }
                out.last = last;
                co_return out;
            },
            .limit = [this] { return limit; },
        };
    }
};

struct served_peer {
    ::asio::io_context io;
    ::asio::ip::tcp::socket client{io};
    network::peer_session::ptr session;
    data_chunk bytes;

    explicit served_peer(bool relay, bool inbound = false) {
        network::settings settings;
        settings.identifier = 0xdab5bffa;   // regtest
        ::asio::ip::tcp::acceptor acceptor(io, {::asio::ip::address_v4::loopback(), 0});
        ::asio::ip::tcp::socket server(io);
        client.connect(acceptor.local_endpoint());
        acceptor.accept(server);
        session = std::make_shared<network::peer_session>(std::move(server), settings, inbound);
        domain::message::version version;
        version.set_relay(relay);
        session->set_peer_version(std::make_shared<domain::message::version const>(std::move(version)));
        ::asio::co_spawn(io, [s = session]() -> ::asio::awaitable<void> {
            (void)co_await s->run();
        }, ::asio::detached);
    }

    ~served_peer() {
        session->stop();
        io.restart();
        io.run_for(100ms);
    }

    void run(std::chrono::milliseconds wait) {
        auto const deadline = std::chrono::steady_clock::now() + wait;
        while (std::chrono::steady_clock::now() < deadline) {
            io.restart();
            io.run_for(5ms);
            while (client.available() > 0) {
                data_chunk more(client.available());
                ::asio::read(client, ::asio::buffer(more));
                bytes.insert(bytes.end(), more.begin(), more.end());
            }
        }
    }

    // The transaction entries of each `inv` the peer was sent, in order,
    // after running for `wait`.
    std::vector<hash_list> invs(std::chrono::milliseconds wait) {
        run(wait);
        std::vector<hash_list> out;
        size_t at = 0;
        while (at + 24 <= bytes.size()) {
            char command[13] = {};
            std::memcpy(command, bytes.data() + at + 4, 12);
            uint32_t length = 0;
            std::memcpy(&length, bytes.data() + at + 16, 4);
            if (at + 24 + length > bytes.size()) break;
            if (std::string(command) == domain::message::inventory::command) {
                data_chunk payload(bytes.begin() + long(at + 24), bytes.begin() + long(at + 24 + length));
                byte_reader reader(payload);
                auto parsed = domain::message::inventory::from_data(reader, session->negotiated_version());
                REQUIRE(parsed.has_value());
                hash_list hashes;
                for (auto const& item : parsed->inventories()) {
                    CHECK(item.type() == domain::message::inventory_vector::type_id::transaction);
                    hashes.push_back(item.hash());
                }
                out.push_back(std::move(hashes));
            }
            at += 24 + length;
        }
        return out;
    }
};

} // namespace

TEST_CASE("the peer's service announces what is admitted after it starts, in order",
          "[tx_announcement][service]") {
    order pool;
    pool.admit(1);   // Before the peer: not owed to it.
    served_peer peer(true);
    node::message_dispatcher dispatcher;
    dispatcher.set_tx_announcement_handler(pool.sources());
    // The median draw: about 170 ms to an outbound peer's next announcement.
    // A random one can fall past the controls' windows.
    dispatcher.set_fee_filter_draw([] { return 0.5; });
    auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
    CHECK(peer.invs(100ms).empty());

    pool.admit(2);
    pool.admit(3);
    peer.session->announce_admissions({3, clock_type::now()});
    CHECK(peer.invs(300ms) == std::vector<hash_list>{txids({2, 3})});

    // Announced once: nothing more is owed.
    peer.session->announce_admissions({3, clock_type::now()});
    peer.bytes.clear();
    CHECK(peer.invs(300ms).empty());

    peer.session->stop();
    peer.io.restart();
    peer.io.run_for(200ms);
    CHECK(service.wait_for(0s) == std::future_status::ready);
}

TEST_CASE("the peer's service skips what the peer knows or filters",
          "[tx_announcement][service]") {
    order pool;
    served_peer peer(true);
    node::message_dispatcher dispatcher;
    dispatcher.set_tx_announcement_handler(pool.sources());
    // The median draw: about 170 ms to an outbound peer's next announcement.
    // A random one can fall past the controls' windows.
    dispatcher.set_fee_filter_draw([] { return 0.5; });
    auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
    peer.run(50ms);
    // As the read loop records them, on the session's strand.
    peer.session->tx_inventory().add_known(txid(1));
    peer.session->tx_inventory().set_fee_filter(1500);
    pool.admit(1, 5000);
    pool.admit(2, 1000);
    pool.admit(3, 2000);
    peer.session->announce_admissions({3, clock_type::now()});
    CHECK(peer.invs(300ms) == std::vector<hash_list>{txids({3})});
}

TEST_CASE("the peer's service announces nothing to a peer that asked for no transactions",
          "[tx_announcement][service]") {
    SECTION("relay off in its version") {
        order pool;
        served_peer peer(false);
        node::message_dispatcher dispatcher;
        dispatcher.set_tx_announcement_handler(pool.sources());
        dispatcher.set_fee_filter_draw([] { return 0.5; });
        auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
        peer.run(50ms);
        pool.admit(1);
        peer.session->announce_admissions({1, clock_type::now()});
        CHECK(peer.invs(300ms).empty());
        CHECK(pool.reads == 0u);
    }
    SECTION("no handler: the node does not relay") {
        served_peer peer(true);
        node::message_dispatcher dispatcher;
        auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
        peer.run(50ms);
        peer.session->announce_admissions({1, clock_type::now()});
        CHECK(peer.invs(300ms).empty());
    }
}

TEST_CASE("the peer's service sends one announcement's worth at a time, the rest at the next",
          "[tx_announcement][service]") {
    order pool;
    pool.limit = 2;
    served_peer peer(true);
    node::message_dispatcher dispatcher;
    dispatcher.set_tx_announcement_handler(pool.sources());
    // The median draw: about 170 ms to an outbound peer's next announcement.
    // A random one can fall past the controls' windows.
    dispatcher.set_fee_filter_draw([] { return 0.5; });
    auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
    peer.run(50ms);
    for (uint64_t id = 1; id <= 5; ++id) {
        pool.admit(id);
    }
    peer.session->announce_admissions({5, clock_type::now()});
    CHECK(peer.invs(1000ms) == std::vector<hash_list>{txids({1, 2}), txids({3, 4}), txids({5})});
}

TEST_CASE("inbound peers are announced to when the inbound schedule says",
          "[tx_announcement][service]") {
    order pool;
    served_peer peer(true, true);
    node::message_dispatcher dispatcher;
    dispatcher.set_tx_announcement_handler(pool.sources());
    // The median draw: about 170 ms to an outbound peer's next announcement.
    // A random one can fall past the controls' windows.
    dispatcher.set_fee_filter_draw([] { return 0.5; });
    auto service = ::asio::co_spawn(peer.io, dispatcher.serve(*peer.session), ::asio::use_future);
    peer.run(50ms);
    pool.admit(1);
    peer.session->announce_admissions({1, clock_type::now() + 400ms});
    CHECK(peer.invs(250ms).empty());
    CHECK(peer.invs(400ms) == std::vector<hash_list>{txids({1})});
}

// -----------------------------------------------------------------------------
// The watch
// -----------------------------------------------------------------------------

namespace {

network::peer_session::ptr make_session(::asio::io_context& io) {
    ::asio::ip::tcp::socket socket(io);
    return std::make_shared<network::peer_session>(std::move(socket), network::settings{}, false);
}

std::optional<network::tx_admissions> heard(network::peer_session::ptr const& peer) {
    std::error_code ec;
    return peer->admissions().try_receive(ec);
}

} // namespace

TEST_CASE("tx announcement watch: each ring hands every peer the last admission and the inbound schedule",
          "[tx_announcement][watch]") {
    ::asio::io_context io;
    ::asio::experimental::select_channel<std::monostate> events(1);
    std::vector<network::peer_session::ptr> peers{make_session(io), make_session(io)};
    uint64_t last = 7;
    size_t draws = 0;
    std::stop_source stop;
    auto watch = ::asio::co_spawn(io, tx_announcement_watch(events, tx_announcement_watch_sources{
            .last = [&]() -> ::asio::awaitable<uint64_t> { co_return last; },
            .peers = [&]() -> ::asio::awaitable<std::vector<network::peer_session::ptr>> { co_return peers; },
            // The largest draw: the inbound schedule is far off.
            .draw = [&] { ++draws; return 0.999999; },
        }, stop.get_token()), ::asio::use_future);
    io.poll();
    CHECK_FALSE(heard(peers[0]));

    auto const before = clock_type::now();
    REQUIRE(events.try_send(std::monostate{}));
    io.poll();
    auto const first = heard(peers[0]);
    REQUIRE(first);
    CHECK(first->last == 7u);
    CHECK(first->inbound_due > before + 1s);
    auto const second = heard(peers[1]);
    REQUIRE(second);
    CHECK(second->inbound_due == first->inbound_due);

    // Not passed yet: the inbound peers keep the one they have.
    last = 9;
    REQUIRE(events.try_send(std::monostate{}));
    io.poll();
    auto const again = heard(peers[0]);
    REQUIRE(again);
    CHECK(again->last == 9u);
    CHECK(again->inbound_due == first->inbound_due);
    CHECK(draws == 1u);

    stop.request_stop();
    io.poll();
    CHECK(watch.wait_for(0s) == std::future_status::ready);
}

TEST_CASE("tx announcement watch: ends when its events close, or stopped before it begins",
          "[tx_announcement][watch][shutdown]") {
    auto const sources = [](size_t& read) {
        return tx_announcement_watch_sources{
            .last = [&read]() -> ::asio::awaitable<uint64_t> { ++read; co_return 0; },
            .peers = []() -> ::asio::awaitable<std::vector<network::peer_session::ptr>> { co_return std::vector<network::peer_session::ptr>{}; },
            .draw = [] { return 0.5; },
        };
    };
    SECTION("events closed") {
        ::asio::io_context io;
        ::asio::experimental::select_channel<std::monostate> events(1);
        size_t read = 0;
        auto watch = ::asio::co_spawn(io, tx_announcement_watch(events, sources(read), {}), ::asio::use_future);
        io.poll();
        REQUIRE(watch.wait_for(0s) != std::future_status::ready);
        events.close();
        io.poll();
        CHECK(watch.wait_for(0s) == std::future_status::ready);
    }
    SECTION("stopped before it begins, with a ring waiting") {
        ::asio::io_context io;
        ::asio::experimental::select_channel<std::monostate> events(1);
        REQUIRE(events.try_send(std::monostate{}));
        size_t read = 0;
        std::stop_source stop;
        stop.request_stop();
        auto watch = ::asio::co_spawn(io, tx_announcement_watch(events, sources(read), stop.get_token()), ::asio::use_future);
        io.poll();
        CHECK(watch.wait_for(0s) == std::future_status::ready);
        CHECK(read == 0u);
    }
}
