// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include <kth/blockchain/pools/mempool.hpp>
#include <kth/blockchain/pools/mempool_admissions.hpp>
#include <kth/domain.hpp>

using namespace kth;
using kth::blockchain::mempool;
using kth::blockchain::mempool_admissions;
using kth::blockchain::mempool_entry;

namespace {

domain::message::transaction::const_ptr make_tx(uint32_t locktime) {
    return std::make_shared<domain::message::transaction const>(
        domain::message::transaction(1, locktime, domain::chain::input::list{}, domain::chain::output::list{}));
}

// Admits a transaction as the chain does: an id, the pool, then the order.
hash_digest admit(mempool& pool, mempool_admissions& order, uint32_t locktime) {
    auto const tx = make_tx(locktime);
    auto const id = order.next_id();
    REQUIRE(pool.add(mempool_entry{tx, 1000, 250, 1, 0, id}));
    order.record(id, tx->hash(), pool);
    return tx->hash();
}

std::vector<hash_digest> hashes(std::vector<mempool_entry> const& entries) {
    std::vector<hash_digest> out;
    for (auto const& entry : entries) {
        out.push_back(entry.tx->hash());
    }
    return out;
}

} // namespace

TEST_CASE("mempool admissions: listed in order of admission, from a position, up to a limit",
          "[mempool][admissions]") {
    mempool pool(1, 2);
    mempool_admissions order;
    CHECK(order.last() == 0u);
    auto const a = admit(pool, order, 1);
    auto const b = admit(pool, order, 2);
    auto const c = admit(pool, order, 3);
    CHECK(order.last() == 3u);

    CHECK(hashes(order.after(0, 10, pool)) == std::vector<hash_digest>{a, b, c});
    CHECK(hashes(order.after(1, 10, pool)) == std::vector<hash_digest>{b, c});
    CHECK(hashes(order.after(0, 2, pool)) == std::vector<hash_digest>{a, b});
    CHECK(order.after(3, 10, pool).empty());
    auto const listed = order.after(1, 1, pool);
    REQUIRE(listed.size() == 1u);
    CHECK(listed[0].entry_id == 2u);
}

TEST_CASE("mempool admissions: what left the pool, or came back under a later id, is dropped as it is read",
          "[mempool][admissions]") {
    mempool pool(1, 2);
    mempool_admissions order;
    auto const a = admit(pool, order, 1);
    auto const b = admit(pool, order, 2);
    auto const c = admit(pool, order, 3);
    REQUIRE(pool.remove_with_descendants(b).size() == 1u);
    CHECK(hashes(order.after(0, 10, pool)) == std::vector<hash_digest>{a, c});
    CHECK(order.size() == 2u);

    // Mined, then pooled again by a reorg-like readmission: only its new place counts.
    REQUIRE(pool.remove_with_descendants(a).size() == 1u);
    auto const again = order.next_id();
    REQUIRE(pool.add(mempool_entry{make_tx(1), 1000, 250, 1, 0, again}));
    order.record(again, a, pool);
    CHECK(hashes(order.after(0, 10, pool)) == std::vector<hash_digest>{c, a});
    CHECK(order.size() == 2u);
}

TEST_CASE("mempool admissions: bounded by the pool even when nobody reads it",
          "[mempool][admissions]") {
    mempool pool(1, 2);
    mempool_admissions order;
    for (uint32_t i = 0; i < 5000; ++i) {
        auto const txid = admit(pool, order, i);
        REQUIRE(pool.remove_with_descendants(txid).size() == 1u);
    }
    // Nothing pooled: what is kept is at most the sweep's slack.
    CHECK(order.size() <= 1025u);
    CHECK(order.last() == 5000u);
}
