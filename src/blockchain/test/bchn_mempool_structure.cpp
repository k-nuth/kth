// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Ports of Bitcoin Cash Node src/test/mempool_tests.cpp (commit 07576013c91ff4a3a74acd85f189c69121cdad1b)
// through the public blockchain::mempool API: MempoolRemoveTest (dependency tree eviction) and
// MempoolAncestryTests (ancestor closures of chains, diamonds and forests).
// Original: Copyright (c) 2011-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.
//
// BCHN's removeRecursive(tx) has no public counterpart: Knuth evicts a pooled transaction and its descendants when a
// block confirms a CONFLICTING spend of one of its inputs, which is what the eviction half of the original asserts
// here. BCHN counts a transaction as its own ancestor and descendant (GetTransactionAncestry_deprecated_slow), Knuth's
// ancestors() / descendants() exclude it: every ancestor number below is Knuth's size + 1. BCHN's descendant number is
// CalculateDescendantMaximum: the largest descendant set (itself included) among the roots the transaction descends from;
// it is computed from parents() / ancestors() / descendants() and checked for every transaction of the original.
// Not ported: the fee fields of TestMemPoolEntryHelper (plumbing, no assertion depends on them); removeRecursive of a
// transaction that is not itself in the pool (BCHN uses it on a reorg to drop the children of a parent that could not be
// re-admitted; Knuth has no operation that removes a transaction and its in-pool descendants by id, and remove_for_block
// leaves the children of a confirmed transaction in place); clear().

#include <test_helpers.hpp>

#include <vector>

#include <kth/blockchain/pools/mempool.hpp>

using namespace kth;
using namespace kth::domain::chain;
using kth::blockchain::mempool;
using kth::blockchain::mempool_entry;

namespace {

hash_digest hash_of(uint64_t seed) {
    hash_digest h{};
    for (int i = 0; i < 8; ++i) h[i] = uint8_t(seed >> (8 * i));
    h[31] = 0x99;
    return h;
}

// A transaction with `outputs` distinct outputs spending `prevouts`; `tag` keeps otherwise equal txs distinct.
transaction make_tx(std::vector<output_point> const& prevouts, uint32_t outputs, uint64_t tag) {
    input::list ins;
    for (auto const& po : prevouts) ins.emplace_back(po, script{}, 0xffffffffu);
    output::list outs;
    for (uint32_t i = 0; i < outputs; ++i) {
        output o;
        o.set_value(1000u + tag * 100u + i);
        o.set_script(script{});
        outs.push_back(std::move(o));
    }
    return transaction{1u, 0u, std::move(ins), std::move(outs)};
}

output_point out(transaction const& tx, uint32_t index) { return output_point{tx.hash(), index}; }

mempool_entry entry_for(transaction const& tx, uint64_t tag) {
    return mempool_entry{std::make_shared<domain::message::transaction>(tx), 1000u + tag, uint32_t(tx.serialized_size(true)), 1u, tag};
}

block block_of(std::vector<transaction> const& txs) {
    return block{header{}, transaction::list(txs.begin(), txs.end())};
}

} // namespace

// MempoolRemoveTest: a parent with three children and three grandchildren.
TEST_CASE("BCHN mempool_tests - MempoolRemoveTest", "[bchn_non_vm][mempool_structure]") {
    auto const external = [](uint64_t seed) { return output_point{hash_of(seed), 0}; };
    auto const parent = make_tx({external(1)}, 3, 1);
    transaction const child[3] = {make_tx({out(parent, 0)}, 1, 2), make_tx({out(parent, 1)}, 1, 3), make_tx({out(parent, 2)}, 1, 4)};
    transaction const grandchild[3] = {make_tx({out(child[0], 0)}, 1, 5), make_tx({out(child[1], 0)}, 1, 6), make_tx({out(child[2], 0)}, 1, 7)};

    mempool mp{0x1111u, 0x2222u};
    // Nothing in the pool: a block that conflicts with the parent's input changes nothing
    mp.remove_for_block(block_of({make_tx({external(1)}, 1, 90)}));
    CHECK(mp.size() == 0u);

    // Just the parent
    REQUIRE(mp.add(entry_for(parent, 1)));
    CHECK(mp.size() == 1u);
    mp.remove_for_block(block_of({make_tx({external(1)}, 1, 91)}));      // conflicting spend of the parent's input
    CHECK(mp.size() == 0u);

    // Parent, children, grandchildren
    REQUIRE(mp.add(entry_for(parent, 1)));
    for (int i = 0; i < 3; i++) {
        REQUIRE(mp.add(entry_for(child[i], 2 + i)));
        REQUIRE(mp.add(entry_for(grandchild[i], 5 + i)));
    }
    CHECK(mp.size() == 7u);

    // Evict child[0] (a block spends the same parent output): grandchild[0] goes with it
    mp.remove_for_block(block_of({make_tx({out(parent, 0)}, 1, 92)}));
    CHECK(mp.size() == 5u);
    CHECK( ! mp.contains(child[0].hash()));
    CHECK( ! mp.contains(grandchild[0].hash()));
    CHECK(mp.contains(child[1].hash()));
    CHECK(mp.contains(grandchild[2].hash()));
    // the same conflict again changes nothing
    mp.remove_for_block(block_of({make_tx({out(parent, 0)}, 1, 93)}));
    CHECK(mp.size() == 5u);

    // Evict the parent: all children and grandchildren go
    mp.remove_for_block(block_of({make_tx({external(1)}, 1, 94)}));
    CHECK(mp.size() == 0u);
}

TEST_CASE("BCHN mempool_tests - MempoolAncestryTests", "[bchn_non_vm][mempool_structure]") {
    mempool mp{0x1111u, 0x2222u};
    uint64_t tag = 0;
    auto const base = [&](uint64_t seed) { return make_tx({output_point{hash_of(seed), 0}}, 1, ++tag); };
    auto const add = [&](mempool& pool, transaction const& tx) { REQUIRE(pool.add(entry_for(tx, ++tag))); };
    auto const ancestors = [](mempool const& pool, transaction const& tx) { return pool.ancestors(tx.hash()).size() + 1; };
    // BCHN's CalculateDescendantMaximum: walk to the roots (transactions without in-pool parents) and take the largest
    // descendant set (itself included) among them.
    // BCHN's CalculateDescendantMaximum: walk to the roots (transactions without in-pool parents) and take the largest
    // descendant set (itself included) among them.
    auto const descendants_of_root = [](mempool const& pool, transaction const& tx) {
        std::vector<hash_digest> roots;
        if (pool.parents(tx.hash()).empty()) {
            roots.push_back(tx.hash());
        } else {
            for (auto const& a : pool.ancestors(tx.hash())) {
                if (pool.parents(a).empty()) roots.push_back(a);
            }
        }
        size_t maximum = 0;
        for (auto const& root : roots) maximum = std::max(maximum, pool.descendants(root).size() + 1);
        return maximum;
    };

    // [tx1]
    auto const tx1 = base(1);
    add(mp, tx1);
    CHECK(ancestors(mp, tx1) == 1u);
    CHECK(descendants_of_root(mp, tx1) == 1u);

    // [tx1].0 <- [tx2]
    auto const tx2 = make_tx({out(tx1, 0)}, 2, ++tag);
    add(mp, tx2);
    CHECK(descendants_of_root(mp, tx1) == 2u);
    CHECK(descendants_of_root(mp, tx2) == 2u);
    CHECK(ancestors(mp, tx2) == 2u);

    // [tx1].0 <- [tx2].0 <- [tx3]
    auto const tx3 = make_tx({out(tx2, 0)}, 2, ++tag);
    add(mp, tx3);
    for (auto const* t : {&tx1, &tx2, &tx3}) CHECK(descendants_of_root(mp, *t) == 3u);
    CHECK(ancestors(mp, tx3) == 3u);

    // [tx1].0 <- [tx2].0 <- [tx3]
    //              \---1 <- [tx4]
    auto const tx4 = make_tx({out(tx2, 1)}, 2, ++tag);
    add(mp, tx4);
    for (auto const* t : {&tx1, &tx2, &tx3, &tx4}) CHECK(descendants_of_root(mp, *t) == 4u);
    CHECK(ancestors(mp, tx4) == 3u);

    // [ty1].0 <- [ty2].0 <- [ty3].0 <- [ty4].0 <- [ty5].0
    //                                              |
    // [tx1].0 <- [tx2].0 <- [tx3].0 <- [ty6] --->--/
    std::vector<transaction> ty;
    for (size_t i = 0; i < 5; ++i) {
        ty.push_back(i == 0 ? base(100) : make_tx({out(ty.back(), 0)}, 1, ++tag));
        add(mp, ty.back());
        CHECK(ancestors(mp, ty.back()) == i + 1);
        CHECK(descendants_of_root(mp, ty.back()) == i + 1);
    }
    auto const ty6 = make_tx({out(tx3, 0), out(ty[4], 0)}, 1, ++tag);
    add(mp, ty6);
    for (auto const* t : {&tx1, &tx2, &tx3, &tx4}) CHECK(descendants_of_root(mp, *t) == 5u);   // tx1, 2, 3, 4 and ty6
    CHECK(ancestors(mp, tx2) == 2u);
    CHECK(ancestors(mp, tx3) == 3u);
    CHECK(ancestors(mp, tx4) == 3u);
    for (size_t i = 0; i < 5; ++i) CHECK(descendants_of_root(mp, ty[i]) == 6u);               // ty1 .. ty6
    CHECK(descendants_of_root(mp, ty6) == 6u);                  // roots tx1 (5) and ty1 (6): the larger
    for (size_t i = 0; i < 5; ++i) CHECK(ancestors(mp, ty[i]) == i + 1);
    CHECK(ancestors(mp, ty6) == 9u);                           // tx1, 2, 3 and ty1 .. ty5, plus itself

    // [ta].0 <- [tb].0 -----<------- [td].0
    //            |                    |
    //            \---1 <- [tc].0 --<--/
    mempool diamond{0x3333u, 0x4444u};
    auto const ta = base(200);
    auto const tb = make_tx({out(ta, 0)}, 2, ++tag);
    auto const tc = make_tx({out(tb, 1)}, 1, ++tag);
    auto const td = make_tx({out(tb, 0), out(tc, 0)}, 1, ++tag);
    for (auto const* t : {&ta, &tb, &tc, &td}) add(diamond, *t);
    for (auto const* t : {&ta, &tb, &tc, &td}) CHECK(descendants_of_root(diamond, *t) == 4u);
    CHECK(ancestors(diamond, tb) == 2u);
    CHECK(ancestors(diamond, tc) == 3u);
    CHECK(ancestors(diamond, td) == 4u);
}
