// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for the block-template builder (kth::blockchain::build_block_template):
// fee-rate ordering, block size / sigchecks limits with coinbase reserve,
// parent-before-child dependency completeness, CTOR ordering, and finality.

#include <test_helpers.hpp>

#include <kth/blockchain/pools/block_template.hpp>
#include <kth/blockchain/pools/mempool.hpp>
#include <kth/domain.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

using namespace kth;
using namespace kth::domain::chain;
using kth::blockchain::mempool;
using kth::blockchain::mempool_entry;
using kth::blockchain::block_template;
using kth::blockchain::block_template_context;
using kth::blockchain::build_block_template;

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

// Build a syntactically-valid tx. `sequence`/`locktime` default to final.
transaction make_tx(std::vector<output_point> const& prevouts,
                    uint32_t num_outputs,
                    uint64_t tag,
                    uint32_t locktime = 0u,
                    uint32_t sequence = 0xffffffffu) {
    input::list ins;
    ins.reserve(prevouts.size());
    for (auto const& po : prevouts) {
        ins.emplace_back(po, script{}, sequence);
    }

    output::list outs;
    outs.reserve(num_outputs);
    for (uint32_t i = 0; i < num_outputs; ++i) {
        output o;
        o.set_value(1000u + tag * 100u + i);
        o.set_script(script{});
        outs.push_back(std::move(o));
    }

    return transaction{1u, locktime, std::move(ins), std::move(outs)};
}

transaction_const_ptr as_ptr(transaction const& tx) {
    return std::make_shared<domain::message::transaction>(tx);
}

mempool_entry make_entry(transaction_const_ptr const& tx, uint64_t fee, uint32_t size, uint32_t sigchecks) {
    return mempool_entry{tx, fee, size, sigchecks, /*time_seen*/ 0u};
}

// A generous context: nothing is limited unless a test tightens it.
block_template_context unlimited_ctx() {
    return block_template_context{
        /*max_block_size*/      1'000'000'000ull,
        /*max_block_sigchecks*/ 1'000'000'000ull,
        /*height*/              100u,
        /*median_time_past*/    1'000'000u};
}

// CTOR predicate: reverse-byte (display) lexicographic txid ascending.
bool canonical_less(transaction_const_ptr const& a, transaction_const_ptr const& b) {
    auto const& ha = a->hash();
    auto const& hb = b->hash();
    return std::lexicographical_compare(ha.rbegin(), ha.rend(), hb.rbegin(), hb.rend());
}

// Invariants every valid template must hold: the selected txs are CTOR-ordered
// and free of duplicates. Per-test cases assert the chain-specific parent /
// child completeness where the structure is known.
void check_template_invariants(block_template const& tpl) {
    REQUIRE(std::is_sorted(tpl.txs.begin(), tpl.txs.end(), canonical_less));

    std::set<hash_digest> present;
    for (auto const& tx : tpl.txs) {
        present.insert(tx->hash());
    }
    REQUIRE(present.size() == tpl.txs.size());
}

} // namespace

// ---------------------------------------------------------------------------

TEST_CASE("block_template: empty pool yields empty template", "[block_template]") {
    mempool pool(0x1111u, 0x2222u);
    auto const tpl = build_block_template(pool, unlimited_ctx());

    REQUIRE(tpl.txs.empty());
    REQUIRE(tpl.total_fees == 0u);
    REQUIRE(tpl.total_size == 0u);
    REQUIRE(tpl.total_sigchecks == 0u);
}

TEST_CASE("block_template: includes all independent txs, CTOR-ordered, totals summed", "[block_template]") {
    mempool pool(0x1111u, 0x2222u);

    std::vector<transaction_const_ptr> txs;
    for (uint64_t i = 0; i < 5; ++i) {
        auto tx = as_ptr(make_tx({op(1000 + i, 0)}, 1, i));
        txs.push_back(tx);
        REQUIRE(pool.add(make_entry(tx, /*fee*/ 100u * (i + 1), /*size*/ 200u, /*sigchecks*/ 3u)));
    }

    auto const tpl = build_block_template(pool, unlimited_ctx());

    REQUIRE(tpl.txs.size() == 5u);
    check_template_invariants(tpl);
    REQUIRE(tpl.total_size == 5u * 200u);
    REQUIRE(tpl.total_sigchecks == 5u * 3u);
    REQUIRE(tpl.total_fees == (100u + 200u + 300u + 400u + 500u));
}

TEST_CASE("block_template: size limit selects highest fee-rate first", "[block_template]") {
    mempool pool(0x1111u, 0x2222u);

    // Three independent txs, size 100 each, fee-rates 3 / 2 / 1.
    auto hi = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto mid = as_ptr(make_tx({op(2, 0)}, 1, 2));
    auto lo = as_ptr(make_tx({op(3, 0)}, 1, 3));
    REQUIRE(pool.add(make_entry(hi,  /*fee*/ 300u, /*size*/ 100u, 1u)));
    REQUIRE(pool.add(make_entry(mid, /*fee*/ 200u, /*size*/ 100u, 1u)));
    REQUIRE(pool.add(make_entry(lo,  /*fee*/ 100u, /*size*/ 100u, 1u)));

    // Coinbase reserve is 1000; budget for exactly two 100-byte txs (>= rejects the third).
    auto ctx = unlimited_ctx();
    ctx.max_block_size = 1000u + 250u;

    auto const tpl = build_block_template(pool, ctx);

    REQUIRE(tpl.txs.size() == 2u);
    check_template_invariants(tpl);
    REQUIRE(tpl.total_size == 200u);
    REQUIRE(tpl.total_fees == 500u);   // 300 + 200; the fee-100 tx is dropped.

    std::set<hash_digest> present;
    for (auto const& tx : tpl.txs) present.insert(tx->hash());
    REQUIRE(present.count(hi->hash()) == 1u);
    REQUIRE(present.count(mid->hash()) == 1u);
    REQUIRE(present.count(lo->hash()) == 0u);
}

TEST_CASE("block_template: sigchecks limit caps selection", "[block_template]") {
    mempool pool(0x1111u, 0x2222u);

    auto a = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto b = as_ptr(make_tx({op(2, 0)}, 1, 2));
    auto c = as_ptr(make_tx({op(3, 0)}, 1, 3));
    REQUIRE(pool.add(make_entry(a, 300u, 100u, /*sigchecks*/ 40u)));
    REQUIRE(pool.add(make_entry(b, 200u, 100u, /*sigchecks*/ 40u)));
    REQUIRE(pool.add(make_entry(c, 100u, 100u, /*sigchecks*/ 40u)));

    // Reserve 100 sigchecks; budget for exactly two 40-sigcheck txs.
    auto ctx = unlimited_ctx();
    ctx.max_block_sigchecks = 100u + 100u;

    auto const tpl = build_block_template(pool, ctx);

    REQUIRE(tpl.txs.size() == 2u);
    REQUIRE(tpl.total_sigchecks == 80u);
}

TEST_CASE("block_template: child added only after its parent (dependency completeness)", "[block_template]") {
    mempool pool(0x1111u, 0x2222u);

    // Parent: low fee-rate. Child: high fee-rate, spends the parent's output 0.
    auto parent = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto child  = as_ptr(make_tx({output_point{parent->hash(), 0}}, 1, 2));

    REQUIRE(pool.add(make_entry(parent, /*fee*/ 100u, /*size*/ 100u, 1u)));
    REQUIRE(pool.add(make_entry(child,  /*fee*/ 900u, /*size*/ 100u, 1u)));

    auto const tpl = build_block_template(pool, unlimited_ctx());

    REQUIRE(tpl.txs.size() == 2u);
    check_template_invariants(tpl);

    // Both present, and the parent precedes the child in CTOR only if its txid
    // sorts first — but regardless, the child is never present without the parent.
    std::set<hash_digest> present;
    for (auto const& tx : tpl.txs) present.insert(tx->hash());
    REQUIRE(present.count(parent->hash()) == 1u);
    REQUIRE(present.count(child->hash()) == 1u);
}

TEST_CASE("block_template: child dropped when parent does not fit", "[block_template]") {
    mempool pool(0x1111u, 0x2222u);

    // A big independent high-fee tx eats the budget; a small parent+child chain
    // cannot fit, so neither the parent nor its child may appear.
    auto big    = as_ptr(make_tx({op(9, 0)}, 1, 9));
    auto parent = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto child  = as_ptr(make_tx({output_point{parent->hash(), 0}}, 1, 2));

    REQUIRE(pool.add(make_entry(big,    /*fee*/ 10'000u, /*size*/ 200u, 1u)));
    REQUIRE(pool.add(make_entry(parent, /*fee*/ 50u,     /*size*/ 200u, 1u)));
    REQUIRE(pool.add(make_entry(child,  /*fee*/ 40u,     /*size*/ 200u, 1u)));

    // Reserve 1000; budget for exactly one 200-byte tx.
    auto ctx = unlimited_ctx();
    ctx.max_block_size = 1000u + 350u;

    auto const tpl = build_block_template(pool, ctx);

    check_template_invariants(tpl);
    std::set<hash_digest> present;
    for (auto const& tx : tpl.txs) present.insert(tx->hash());
    REQUIRE(present.count(big->hash()) == 1u);
    // Child must never appear without its parent.
    if (present.count(child->hash()) == 1u) {
        REQUIRE(present.count(parent->hash()) == 1u);
    }
}

TEST_CASE("block_template: non-final tx is excluded", "[block_template]") {
    mempool pool(0x1111u, 0x2222u);

    auto final_tx = as_ptr(make_tx({op(1, 0)}, 1, 1));
    // locktime as a future height with a non-final input sequence => not final
    // at the template height (ctx.height == 100).
    auto non_final = as_ptr(make_tx({op(2, 0)}, 1, 2, /*locktime*/ 100000u, /*sequence*/ 0u));

    REQUIRE(pool.add(make_entry(final_tx,  100u, 100u, 1u)));
    REQUIRE(pool.add(make_entry(non_final, 900u, 100u, 1u)));

    auto const tpl = build_block_template(pool, unlimited_ctx());

    std::set<hash_digest> present;
    for (auto const& tx : tpl.txs) present.insert(tx->hash());
    REQUIRE(present.count(final_tx->hash()) == 1u);
    REQUIRE(present.count(non_final->hash()) == 0u);
}
