// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(KTH_CURRENCY_BCH)
#include <test_helpers.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <kth/infrastructure/utility/timer.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/infrastructure/math/hash.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::blockchain;
using namespace kth::test;

// =============================================================================
// CHIP-2025-03 Faster Blocks through validate_block_batch
// =============================================================================
//
// The observable behaviour of the consensus rules that depend on the tick
// schedule, driven the way a node drives them: headers into the index, then the
// batch. Regtest activates by the same MTP predicate as every other network, so
// the activation time is chosen to land H_a at a known height of a chain whose
// timestamps are base + 600 * height (MTP(h) = timestamp(h - 5) for h >= 10).
//
//   * subsidy      the coinbase of a 60-tick block may claim only a tenth
//   * coinbase     version 1 + locktime 0 + outputs >= 25 BCH is invalid from the
//                  block built on H_a, and only from there
//   * maturity     100 ten-minute block-equivalents, in ticks, across H_a
//
// Each case has a control that differs only in the property under test.

namespace {

using domain::chain::block;
using domain::chain::block_ticks;
using domain::chain::input;
using domain::chain::output;
using domain::chain::output_point;
using domain::chain::script;
using domain::chain::transaction;

constexpr uint32_t h_a = 120;
constexpr uint64_t coin = 100'000'000;

struct spec {
    uint32_t len = h_a + 3;
    uint64_t coinbase_value_override_at = 0;      // height; 0 = none
    uint64_t coinbase_value_override = 0;
    uint32_t coinbase_version = 1;
    uint32_t coinbase_locktime = 0;
    uint32_t coinbase_sequence = 0xffffffff;
    domain::config::network consensus_network = domain::config::network::regtest;
    uint32_t spend_coinbase_of = 0;               // height whose coinbase is spent; 0 = none
    uint32_t spend_at = 0;
    uint32_t padding_transactions = 0;
    uint32_t padding_height = h_a + 1;
    uint64_t padded_block_size = 0;
    bool unlocked_spend = false;
    uint32_t spend_locktime = 0;
    uint32_t spend_sequence = 0xffffffff;
    uint32_t spend_version = 1;
};

uint32_t base_time(uint32_t /*len*/) {
    return 1'700'000'000; // Fixed historical timestamps; no wall-clock boundary dependency.
}

// MTP(h_a) is the first to reach it: timestamp(h_a - 5) == activation, and
// MTP(h_a - 1) = timestamp(h_a - 6) is one block short of it.
uint64_t activation_time(uint32_t base) {
    return uint64_t(base) + uint64_t(h_a - 5) * 600;
}

transaction coinbase(uint32_t height, uint64_t value, uint32_t version, uint32_t locktime, uint32_t sequence) {
    auto cb = coinbase_tx(height, 0, value);
    input::list inputs = cb.inputs();
    inputs.front().set_sequence(sequence);
    output::list outputs = cb.outputs();
    return transaction(version, locktime, std::move(inputs), std::move(outputs));
}

block mine(hash_digest const& prev, uint32_t timestamp, transaction::list txs) {
    block candidate(domain::chain::header{0x20000000, prev, null_hash, timestamp, regtest_bits, 0}, std::move(txs));
    auto const merkle = candidate.generate_merkle_root();
    for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
        domain::chain::header header{0x20000000, prev, merkle, timestamp, regtest_bits, uint32_t(nonce)};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), /*retarget*/ false)) {
            return block(std::move(header), transaction::list(candidate.transactions()));
        }
    }
    throw std::runtime_error("no nonce satisfied the regtest target");
}

std::vector<domain::message::block::const_ptr> build_chain(spec const& sp, uint32_t base, domain::script_flags_t flags) {
    // The branch's own schedule, from how the chain was constructed.
    auto const ticks = block_ticks::activated_at(h_a);

    std::vector<domain::message::block::const_ptr> blocks;
    blocks.reserve(sp.len);
    auto prev = block::genesis_regtest().hash();

    for (uint32_t h = 1; h <= sp.len; ++h) {
        transaction::list txs;
        uint64_t fees = 0;
        std::vector<transaction> extra;

        if (sp.spend_coinbase_of != 0 && h == sp.spend_at) {
            auto const& source = blocks[sp.spend_coinbase_of - 1]->transactions().front();
            constexpr uint64_t fee = 1000;
            if (sp.unlocked_spend) {
                input::list inputs;
                inputs.emplace_back(output_point{source.hash(), 0}, script{}, sp.spend_sequence);
                output::list outputs;
                outputs.emplace_back(source.outputs()[0].value() - fee, script{}, domain::chain::token_data_opt{});
                extra.emplace_back(sp.spend_version, sp.spend_locktime, std::move(inputs), std::move(outputs));
            } else {
                extra.push_back(spend_p2pkh(source, 0, source.outputs()[0].value() - fee, flags));
            }
            fees = fee;
        }

        if (sp.padding_transactions && h == sp.padding_height) {
            for (uint32_t fund = 1; fund <= sp.padding_transactions; ++fund) {
                auto const& source = blocks[fund - 1]->transactions().front();
                input::list inputs;
                inputs.emplace_back(output_point{source.hash(), 0}, script{}, 0xffffffff);
                output::list outputs;
                outputs.emplace_back(source.outputs()[0].value(),
                    script{data_chunk(500'000, 0), false}, domain::chain::token_data_opt{});
                extra.emplace_back(1, 0, std::move(inputs), std::move(outputs));
            }
        }

        auto const value = (sp.coinbase_value_override_at == h)
            ? sp.coinbase_value_override
            : block::subsidy(ticks, h, /*retarget*/ false) + fees;
        bool const shaped = (sp.coinbase_value_override_at == h);
        auto cb = coinbase(h, value, shaped ? sp.coinbase_version : 1u, shaped ? sp.coinbase_locktime : 0u,
            shaped ? sp.coinbase_sequence : 0xffffffffu);
        if ((sp.unlocked_spend && h == sp.spend_coinbase_of) || h <= sp.padding_transactions) {
            domain::machine::operation::list ops;
            ops.emplace_back(domain::machine::opcode::push_positive_1);
            output::list outs;
            outs.emplace_back(value, script{std::move(ops)}, domain::chain::token_data_opt{});
            cb = transaction(cb.version(), cb.locktime(), input::list(cb.inputs()), std::move(outs));
        }
        txs.push_back(std::move(cb));
        for (auto& tx : extra) txs.push_back(std::move(tx));

        if (sp.padded_block_size != 0 && h == sp.padding_height) {
            REQUIRE(sp.padding_transactions == 1);
            block const probe(domain::chain::header{}, transaction::list(txs));
            auto const overhead = probe.serialized_size() - 500'000u;
            // Keep CompactSize's five-byte script-length encoding unchanged.
            REQUIRE(sp.padded_block_size > overhead + 65'535u);
            auto const& padded = txs.back();
            auto outputs = padded.outputs();
            outputs.front().set_script(script{data_chunk(sp.padded_block_size - overhead, 0), false});
            txs.back() = transaction(padded.version(), padded.locktime(), input::list(padded.inputs()), std::move(outputs));
            REQUIRE(block(domain::chain::header{}, transaction::list(txs)).serialized_size() == sp.padded_block_size);
        }

        auto mined = mine(prev, base + h * 600, std::move(txs));
        prev = mined.hash();
        blocks.push_back(std::make_shared<domain::message::block const>(std::move(mined)));
    }
    return blocks;
}

code run(char const* name, spec const& sp) {
    chain_fixture fixture(name);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // The index fixture and PoW remain regtest. Selected consensus settings
    // may seed chipnet's two-megabyte limit; this does not model live chipnet.
    settings s(sp.consensus_network);
    s.retarget = false;
    auto const base = base_time(sp.len);
    s.cantor_activation_time = activation_time(base);

    auto const blocks = build_chain(sp, base, s.enabled_flags());

    domain::message::header::list headers;
    headers.reserve(blocks.size());
    for (auto const& blk : blocks) headers.push_back(blk->header());
    auto const added = fixture.organizer().add_headers(headers);
    REQUIRE((added.headers_added == blocks.size() || added.error == error::stale_chain));
    return validate_block_batch(fixture.chain(), s, sp.consensus_network, blocks, 1);
}

} // namespace

// -----------------------------------------------------------------------------
// Subsidy
// -----------------------------------------------------------------------------

TEST_CASE("faster blocks - a 60-tick coinbase may claim exactly a tenth of the subsidy",
          "[batch][faster_blocks]") {
    spec sp;
    CHECK(run("fb_subsidy_exact", sp) == error::success);
}

TEST_CASE("faster blocks - a coinbase claiming more than a tenth after H_a is rejected",
          "[batch][faster_blocks]") {
    spec sp;
    sp.coinbase_value_override_at = h_a + 1;
    sp.coinbase_value_override = 5 * coin + 1;          // one satoshi over 50 BCH * 60 / 600
    CHECK(run("fb_subsidy_over", sp) == error::coinbase_value_limit);
}

TEST_CASE("faster blocks - H_a itself still earns the full 600-tick subsidy",
          "[batch][faster_blocks]") {
    spec sp;
    sp.coinbase_value_override_at = h_a;
    sp.coinbase_value_override = 20 * coin;             // under 25 BCH and well under 50 BCH
    CHECK(run("fb_subsidy_at_ha", sp) == error::success);
}

// -----------------------------------------------------------------------------
// Coinbase shape
// -----------------------------------------------------------------------------

TEST_CASE("faster blocks - the forbidden coinbase shape is rejected from the block built on H_a",
          "[batch][faster_blocks]") {
    spec sp;
    sp.coinbase_value_override_at = h_a + 1;
    sp.coinbase_value_override = 30 * coin;             // version 1, locktime 0, >= 25 BCH
    CHECK(run("fb_shape_after", sp) == error::coinbase_shape);
}

TEST_CASE("faster blocks - the same coinbase shape is exempt at H_a",
          "[batch][faster_blocks]") {
    spec sp;
    sp.coinbase_value_override_at = h_a;
    sp.coinbase_value_override = 30 * coin;
    CHECK(run("fb_shape_at_ha", sp) == error::success);
}

TEST_CASE("faster blocks - another version or a locktime escapes the shape rule (and meets the value limit instead)",
          "[batch][faster_blocks]") {
    spec version2;
    version2.coinbase_value_override_at = h_a + 1;
    version2.coinbase_value_override = 30 * coin;
    version2.coinbase_version = 2;
    CHECK(run("fb_shape_v2", version2) == error::coinbase_value_limit);

    spec locktime;
    locktime.coinbase_value_override_at = h_a + 1;
    locktime.coinbase_value_override = 30 * coin;
    locktime.coinbase_locktime = 1;
    CHECK(run("fb_shape_lt", locktime) == error::coinbase_value_limit);
}

// -----------------------------------------------------------------------------
// Coinbase maturity in ticks
// -----------------------------------------------------------------------------
//
// The coinbase of block 21 is created at T = 21 * 600 = 12600. 100 blocks later
// at the old cadence it would be mature at 121, but 121 is the first 60-tick
// block (T = 72060), only 59460 ticks on. It matures once T(b) >= 72600, which is
// the tenth block after H_a: 130.

TEST_CASE("faster blocks - a coinbase is not mature 100 blocks later when those blocks are 60-tick",
          "[batch][faster_blocks]") {
    spec sp;
    sp.len = 135;
    sp.spend_coinbase_of = 21;
    sp.spend_at = h_a + 1;                              // 121: 100 blocks, 59460 ticks
    CHECK(run("fb_maturity_121", sp) == error::coinbase_maturity);
}

TEST_CASE("faster blocks - the last immature block is sixty ticks short of 60000",
          "[batch][faster_blocks]") {
    spec sp;
    sp.len = 135;
    sp.spend_coinbase_of = 21;
    sp.spend_at = h_a + 9;                              // 129: 72540 - 12600 = 59940
    CHECK(run("fb_maturity_129", sp) == error::coinbase_maturity);
}

TEST_CASE("faster blocks - the coinbase matures at exactly 60000 ticks",
          "[batch][faster_blocks]") {
    spec sp;
    sp.len = 135;
    sp.spend_coinbase_of = 21;
    sp.spend_at = h_a + 10;                             // 130: 72600 - 12600 = 60000
    CHECK(run("fb_maturity_130", sp) == error::success);
}

TEST_CASE("batch uses exact tick finality including the strict release boundary", "[batch][faster_blocks][locks]") {
    spec sp;
    sp.len = 132;
    sp.spend_coinbase_of = 1;
    sp.unlocked_spend = true;
    sp.spend_locktime = 121;
    sp.spend_sequence = 0xfffffffe;
    sp.spend_at = 130; // T=72600 ==600*121, non-final.
    CHECK(run("fb_lock_equal", sp) == error::block_non_final);
    sp.spend_at = 131; // T=72660 >600*121, final without quotient rounding.
    CHECK(run("fb_lock_after", sp) == error::success);
}

TEST_CASE("batch checks relative locks in ticks on matured inputs", "[batch][faster_blocks][locks]") {
    spec sp;
    sp.len = 132;
    sp.spend_coinbase_of = 1;
    sp.unlocked_spend = true;
    sp.spend_version = 2;
    sp.spend_sequence = 120;
    sp.spend_at = 129; // T(129)-T(1)=71940 <72000.
    CHECK(run("fb_sequence_before", sp) == error::sequence_locked);
    sp.spend_at = 130; // T(130)-T(1)=72000, inclusive release.
    CHECK(run("fb_sequence_exact", sp) == error::success);
}

TEST_CASE("batch enforces the scaled total block size independently of transaction caps", "[batch][faster_blocks][block_size]") {
    spec sp;
    sp.padding_transactions = 7; // Seven individually valid ~500KB transactions.
    CHECK(run("fb_size_over", sp) == error::block_size_limit);
    sp.padding_transactions = 6;
    CHECK(run("fb_size_under", sp) == error::success);
    sp.padding_transactions = 7;
    sp.padding_height = h_a;
    CHECK(run("fb_size_before", sp) == error::success);
}

TEST_CASE("batch retains BIP68 time locks in 512-second MTP units", "[batch][faster_blocks][locks]") {
    spec sp;
    sp.len = 132;
    sp.spend_coinbase_of = 20;
    sp.unlocked_spend = true;
    sp.spend_version = 2;
    sp.spend_sequence = relative_locktime_time_locked | 128; // 65536 seconds.
    sp.spend_at = 129; // Parent MTP123*600 minus funding-parent MTP14*600 =65400.
    CHECK(run("fb_sequence_time_before", sp) == error::sequence_locked);
    sp.spend_at = 130; // 66000 seconds; the coinbase is already tick-mature in both cases.
    CHECK(run("fb_sequence_time_exact", sp) == error::success);
}

TEST_CASE("batch enforces a two-megabyte configuration's exact two-hundred-kilobyte short-block limit",
          "[batch][faster_blocks][small_floor]") {
    spec sp;
    sp.consensus_network = domain::config::network::chipnet;
    sp.padding_transactions = 1;
    sp.padded_block_size = 200'000;
    CHECK(run("fb_2mb_exact", sp) == error::success);
    sp.padded_block_size = 200'001;
    CHECK(run("fb_2mb_above", sp) == error::block_size_limit);
    sp.padding_height = h_a; // The same body still fits the last 600-tick block.
    CHECK(run("fb_2mb_before", sp) == error::success);
}

TEST_CASE("coinbase height-minus-one locktime breaks at the second short block unless sequence is final",
          "[batch][faster_blocks][coinbase_locktime]") {
    spec sp;
    sp.len = h_a + 1;
    sp.coinbase_value_override_at = sp.len;
    sp.coinbase_value_override = 5 * coin;
    sp.coinbase_locktime = sp.len - 1;
    sp.coinbase_sequence = 0xfffffffe;
    CHECK(run("fb_cb_lt_first", sp) == error::success);
    sp.len = h_a + 2;
    sp.coinbase_value_override_at = sp.len;
    sp.coinbase_locktime = sp.len - 1;
    CHECK(run("fb_cb_lt_second", sp) == error::block_non_final);
    sp.coinbase_sequence = 0xffffffff;
    CHECK(run("fb_cb_lt_escape", sp) == error::success);
}

#endif // KTH_CURRENCY_BCH
