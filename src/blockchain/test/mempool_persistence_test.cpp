// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Round-trip tests for mempool.dat persistence (kth::database::store_mempool /
// load_mempool): serialization fidelity, time_seen ordering, and graceful
// handling of a missing / corrupt file. Lives in the blockchain test suite
// because the database module has no active test target of its own.

#include <test_helpers.hpp>

#include <kth/database/mempool_store.hpp>
#include <kth/domain.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace kth;
using namespace kth::domain::chain;
using kth::database::mempool_stored_tx;
using kth::database::store_mempool;
using kth::database::load_mempool;

namespace {

hash_digest make_hash(uint64_t seed) {
    hash_digest h{};
    for (int i = 0; i < 8; ++i) {
        h[i] = static_cast<uint8_t>(seed >> (8 * i));
    }
    h[16] = static_cast<uint8_t>(seed >> 5);
    h[31] = static_cast<uint8_t>(seed >> 11);
    return h;
}

output_point op(uint64_t seed, uint32_t index) {
    return output_point{make_hash(seed), index};
}

transaction make_tx(std::vector<output_point> const& prevouts, uint32_t num_outputs, uint64_t tag) {
    input::list ins;
    ins.reserve(prevouts.size());
    for (auto const& po : prevouts) {
        ins.emplace_back(po, script{}, 0xffffffffu);
    }
    output::list outs;
    outs.reserve(num_outputs);
    for (uint32_t i = 0; i < num_outputs; ++i) {
        output o;
        o.set_value(1000u + tag * 100u + i);
        o.set_script(script{});
        outs.push_back(std::move(o));
    }
    return transaction{1u, 0u, std::move(ins), std::move(outs)};
}

transaction_const_ptr as_ptr(transaction const& tx) {
    return std::make_shared<domain::message::transaction>(tx);
}

mempool_stored_tx stored(transaction_const_ptr const& tx, uint64_t time_seen) {
    return mempool_stored_tx{tx, time_seen};
}

// A per-test unique temp path that is removed on scope exit.
struct temp_dat {
    std::filesystem::path path;
    explicit temp_dat(char const* name)
        : path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove(path);
    }
    ~temp_dat() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path.string() + ".new", ec);
    }
};

} // namespace

// ---------------------------------------------------------------------------

TEST_CASE("mempool persistence: empty set round-trips to empty", "[mempool_persistence]") {
    temp_dat dat("kth_mp_persist_empty.dat");

    REQUIRE(store_mempool(dat.path, {}));
    auto const loaded = load_mempool(dat.path);
    REQUIRE(loaded.empty());
}

TEST_CASE("mempool persistence: round-trips txs, ordered by time_seen", "[mempool_persistence]") {
    temp_dat dat("kth_mp_persist_rt.dat");

    auto a = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto b = as_ptr(make_tx({op(2, 0)}, 1, 2));
    auto c = as_ptr(make_tx({op(3, 0)}, 1, 3));

    // Deliberately out-of-order time_seen; store must write them sorted ascending.
    std::vector<mempool_stored_tx> txs{stored(a, 300u), stored(b, 100u), stored(c, 200u)};
    REQUIRE(store_mempool(dat.path, txs));

    auto const loaded = load_mempool(dat.path);
    REQUIRE(loaded.size() == 3u);
    REQUIRE(loaded[0].time_seen == 100u);
    REQUIRE(loaded[1].time_seen == 200u);
    REQUIRE(loaded[2].time_seen == 300u);
    REQUIRE(loaded[0].tx->hash() == b->hash());
    REQUIRE(loaded[1].tx->hash() == c->hash());
    REQUIRE(loaded[2].tx->hash() == a->hash());
}

TEST_CASE("mempool persistence: tx content survives the round-trip", "[mempool_persistence]") {
    temp_dat dat("kth_mp_persist_content.dat");

    auto tx = as_ptr(make_tx({op(7, 0), op(8, 1)}, 3, 42));
    REQUIRE(store_mempool(dat.path, {stored(tx, 555u)}));

    auto const loaded = load_mempool(dat.path);
    REQUIRE(loaded.size() == 1u);
    // Equal txid == equal serialization (txid is the double-SHA256 of the tx).
    REQUIRE(loaded[0].tx->hash() == tx->hash());
    REQUIRE(loaded[0].tx->inputs().size() == 2u);
    REQUIRE(loaded[0].tx->outputs().size() == 3u);
    REQUIRE(loaded[0].time_seen == 555u);
}

TEST_CASE("mempool persistence: missing file loads to empty", "[mempool_persistence]") {
    auto const missing = std::filesystem::temp_directory_path() / "kth_mp_persist_does_not_exist.dat";
    std::filesystem::remove(missing);
    auto const loaded = load_mempool(missing);
    REQUIRE(loaded.empty());
}

TEST_CASE("mempool persistence: corrupt file loads to empty without throwing", "[mempool_persistence]") {
    temp_dat dat("kth_mp_persist_corrupt.dat");
    {
        std::ofstream out(dat.path, std::ios::binary | std::ios::trunc);
        char const garbage[] = "not a valid mempool.dat header at all";
        out.write(garbage, sizeof(garbage));
    }
    auto const loaded = load_mempool(dat.path);
    REQUIRE(loaded.empty());
}
