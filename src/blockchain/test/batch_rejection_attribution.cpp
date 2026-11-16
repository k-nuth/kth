// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(KTH_CURRENCY_BCH)
#include <test_helpers.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/infrastructure/math/hash.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::blockchain;
using namespace kth::test;

// =============================================================================
// Which block a batch refusal is about
// =============================================================================
//
// validate_block_batch judges a whole batch and answers with one code. A caller
// that has to ACT on a refusal — drop the one block that failed, keep what came
// before it, tell whoever supplied that block why — needs to know which block it
// was, and the code alone does not say: coinbase_value_limit from a batch of a
// hundred blocks names none of them.
//
// Every case here builds a real regtest chain (real proof of work, real merkle
// roots, real signatures), puts its headers in the index the way the node does,
// and runs the real validator over the bodies. Each defect is planted at a
// chosen height, then moved, and the report has to follow it.
//
// Attribution controls exercise the extended reporting contract. Behavioral
// admission regressions belong to the node pipeline tests; an unavailable
// reporting overload on the base is not a behavioral RED result.

namespace {

using domain::chain::block;
using domain::chain::input;
using domain::chain::output;
using domain::chain::output_point;
using domain::chain::script;
using domain::chain::transaction;

constexpr uint32_t base_time = 1'700'000'000;

// A spend's coinbase has to be a hundred blocks old.
constexpr uint32_t maturity = 100;

// Where each defect goes, by height. Empty means a valid chain.
struct chain_spec {
    uint32_t len = 6;
    // The coinbase at this height encodes the wrong BIP34 height: a structural
    // refusal from the serial phase, before any prevout is resolved.
    std::optional<uint32_t> wrong_height_at;
    // The coinbase at this height claims one satoshi more than it may.
    std::optional<uint32_t> overclaim_at;
    // A transaction at this height spends an outpoint nothing ever created.
    std::optional<uint32_t> missing_prevout_at;
    // A transaction at this height spends the coinbase matured for it with a
    // correct signature.
    std::vector<uint32_t> good_spend_at;
    // A transaction at this height spends the coinbase matured for it with an
    // EMPTY unlocking script: a script refusal from the parallel phase.
    std::vector<uint32_t> bad_spend_at;
};

bool contains(std::vector<uint32_t> const& heights, uint32_t h) {
    for (auto const x : heights) {
        if (x == h) {
            return true;
        }
    }
    return false;
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

// Spend `source`'s first output back to the miner, paying no fee, with no
// unlocking script at all. Structurally a transaction; never a valid one.
transaction unsigned_spend(transaction const& source) {
    input::list inputs;
    inputs.emplace_back(output_point{source.hash(), 0}, script{}, 0xffffffff);
    output::list outputs;
    outputs.emplace_back(source.outputs().front().value(), miner_script(), domain::chain::token_data_opt{});
    return transaction(1, 0, std::move(inputs), std::move(outputs));
}

std::vector<block_const_ptr> build_chain(chain_spec const& sp, domain::script_flags_t flags) {
    std::vector<block_const_ptr> blocks;
    auto prev = block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= sp.len; ++h) {
        transaction::list txs;
        uint64_t fees = 0;

        // Spends go after the coinbase; with one non-coinbase transaction per
        // block there is no ordering rule to satisfy.
        if (contains(sp.good_spend_at, h) || contains(sp.bad_spend_at, h)) {
            REQUIRE(h > maturity);
            auto const& matured = blocks[h - maturity - 2]->transactions().front();
            if (contains(sp.good_spend_at, h)) {
                txs.push_back(spend_p2pkh(matured, 0, matured.outputs().front().value(), flags));
            } else {
                txs.push_back(unsigned_spend(matured));
            }
        }
        if (sp.missing_prevout_at && *sp.missing_prevout_at == h) {
            hash_digest nowhere{};
            nowhere.fill(0xab);
            input::list inputs;
            inputs.emplace_back(output_point{nowhere, 0}, script{}, 0xffffffff);
            output::list outputs;
            outputs.emplace_back(1000, miner_script(), domain::chain::token_data_opt{});
            txs.emplace_back(1, 0, std::move(inputs), std::move(outputs));
        }

        auto const encoded_height = (sp.wrong_height_at && *sp.wrong_height_at == h) ? h + 1 : h;
        auto value = block::subsidy(h, /*retarget*/ false) + fees;
        if (sp.overclaim_at && *sp.overclaim_at == h) {
            value += 1;
        }
        txs.insert(txs.begin(), coinbase_tx(encoded_height, 0, value));

        auto mined = mine(prev, base_time + h * 600, std::move(txs));
        prev = mined.hash();
        blocks.push_back(std::make_shared<domain::message::block const>(std::move(mined)));
    }
    return blocks;
}

struct verdict {
    code result;
    batch_rejection rejection;
};

// Headers into the index first, as they are on a node, then the real batch,
// through the reporting form of the validator.
verdict run(char const* name, chain_spec const& sp) {
    chain_fixture fixture(name);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings s(domain::config::network::regtest);
    s.retarget = false;
    auto const blocks = build_chain(sp, s.enabled_flags());

    domain::message::header::list headers;
    for (auto const& blk : blocks) headers.push_back(blk->header());
    auto const added = fixture.organizer().add_headers(headers);
    REQUIRE(added.headers_added == blocks.size());

    verdict out{error::success, {}};
    out.result = validate_block_batch(fixture.chain(), s, domain::config::network::regtest,
        blocks, 1, out.rejection);
    return out;
}

// The same batch through the old form, which has no report.
code run_unattributed(char const* name, chain_spec const& sp) {
    chain_fixture fixture(name);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings s(domain::config::network::regtest);
    s.retarget = false;
    auto const blocks = build_chain(sp, s.enabled_flags());

    domain::message::header::list headers;
    for (auto const& blk : blocks) headers.push_back(blk->header());
    REQUIRE(fixture.organizer().add_headers(headers).headers_added == blocks.size());

    return validate_block_batch(fixture.chain(), s, domain::config::network::regtest, blocks, 1);
}

// The same batch, given each block's identifiers; `txids` may alter them.
template <typename Alter>
verdict run_with_txids(char const* name, chain_spec const& sp, Alter&& alter) {
    chain_fixture fixture(name);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings s(domain::config::network::regtest);
    s.retarget = false;
    auto const blocks = build_chain(sp, s.enabled_flags());

    domain::message::header::list headers;
    for (auto const& blk : blocks) headers.push_back(blk->header());
    REQUIRE(fixture.organizer().add_headers(headers).headers_added == blocks.size());

    std::vector<hash_list> txids;
    for (auto const& blk : blocks) txids.push_back(blk->to_hashes());
    alter(txids);

    verdict out{error::success, {}};
    out.result = validate_block_batch(fixture.chain(), s, domain::config::network::regtest,
        blocks, txids, 1, out.rejection);
    return out;
}

} // namespace

// -----------------------------------------------------------------------------
// The baseline: a code names no block
// -----------------------------------------------------------------------------

TEST_CASE("batch rejection: the code alone cannot say which block was refused",
          "[batch_rejection][baseline]") {
    // The same defect at two heights, judged by the form the pipeline calls
    // today. Both refusals are correct and both are the same code; a caller
    // holding only that code cannot tell the two batches apart, and so cannot
    // say which block to drop. Passes on the unmodified base: it states the gap
    // the attribution cases below close.
    chain_spec at_three;
    at_three.wrong_height_at = 3;
    chain_spec at_five;
    at_five.wrong_height_at = 5;

    auto const three = run_unattributed("baseline_three", at_three);
    auto const five = run_unattributed("baseline_five", at_five);
    REQUIRE(three == error::coinbase_height_mismatch);
    REQUIRE(five == error::coinbase_height_mismatch);
    CHECK(three == five);
}

// -----------------------------------------------------------------------------
// Attribution follows the defect
// -----------------------------------------------------------------------------

TEST_CASE("batch rejection: a valid batch reports no block and the old form agrees",
          "[batch_rejection]") {
    chain_spec valid;
    auto const v = run("attr_valid", valid);
    CHECK(v.result == error::success);
    CHECK_FALSE(v.rejection.block.has_value());
    CHECK(run_unattributed("attr_valid_old", valid) == error::success);
}

TEST_CASE("batch rejection: a structural refusal names the block that failed",
          "[batch_rejection][serial]") {
    // The serial phase walks the batch in height order and stops at the first
    // block that fails, so the report is that block's index — and moves with it.
    chain_spec at_three;
    at_three.wrong_height_at = 3;
    auto const three = run("attr_structural_three", at_three);
    REQUIRE(three.result == error::coinbase_height_mismatch);
    REQUIRE(three.rejection.block.has_value());
    CHECK(*three.rejection.block == 2u);     // index 2 is height 3

    chain_spec at_five;
    at_five.wrong_height_at = 5;
    auto const five = run("attr_structural_five", at_five);
    REQUIRE(five.result == error::coinbase_height_mismatch);
    REQUIRE(five.rejection.block.has_value());
    CHECK(*five.rejection.block == 4u);
}

TEST_CASE("batch rejection: a coinbase that claims too much is attributed to its block",
          "[batch_rejection][serial]") {
    // Decided after prevout resolution, in the second serial pass. Still one
    // block's fault, still that block.
    chain_spec sp;
    sp.overclaim_at = 4;
    auto const v = run("attr_overclaim", sp);
    REQUIRE(v.result == error::coinbase_value_limit);
    REQUIRE(v.rejection.block.has_value());
    CHECK(*v.rejection.block == 3u);
}

TEST_CASE("batch rejection: a prevout nothing created is attributed to the block that spends it",
          "[batch_rejection][resolution]") {
    // The store answers with a list of absent keys in no order a caller may
    // rely on; the report is the lowest block that spends one of them.
    chain_spec sp;
    sp.missing_prevout_at = 2;
    auto const v = run("attr_missing_prevout", sp);
    REQUIRE(v.result == error::missing_previous_output);
    REQUIRE(v.rejection.block.has_value());
    CHECK(*v.rejection.block == 1u);
}

TEST_CASE("batch rejection: a script failure names the lowest failing block, whatever the thread order",
          "[batch_rejection][scripts]") {
    // Scripts are verified in parallel across the whole batch. Whichever worker
    // happens to fail first must not decide the report: the block named has to
    // be the lowest one with a failing transaction, every time.
    //
    // A valid signed spend at 102 shows that lower blocks are judged and pass;
    // two unsigned spends at 103 and 105 show that the lower of two failures is
    // the one named, and that the code is the same as when it fails alone.
    chain_spec alone;
    alone.len = 106;
    alone.good_spend_at = {102};
    alone.bad_spend_at = {105};
    auto const single = run("attr_script_alone", alone);
    REQUIRE(single.result != error::success);
    REQUIRE(single.rejection.block.has_value());
    CHECK(*single.rejection.block == 104u);

    chain_spec two;
    two.len = 106;
    two.good_spend_at = {102};
    two.bad_spend_at = {103, 105};
    for (int attempt = 0; attempt < 3; ++attempt) {
        CAPTURE(attempt);
        auto const v = run("attr_script_two", two);
        REQUIRE(v.result == single.result);
        REQUIRE(v.rejection.block.has_value());
        CHECK(*v.rejection.block == 102u);     // index 102 is height 103
    }
}

// -----------------------------------------------------------------------------
// The SigChecks limiter, pure
// -----------------------------------------------------------------------------

TEST_CASE("batch rejection: the sigchecks limiter names the block over its limit",
          "[batch_rejection][sigchecks]") {
    std::vector<uint64_t> const block_limits{10, 10, 10};
    std::vector<uint64_t> const tx_limits{5, 5, 5};

    // Block 1 goes over its block limit on its second transaction.
    std::vector<sigcheck_entry> const over_block{
        {0, 0, 4}, {0, 1, 4},
        {1, 2, 5}, {1, 3, 5}, {1, 4, 1},
        {2, 5, 1}};
    size_t named = 99;
    CHECK(enforce_sigcheck_limits(over_block, block_limits, tx_limits, named)
          == error::block_sigchecks_limit);
    CHECK(named == 1u);

    // Block 2 has a transaction over the per-transaction limit.
    std::vector<sigcheck_entry> const over_tx{
        {0, 0, 4},
        {1, 1, 4},
        {2, 2, 3}, {2, 2, 3}};
    named = 99;
    CHECK(enforce_sigcheck_limits(over_tx, block_limits, tx_limits, named)
          == error::transaction_sigchecks_limit);
    CHECK(named == 2u);

    // Within every limit: nothing named, and the out-parameter is left alone.
    std::vector<sigcheck_entry> const fine{{0, 0, 5}, {1, 1, 5}, {2, 2, 5}};
    named = 99;
    CHECK(enforce_sigcheck_limits(fine, block_limits, tx_limits, named) == error::success);
    CHECK(named == 99u);

    // The old forms still answer the same codes.
    CHECK(enforce_sigcheck_limits(over_block, block_limits, tx_limits) == error::block_sigchecks_limit);
    CHECK(enforce_sigcheck_limits(over_tx, block_limits, uint64_t{5}) == error::transaction_sigchecks_limit);
}


TEST_CASE("batch rejection: given identifiers, the verdict is the one computed ones give",
          "[batch_rejection]") {
    auto const keep = [](std::vector<hash_list>&) {};
    chain_spec valid;
    auto const v = run_with_txids("attr_txids_valid", valid, keep);
    CHECK(v.result == error::success);
    CHECK_FALSE(v.rejection.block.has_value());

    chain_spec overclaim;
    overclaim.overclaim_at = 4;
    auto const o = run_with_txids("attr_txids_overclaim", overclaim, keep);
    REQUIRE(o.result == error::coinbase_value_limit);
    REQUIRE(o.rejection.block.has_value());
    CHECK(*o.rejection.block == 3u);
}

TEST_CASE("batch rejection: identifiers that are not the block's are judged as given",
          "[batch_rejection][serial]") {
    // The merkle root is checked against the identifiers passed in: an altered
    // one is a mismatch of the block it belongs to, not a recomputation.
    chain_spec valid;
    auto const v = run_with_txids("attr_txids_altered", valid, [](std::vector<hash_list>& t) {
        t[2][0][0] ^= 0x01;
    });
    REQUIRE(v.result == error::merkle_mismatch);
    REQUIRE(v.rejection.block.has_value());
    CHECK(*v.rejection.block == 2u);
}

TEST_CASE("batch rejection: identifiers that do not match the batch are refused unattributed",
          "[batch_rejection]") {
    chain_spec valid;
    auto const fewer_blocks = run_with_txids("attr_txids_short", valid, [](std::vector<hash_list>& t) {
        t.pop_back();
    });
    CHECK(fewer_blocks.result == error::operation_failed);
    CHECK_FALSE(fewer_blocks.rejection.block.has_value());

    auto const extra_tx = run_with_txids("attr_txids_extra", valid, [](std::vector<hash_list>& t) {
        t[1].push_back(null_hash);
    });
    CHECK(extra_tx.result == error::operation_failed);
    CHECK_FALSE(extra_tx.rejection.block.has_value());
}

TEST_CASE("batch rejection: body defects in several blocks name the lowest, whatever the thread order",
          "[batch_rejection]") {
    // The bodies are checked in parallel. Two blocks with a body defect: the
    // refusal names the lower, as a check in height order does.
    chain_spec valid;
    valid.len = 12;
    auto const both = run_with_txids("attr_body_two", valid, [](std::vector<hash_list>& t) {
        t[9][0][0] ^= 0x01;
        t[4][0][0] ^= 0x01;
    });
    REQUIRE(both.result == error::merkle_mismatch);
    REQUIRE(both.rejection.block.has_value());
    CHECK(*both.rejection.block == 4u);
}

TEST_CASE("batch rejection: a body defect is refused before a lower block's structural one",
          "[batch_rejection]") {
    // Every body is checked before any block's height-dependent rules, so a
    // body defect above a wrong coinbase height is the one reported: computing
    // the bodies in parallel keeps that order.
    chain_spec spec;
    spec.len = 8;
    spec.wrong_height_at = 2;
    auto const v = run_with_txids("attr_body_before_structure", spec, [](std::vector<hash_list>& t) {
        t[6][0][0] ^= 0x01;
    });
    REQUIRE(v.result == error::merkle_mismatch);
    REQUIRE(v.rejection.block.has_value());
    CHECK(*v.rejection.block == 6u);

    auto const keep = [](std::vector<hash_list>&) {};
    auto const alone = run_with_txids("attr_structure_alone", spec, keep);
    REQUIRE(alone.result == error::coinbase_height_mismatch);
    REQUIRE(alone.rejection.block.has_value());
    CHECK(*alone.rejection.block == 1u);
}

#endif // KTH_CURRENCY_BCH
