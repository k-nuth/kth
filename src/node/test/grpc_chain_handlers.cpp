// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The typed API over the chain bridge, end to end through a real generated
// client and the real transport. The chain behind it is a fake with the
// block_chain query signatures: the adapter logic under test is the same code
// that production instantiates for blockchain::block_chain.

#include <test_helpers.hpp>

#include <atomic>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>

#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/grpc_rpc/chain_handlers.hpp>

#include "grpc_test_support.hpp"

using namespace kth;
using namespace kth::node;
using namespace kth::node::grpc_rpc;
using kth::node::test::client;
namespace v1 = ::kth::rpc::v1;

namespace {

struct heights {
    uint32_t header;
    uint32_t block;
};

// What block_chain exposes to the adapter, with scripted answers. Every method
// records the thread it ran on, to prove where chain work happens.
struct fake_chain {
    using block_ptr = std::shared_ptr<domain::message::block const>;
    using tx_ptr = std::shared_ptr<domain::message::transaction const>;

    std::mutex mutex;
    std::vector<std::thread::id> threads;
    std::vector<hash_digest> requested;

    std::function<std::expected<heights, int>()> last_heights = [] {
        return std::expected<heights, int>(heights{800, 790});
    };
    std::function<std::expected<hash_digest, int>(size_t)> block_hash;
    // The block lookup is height -> raw body, as the production adapter does it.
    std::function<std::expected<size_t, kth::code>(hash_digest const&)> height_of;
    std::function<std::expected<std::vector<data_chunk>, database::result_code>(uint32_t, uint32_t)> raw;
    ::asio::thread_pool disk_pool{1};   // stands in for the chain's priority pool
    std::vector<std::thread::id> disk_threads;
    std::function<std::expected<std::tuple<tx_ptr, size_t, size_t>, kth::code>(hash_digest const&)> confirmed;
    std::function<std::expected<tx_ptr, kth::code>(hash_digest const&)> pooled;

    void note(hash_digest const* hash = nullptr) {
        std::lock_guard<std::mutex> const lock(mutex);
        threads.push_back(std::this_thread::get_id());
        if (hash) requested.push_back(*hash);
    }

    std::expected<heights, int> get_last_heights() { note(); return last_heights(); }
    std::expected<hash_digest, int> get_block_hash(size_t height) { note(); return block_hash(height); }

    ::asio::awaitable<std::expected<size_t, kth::code>>
    fetch_block_height(hash_digest const& hash) { note(&hash); co_return height_of(hash); }

    std::expected<std::vector<data_chunk>, database::result_code>
    fetch_blocks_raw(uint32_t from, uint32_t to) {
        std::lock_guard<std::mutex> const lock(mutex);
        disk_threads.push_back(std::this_thread::get_id());
        return raw(from, to);
    }

    struct pool_view {
        ::asio::thread_pool& pool;
        auto get_executor() { return pool.get_executor(); }
    };
    pool_view thread_pool() { return {disk_pool}; }

    ::asio::awaitable<std::expected<std::tuple<tx_ptr, size_t, size_t>, kth::code>>
    fetch_transaction(hash_digest const& hash, bool) { note(&hash); co_return confirmed(hash); }

    ::asio::awaitable<std::expected<tx_ptr, kth::code>>
    fetch_unconfirmed_transaction(hash_digest const& hash) { note(&hash); co_return pooled(hash); }
};

std::string as_bytes(hash_digest const& hash) {
    return std::string(reinterpret_cast<char const*>(hash.data()), hash.size());
}

std::string as_bytes(data_chunk const& data) {
    return std::string(reinterpret_cast<char const*>(data.data()), data.size());
}

struct harness {
    explicit harness(v1::Network network = v1::NETWORK_MAINNET, transport_options options = {})
        : chain_pool(1)
    {
        // The pool's single thread is the "chain thread": learn its id by running on it.
        std::promise<std::thread::id> id;
        ::asio::post(chain_pool, [&id] { id.set_value(std::this_thread::get_id()); });
        chain_thread = id.get_future().get();

        chain.block_hash = [](size_t) { return std::expected<hash_digest, int>(null_hash); };
        chain.height_of = [](hash_digest const&) { return std::unexpected(kth::code(error::not_found)); };
        chain.raw = [](uint32_t, uint32_t) { return std::unexpected(database::result_code::key_not_found); };
        chain.confirmed = [](hash_digest const&) { return std::unexpected(kth::code(error::not_found)); };
        chain.pooled = [](hash_digest const&) { return std::unexpected(kth::code(error::not_found)); };

        options.port = 0;
        options.user = "user";
        options.password = "secret";
        options.shutdown_grace = std::chrono::milliseconds(200);
        service = std::make_unique<transport>(options,
            make_chain_handlers(make_chain_reader(chain), network, chain_pool.get_executor()));
        auto const started = service->start();
        REQUIRE(started.has_value());
        port = *started;
    }

    ~harness() {
        service->stop();
        service->join();
        chain_pool.join();
        chain.disk_pool.join();
    }

    fake_chain chain;
    ::asio::thread_pool chain_pool;
    std::thread::id chain_thread;
    std::unique_ptr<transport> service;
    std::uint16_t port{0};
};

} // namespace

TEST_CASE("grpc hash order on the wire is the reverse of the displayed hex", "[grpc chain]") {
    auto const genesis = domain::chain::block::genesis_mainnet();
    auto const display = encode_hash(genesis.hash());
    REQUIRE(display == "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");

    // The 32 bytes documented in node.proto for the mainnet genesis block.
    auto const wire = decode_base16("6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000");
    REQUIRE(wire.has_value());
    CHECK(as_bytes(genesis.hash()) == as_bytes(*wire));
}

TEST_CASE("grpc GetChainInfo reports network heights and best hash from the chain thread", "[grpc chain]") {
    harness h(v1::NETWORK_CHIPNET);
    auto const tip = domain::chain::block::genesis_mainnet().hash();
    h.chain.last_heights = [] { return std::expected<heights, int>(heights{801, 790}); };
    h.chain.block_hash = [tip](size_t height) {
        return height == 790 ? std::expected<hash_digest, int>(tip) : std::unexpected(1);
    };

    auto const info = client(h.port).chain_info();
    REQUIRE(info.status.ok());
    CHECK(info.response.network() == v1::NETWORK_CHIPNET);
    CHECK(info.response.block_height() == 790);
    CHECK(info.response.header_height() == 801);
    CHECK(info.response.best_block_hash() == as_bytes(tip));

    REQUIRE_FALSE(h.chain.threads.empty());
    for (auto const& id : h.chain.threads) {
        CHECK(id == h.chain_thread);   // chain state was never touched from a gRPC thread
    }
}

TEST_CASE("grpc GetChainInfo retries a tip that moved and reports one that will not hold still", "[grpc chain]") {
    harness h;
    h.chain.block_hash = [](size_t) { return std::expected<hash_digest, int>(null_hash); };

    SECTION("one move is absorbed") {
        auto reads = std::make_shared<std::atomic<int>>(0);
        h.chain.last_heights = [reads] {
            auto const n = (*reads)++;
            return std::expected<heights, int>(heights{900, static_cast<uint32_t>(n < 2 ? 800 + n : 801)});
        };
        auto const info = client(h.port).chain_info();
        REQUIRE(info.status.ok());
        CHECK(info.response.block_height() == 801);
    }
    SECTION("endless movement is ABORTED, not spun on") {
        auto reads = std::make_shared<std::atomic<int>>(0);
        h.chain.last_heights = [reads] {
            return std::expected<heights, int>(heights{900, static_cast<uint32_t>(800 + (*reads)++)});
        };
        auto const info = client(h.port).chain_info();
        CHECK(info.status.error_code() == grpc::StatusCode::ABORTED);
        CHECK(reads->load() == 6);   // three attempts of two reads
    }
}

TEST_CASE("grpc GetChainInfo surfaces unreadable chain state", "[grpc chain]") {
    harness h;
    h.chain.last_heights = [] { return std::unexpected(7); };
    CHECK(client(h.port).chain_info().status.error_code() == grpc::StatusCode::UNAVAILABLE);

    h.chain.last_heights = [] { return std::expected<heights, int>(heights{10, 10}); };
    h.chain.block_hash = [](size_t) { return std::unexpected(9); };
    CHECK(client(h.port).chain_info().status.error_code() == grpc::StatusCode::INTERNAL);
}

TEST_CASE("grpc GetBlock returns the stored body for the exact bytes asked", "[grpc chain]") {
    harness h;
    auto const genesis = domain::chain::block::genesis_mainnet();
    auto const body = to_data_chunk(static_cast<domain::chain::block const&>(genesis));
    REQUIRE(body.size() == 285);   // 80 byte header + one coinbase transaction
    auto const hash = genesis.hash();
    h.chain.height_of = [&](hash_digest const& asked) {
        return asked == hash ? std::expected<size_t, kth::code>(0)
                             : std::unexpected(kth::code(error::not_found));
    };
    h.chain.block_hash = [&](size_t) { return std::expected<hash_digest, int>(hash); };
    h.chain.raw = [&](uint32_t from, uint32_t to) {
        REQUIRE(from == to);
        return std::expected<std::vector<data_chunk>, database::result_code>(std::vector<data_chunk>{body});
    };

    auto const reply = client(h.port).block(as_bytes(hash));
    REQUIRE(reply.status.ok());
    CHECK(reply.response.height() == 0);
    CHECK(reply.response.raw_block() == as_bytes(body));

    // The hash reached the chain unchanged and unreversed; the disk read ran
    // neither on the chain executor nor on a transport thread.
    REQUIRE(h.chain.requested.size() == 1);
    CHECK(h.chain.requested[0] == hash);
    REQUIRE(h.chain.disk_threads.size() == 1);
    CHECK(h.chain.disk_threads[0] != h.chain_thread);
    CHECK(h.chain.disk_threads[0] != std::this_thread::get_id());

    CHECK(client(h.port).block(as_bytes(null_hash)).status.error_code() == grpc::StatusCode::NOT_FOUND);
}

TEST_CASE("grpc GetBlock rejects a body that is not the block asked for and a reorg during the read", "[grpc chain]") {
    harness h;
    auto const genesis = domain::chain::block::genesis_mainnet();
    auto const other = domain::chain::block::genesis_regtest();
    auto const hash = genesis.hash();
    h.chain.height_of = [&](hash_digest const&) { return std::expected<size_t, kth::code>(5); };
    h.chain.block_hash = [&](size_t) { return std::expected<hash_digest, int>(hash); };

    SECTION("the active body at that height belongs to another block") {
        auto const body = to_data_chunk(static_cast<domain::chain::block const&>(other));
        h.chain.raw = [&](uint32_t, uint32_t) {
            return std::expected<std::vector<data_chunk>, database::result_code>(std::vector<data_chunk>{body});
        };
        CHECK(client(h.port).block(as_bytes(hash)).status.error_code() == grpc::StatusCode::NOT_FOUND);
    }
    SECTION("the active chain no longer holds the block after the read") {
        auto const body = to_data_chunk(static_cast<domain::chain::block const&>(genesis));
        h.chain.raw = [&](uint32_t, uint32_t) {
            return std::expected<std::vector<data_chunk>, database::result_code>(std::vector<data_chunk>{body});
        };
        h.chain.block_hash = [&](size_t) { return std::expected<hash_digest, int>(other.hash()); };
        CHECK(client(h.port).block(as_bytes(hash)).status.error_code() == grpc::StatusCode::ABORTED);
    }
    SECTION("missing body") {
        h.chain.raw = [](uint32_t, uint32_t) { return std::unexpected(database::result_code::key_not_found); };
        CHECK(client(h.port).block(as_bytes(hash)).status.error_code() == grpc::StatusCode::NOT_FOUND);
    }
    SECTION("storage failure and a malformed record") {
        h.chain.raw = [](uint32_t, uint32_t) { return std::unexpected(database::result_code::other); };
        CHECK(client(h.port).block(as_bytes(hash)).status.error_code() == grpc::StatusCode::INTERNAL);
        h.chain.raw = [](uint32_t, uint32_t) {
            return std::expected<std::vector<data_chunk>, database::result_code>(std::vector<data_chunk>{data_chunk(10)});
        };
        CHECK(client(h.port).block(as_bytes(hash)).status.error_code() == grpc::StatusCode::INTERNAL);
    }
}

TEST_CASE("grpc GetBlock and GetTransaction reject malformed hashes without touching the chain", "[grpc chain]") {
    harness h;
    for (auto const length : {0, 1, 31, 33, 64}) {
        auto const bad = std::string(static_cast<size_t>(length), 'a');
        auto const block = client(h.port).block(bad);
        CHECK(block.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
        CHECK(block.status.error_message().find("32 bytes") != std::string::npos);
        auto const tx = client(h.port).transaction(bad);
        CHECK(tx.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
    }
    // A hex string is the classic mistake: 64 characters, but not 32 bytes of hash.
    auto const hex = encode_hash(domain::chain::block::genesis_mainnet().hash());
    CHECK(client(h.port).block(hex).status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);

    CHECK(h.chain.threads.empty());
    CHECK(h.chain.requested.empty());
}

TEST_CASE("grpc chain errors keep their meaning", "[grpc chain]") {
    harness h;
    auto const hash = as_bytes(null_hash);

    h.chain.height_of = [](hash_digest const&) { return std::unexpected(kth::code(error::service_stopped)); };
    CHECK(client(h.port).block(hash).status.error_code() == grpc::StatusCode::UNAVAILABLE);

    h.chain.height_of = [](hash_digest const&) { return std::unexpected(kth::code(error::operation_failed)); };
    auto const failed = client(h.port).block(hash);
    CHECK(failed.status.error_code() == grpc::StatusCode::INTERNAL);
    CHECK(failed.status.error_message().find("block lookup") != std::string::npos);
}

TEST_CASE("grpc GetTransaction distinguishes confirmed from mempool from unknown", "[grpc chain]") {
    harness h;
    auto const genesis = domain::chain::block::genesis_mainnet();
    auto const coinbase = genesis.transactions().front();
    auto const tx = std::make_shared<domain::message::transaction const>(coinbase);
    auto const expected = as_bytes(to_data_chunk(static_cast<domain::chain::transaction const&>(coinbase), true));
    auto const id = coinbase.hash();

    SECTION("confirmed carries height and position") {
        h.chain.confirmed = [&](hash_digest const&) {
            return std::expected<std::tuple<fake_chain::tx_ptr, size_t, size_t>, kth::code>(
                std::tuple{tx, size_t{3}, size_t{500}});   // (transaction, index, height)
        };
        auto const reply = client(h.port).transaction(as_bytes(id));
        REQUIRE(reply.status.ok());
        CHECK(reply.response.raw_transaction() == expected);
        REQUIRE(reply.response.has_confirmation());
        CHECK(reply.response.confirmation().block_height() == 500);
        CHECK(reply.response.confirmation().index_in_block() == 3);
    }
    SECTION("a mempool transaction has no confirmation") {
        h.chain.pooled = [&](hash_digest const&) {
            return std::expected<fake_chain::tx_ptr, kth::code>(tx);
        };
        auto const reply = client(h.port).transaction(as_bytes(id));
        REQUIRE(reply.status.ok());
        CHECK(reply.response.raw_transaction() == expected);
        CHECK_FALSE(reply.response.has_confirmation());
    }
    SECTION("unknown everywhere is NOT_FOUND") {
        CHECK(client(h.port).transaction(as_bytes(id)).status.error_code() == grpc::StatusCode::NOT_FOUND);
    }
    SECTION("a failing confirmed lookup is not mistaken for absence") {
        h.chain.confirmed = [](hash_digest const&) { return std::unexpected(kth::code(error::operation_failed)); };
        h.chain.pooled = [&](hash_digest const&) { return std::expected<fake_chain::tx_ptr, kth::code>(tx); };
        CHECK(client(h.port).transaction(as_bytes(id)).status.error_code() == grpc::StatusCode::INTERNAL);
    }
}

TEST_CASE("grpc GetBlock larger than the response limit is refused and not truncated", "[grpc chain]") {
    transport_options options;
    options.max_response_bytes = 100;   // below the 285 byte genesis block
    harness h(v1::NETWORK_MAINNET, options);
    auto const genesis = domain::chain::block::genesis_mainnet();
    auto const body = to_data_chunk(static_cast<domain::chain::block const&>(genesis));
    h.chain.height_of = [](hash_digest const&) { return std::expected<size_t, kth::code>(0); };
    h.chain.block_hash = [&](size_t) { return std::expected<hash_digest, int>(genesis.hash()); };
    h.chain.raw = [&](uint32_t, uint32_t) {
        return std::expected<std::vector<data_chunk>, database::result_code>(std::vector<data_chunk>{body});
    };
    auto const reply = client(h.port).block(as_bytes(genesis.hash()));
    CHECK(reply.status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED);
    CHECK(reply.status.error_message().find("100 byte limit") != std::string::npos);
}
