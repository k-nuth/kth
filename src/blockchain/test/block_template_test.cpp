// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for the block-template builder (kth::blockchain::build_block_template):
// fee-rate ordering, block size / sigchecks limits with coinbase reserve,
// parent-before-child dependency completeness, CTOR ordering, and finality.

#include <test_helpers.hpp>

#include <catch2/catch_approx.hpp>

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
using kth::blockchain::mining_template;
using kth::blockchain::make_mining_template;

namespace {

// The builder takes a coherent set of entries rather than the pool, so that the
// obligation to have one lands on the caller holding the exclusion (#611). A
// test pool has no concurrent writer, so copying it out here is coherent by
// construction — that is exactly the assumption production could not make.
std::vector<mempool_entry> view_of(mempool const& pool) {
    std::vector<mempool_entry> entries;
    entries.reserve(pool.size());
    pool.for_each([&](mempool_entry const& entry) { entries.push_back(entry); });
    return entries;
}

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
                    uint32_t sequence = 0xffffffffu, uint32_t version = 1u) {
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

    return transaction{version, locktime, std::move(ins), std::move(outs)};
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
    auto const tpl = build_block_template(view_of(pool), unlimited_ctx());

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

    auto const tpl = build_block_template(view_of(pool), unlimited_ctx());

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

    auto const tpl = build_block_template(view_of(pool), ctx);

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

TEST_CASE("block_template: a larger coinbase size reserve shrinks the selection", "[block_template]") {
    mempool pool(0x1111u, 0x2222u);

    auto hi  = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto mid = as_ptr(make_tx({op(2, 0)}, 1, 2));
    auto lo  = as_ptr(make_tx({op(3, 0)}, 1, 3));
    REQUIRE(pool.add(make_entry(hi,  300u, 100u, 1u)));
    REQUIRE(pool.add(make_entry(mid, 200u, 100u, 1u)));
    REQUIRE(pool.add(make_entry(lo,  100u, 100u, 1u)));

    // The same 1250-byte block that fits two 100-byte txs under the default
    // 1000-byte reserve leaves room for only one once the reserve grows to 1100.
    auto ctx = unlimited_ctx();
    ctx.max_block_size = 1000u + 250u;
    ctx.coinbase_reserve_size = 1100u;

    auto const tpl = build_block_template(view_of(pool), ctx);

    REQUIRE(tpl.txs.size() == 1u);
    REQUIRE(tpl.total_fees == 300u);   // only the top fee-rate tx.
}

TEST_CASE("block_template: a larger coinbase sigchecks reserve caps selection", "[block_template]") {
    mempool pool(0x1111u, 0x2222u);

    auto a = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto b = as_ptr(make_tx({op(2, 0)}, 1, 2));
    REQUIRE(pool.add(make_entry(a, 300u, 100u, /*sigchecks*/ 40u)));
    REQUIRE(pool.add(make_entry(b, 200u, 100u, /*sigchecks*/ 40u)));

    // 200 sigchecks fits both under the default 100 reserve; a 130 reserve
    // leaves room for one.
    auto ctx = unlimited_ctx();
    ctx.max_block_sigchecks = 200u;
    ctx.coinbase_reserve_sigchecks = 130u;

    auto const tpl = build_block_template(view_of(pool), ctx);

    REQUIRE(tpl.txs.size() == 1u);
    REQUIRE(tpl.total_sigchecks == 40u);
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

    auto const tpl = build_block_template(view_of(pool), ctx);

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

    auto const tpl = build_block_template(view_of(pool), unlimited_ctx());

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

    auto const tpl = build_block_template(view_of(pool), ctx);

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

    auto const tpl = build_block_template(view_of(pool), unlimited_ctx());

    std::set<hash_digest> present;
    for (auto const& tx : tpl.txs) present.insert(tx->hash());
    REQUIRE(present.count(final_tx->hash()) == 1u);
    REQUIRE(present.count(non_final->hash()) == 0u);
}

// ---------------------------------------------------------------------------
// make_mining_template: the pure header-field assembly (subsidy, times).

TEST_CASE("make_mining_template computes min_time as MTP + 1", "[block_template]") {
    auto const t = make_mining_template(0x20000000u, null_hash, 1u, 0x1d00ffffu,
        /*mtp*/ 1000u, /*now*/ 5000u, 32000000u, 226950u, block_template{});
    REQUIRE(t.min_time == 1001u);
}

TEST_CASE("make_mining_template clamps current_time up to min_time", "[block_template]") {
    // now behind MTP+1 -> clamped to min_time.
    auto const behind = make_mining_template(0x20000000u, null_hash, 1u, 0x1d00ffffu,
        /*mtp*/ 4000u, /*now*/ 100u, 32000000u, 226950u, block_template{});
    REQUIRE(behind.current_time == 4001u);

    // now ahead of MTP+1 -> used as-is.
    auto const ahead = make_mining_template(0x20000000u, null_hash, 1u, 0x1d00ffffu,
        /*mtp*/ 1000u, /*now*/ 9000u, 32000000u, 226950u, block_template{});
    REQUIRE(ahead.current_time == 9000u);
}

TEST_CASE("make_mining_template coinbase value is subsidy plus fees", "[block_template]") {
    block_template selection{};
    selection.total_fees = 1234u;
    auto const t = make_mining_template(0x20000000u, null_hash, /*height*/ 1u,
        0x1d00ffffu, 1000u, 5000u, 32000000u, 226950u, std::move(selection));
    // Height 1 subsidy is 50 BCH.
    REQUIRE(t.coinbase_value == 5000000000ull + 1234u);
}

TEST_CASE("make_mining_template passes the header fields through", "[block_template]") {
    auto const t = make_mining_template(0x20000000u, null_hash, /*height*/ 7u,
        0x1d00ffffu, 1000u, 5000u, /*size*/ 32000000u, /*sigchecks*/ 226950u,
        block_template{});
    REQUIRE(t.version == 0x20000000u);
    REQUIRE(t.previous_block_hash == null_hash);
    REQUIRE(t.height == 7u);
    REQUIRE(t.bits == 0x1d00ffffu);
    REQUIRE(t.size_limit == 32000000u);
    REQUIRE(t.sigchecks_limit == 226950u);
}

// ---------------------------------------------------------------------------
// difficulty_from_bits: the compact nBits -> Bitcoin difficulty conversion.

TEST_CASE("difficulty_from_bits is 1.0 at the difficulty-1 target", "[block_template]") {
    using kth::blockchain::difficulty_from_bits;
    REQUIRE(difficulty_from_bits(0x1d00ffffu) == Catch::Approx(1.0));
}

TEST_CASE("difficulty_from_bits scales with the exponent", "[block_template]") {
    using kth::blockchain::difficulty_from_bits;
    // One lower exponent byte is a 256x harder target.
    REQUIRE(difficulty_from_bits(0x1c00ffffu) == Catch::Approx(256.0));
}

TEST_CASE("difficulty_from_bits is zero for a zero mantissa", "[block_template]") {
    using kth::blockchain::difficulty_from_bits;
    REQUIRE(difficulty_from_bits(0x1d000000u) == Catch::Approx(0.0));
}

// =============================================================================
// Why the builder takes entries and not the pool (#611)
// =============================================================================

TEST_CASE("an incoherent view selects a child whose parent is missing", "[block_template][coherence]") {
    // The dependency rule reads a prevout whose txid is absent from the view as
    // already confirmed — it has no other way to tell an unconfirmed parent from
    // a chain output. So on a view that is missing a parent it holds, the child
    // looks spendable and is selected alone, and the block would spend an output
    // that is neither in it nor on the chain.
    //
    // This is what the builder does with a set the pool was never in, which is
    // what walking the live map produced. It is not a defect of the selection:
    // no rule over a set can recover what the set omits. It is the reason the
    // coherent view is the caller's obligation, and the reason this signature
    // takes entries rather than the pool.
    auto const parent = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto const child = as_ptr(make_tx({output_point{parent->hash(), 0}}, 1, 2));

    std::vector<mempool_entry> const incoherent{make_entry(child, 5000, 250, 1)};

    auto const tpl = build_block_template(incoherent, unlimited_ctx());

    REQUIRE(tpl.txs.size() == 1);
    CHECK(tpl.txs.front()->hash() == child->hash());

    // Stated the other way round, since that is the property callers rely on:
    // the selection is closed under in-mempool parents *of the view given*, and
    // the parent is not in it.
    auto const& only_input = tpl.txs.front()->inputs().front();
    CHECK(only_input.previous_output().hash() == parent->hash());
}

TEST_CASE("a coherent view keeps the child with its parent", "[block_template][coherence]") {
    // The same two transactions, this time as a set the pool could have held.
    auto const parent = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto const child = as_ptr(make_tx({output_point{parent->hash(), 0}}, 1, 2));

    // The child pays far better, so fee-rate order alone would place it first;
    // only the dependency rule keeps them together and in CTOR order.
    std::vector<mempool_entry> const coherent{
        make_entry(parent, 100, 250, 1),
        make_entry(child, 50000, 250, 1)};

    auto const tpl = build_block_template(coherent, unlimited_ctx());

    REQUIRE(tpl.txs.size() == 2);
    check_template_invariants(tpl);

    std::set<hash_digest> selected;
    for (auto const& tx : tpl.txs) {
        selected.insert(tx->hash());
    }
    CHECK(selected.contains(parent->hash()));
    CHECK(selected.contains(child->hash()));
}

#if defined(KTH_CURRENCY_BCH)
namespace {
void use_faster_template_context(block_template_context& ctx, size_t activation_height) {
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    ctx.ticks = block_ticks::activated_at(activation_height);
    ctx.sequence_locks = true;
#else
    // The base builder has no schedule or BIP68 context. Keep the same txs,
    // candidate height and expected selection when exercising that boundary.
    (void)ctx;
    (void)activation_height;
#endif
}
} // namespace

TEST_CASE("block_template: a formerly mature coinbase spend is filtered under the shorter schedule", "[block_template][faster_blocks][template_context]") {
    auto funding = op(900, 0);
    funding.validation.coinbase = true;
    funding.validation.height = 90;
    auto const parent = as_ptr(make_tx({funding}, 1, 700));
    auto const child = as_ptr(make_tx({output_point{parent->hash(), 0}}, 1, 701));
    std::vector<mempool_entry> const entries{
        make_entry(parent, 100, 250, 1), make_entry(child, 1000, 250, 1)};

    auto ctx = unlimited_ctx();
    ctx.height = 200;
    auto const old_schedule = build_block_template(entries, ctx);
    REQUIRE(old_schedule.txs.size() == 2);

    // With H_a100, this candidate represents12000 ticks of coin age, not
    // the60000 needed. The dependent child must be excluded with its parent.
    use_faster_template_context(ctx, 100);
    auto const early = build_block_template(entries, ctx);
    CHECK(early.txs.empty());
    CHECK(early.total_fees == 0);
    CHECK(early.total_size == 0);

    ctx.height = 1000; // T1000-T90 =114000-54000 =60000, exactly mature.
    auto const mature = build_block_template(entries, ctx);
    REQUIRE(mature.txs.size() == 2);
    check_template_invariants(mature);
}

TEST_CASE("block_template: relative height locks use the candidate branch ticks", "[block_template][faster_blocks][template_context]") {
    auto funding = op(901, 0);
    funding.validation.height = 100;
    auto tx = make_tx({funding}, 1, 702, 0, 2, 2); // version2, two600-tick units
    std::vector<mempool_entry> const entries{make_entry(as_ptr(tx), 100, 250, 1)};
    auto ctx = unlimited_ctx();
    ctx.height = 102;
    REQUIRE(build_block_template(entries, ctx).txs.size() == 1);

    use_faster_template_context(ctx, 100);
    CHECK(build_block_template(entries, ctx).txs.empty());
    ctx.height = 119;
    CHECK(build_block_template(entries, ctx).txs.empty());
    ctx.height = 120; // T120-T100 =1200, exactly the encoded wait.
    REQUIRE(build_block_template(entries, ctx).txs.size() == 1);
}

TEST_CASE("block_template: an unconfirmed parent's relative age stays zero as the tip advances", "[block_template][faster_blocks][template_context]") {
    auto const parent = as_ptr(make_tx({op(902, 0)}, 1, 703));
    auto funding = output_point{parent->hash(), 0};
    // Cached admission metadata can be old. Membership in this immutable
    // snapshot is what determines that the parent confirms in the candidate.
    funding.validation.height = 1;
    funding.validation.median_time_past = 1;
    funding.validation.from_mempool = true;
    auto child = make_tx({funding}, 1, 704, 0, 1, 2);
    auto ctx = unlimited_ctx();
    ctx.height = 200;
    use_faster_template_context(ctx, 100);
    std::vector<mempool_entry> entries{
        make_entry(parent, 100, 250, 1), make_entry(as_ptr(child), 1000, 250, 1)};
    auto const selected = build_block_template(entries, ctx);
    CHECK(selected.txs.size() == 1);
    if (selected.txs.size() == 1) {
        CHECK(selected.txs.front()->hash() == parent->hash());
    }

    // Zero relative delay allows the same-block dependency, as before.
    auto child_zero = make_tx({funding}, 1, 704, 0, 0, 2);
    entries[1] = make_entry(as_ptr(child_zero), 1000, 250, 1);
    REQUIRE(build_block_template(entries, ctx).txs.size() == 2);
}

TEST_CASE("block_template: relative MTP locks retain the512-second boundary", "[block_template][faster_blocks][template_context]") {
    auto funding = op(903, 0);
    funding.validation.height = 100;
    funding.validation.median_time_past = 1'000'000;
    auto tx = make_tx({funding}, 1, 705, 0, relative_locktime_time_locked | 1u, 2);
    std::vector<mempool_entry> const entries{make_entry(as_ptr(tx), 100, 250, 1)};
    auto ctx = unlimited_ctx();
    ctx.height = 200;
    use_faster_template_context(ctx, 100);
    ctx.median_time_past = 1'000'511;
    CHECK(build_block_template(entries, ctx).txs.empty());
    ctx.median_time_past = 1'000'512;
    CHECK(build_block_template(entries, ctx).txs.size() == 1);
}
#endif
