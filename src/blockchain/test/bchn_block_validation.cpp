// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Block-level consensus scenarios of Bitcoin Cash Node (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b), driven through the real
// validate_block_batch on a regtest chain: test/functional/feature_block.py,
// test/functional/abc-p2p-fullblocktest.py and src/test/blockcheck_tests.cpp.
// Original: Copyright (c) The Bitcoin Core / Bitcoin developers, MIT license.
// The original name of each scenario is in its TEST_CASE.

#include <test_helpers.hpp>

#include <algorithm>
#include <optional>

#include "bchn_block_builder.hpp"

using namespace kth;
using namespace kth::test;
using kth::domain::chain::block;
using kth::domain::chain::input;
using kth::domain::chain::output;
using kth::domain::chain::output_point;
using kth::domain::chain::script;
using kth::domain::chain::transaction;
using kth::domain::machine::opcode;
using kth::domain::machine::operation;

namespace {

constexpr uint64_t coinbase_maturity = 100;

// 0-value OP_RETURN output carrying `payload` bytes (OP_RETURN OP_PUSHDATA4 <payload>).
// Built from raw bytes: the operation list refuses a push over the 520-byte
// stack element limit, which an unspendable output is not subject to.
output op_return_output(size_t payload) {
    data_chunk raw{0x6a, 0x4e, uint8_t(payload), uint8_t(payload >> 8), uint8_t(payload >> 16), uint8_t(payload >> 24)};
    raw.insert(raw.end(), payload, 0x00);
    byte_reader reader(raw);
    auto locking = script::from_data(reader, false);
    REQUIRE(locking);
    return output(0, std::move(*locking), domain::chain::token_data_opt{});
}

// Spend of coinbase `h`: a fat OP_RETURN output plus the rest back to the miner.
transaction fat_spend(domain::chain::block const& source_block, size_t payload, domain::script_flags_t flags) {
    auto const& cb = source_block.transactions().front();
    auto const value = cb.outputs()[0].value();
    output::list outs;
    outs.push_back(op_return_output(payload));
    outs.emplace_back(value - 1000, miner_script(), domain::chain::token_data_opt{});
    return signed_spend(cb, 0, std::move(outs), flags);
}

// A block at height 134 (the first one that can spend coinbases 1..33): 33 fat
// spends plus a coinbase carrying one more OP_RETURN output as padding, so that
// the block is exactly `target` bytes. The padding sits in the coinbase because
// it has no signature, whose DER length would move the size by a byte or two.
block fat_block(regtest_chain& chain, domain::script_flags_t flags, uint64_t target) {
    constexpr size_t n_fat = 33;
    constexpr size_t fat_payload = 950'000;
    constexpr size_t padding_overhead = 8 + 5 + 6;   // value + 5-byte length + OP_RETURN OP_PUSHDATA4 <len>

    std::vector<transaction> txs;
    for (uint32_t h = 1; h <= n_fat; ++h) {
        txs.push_back(fat_spend(chain.at(h), fat_payload, flags));
    }
    sort_ctor(txs);

    auto const height = chain.height() + 1;
    auto const coinbase_value = block::subsidy(height, false) + n_fat * 1000ull;
    auto const assemble = [&](size_t padding) {
        auto cb = coinbase_tx(height, 0, coinbase_value);
        if (padding != 0) {
            auto outs = cb.outputs();
            outs.push_back(op_return_output(padding));
            cb = transaction(cb.version(), cb.locktime(), domain::chain::input::list(cb.inputs()), std::move(outs));
        }
        transaction::list all;
        all.push_back(std::move(cb));
        for (auto const& tx : txs) all.push_back(tx);
        return all;
    };
    auto const unpadded = block(domain::chain::header{0x20000000, null_hash, null_hash, 0, regtest_bits, 0},
                                assemble(0)).serialized_size();
    REQUIRE(target > unpadded + padding_overhead);
    auto all = assemble(size_t(target - unpadded) - padding_overhead);
    return chain.append_raw(std::move(all));
}

} // namespace

// BCHN blockcheck_tests/blockfail "Oversize block" (and feature_block.py b23 / b24-style
// max-size scenarios): a block of exactly the size limit is valid, one byte more is not.
// BCHN's limit for this regtest height is the ABLA initial limit, 32,000,000 bytes.
TEST_CASE("BCHN blockfail - block of exactly the consensus size limit is accepted",
          "[bchn_non_vm][block][block_size]") {
    chain_fixture fixture("bchn_size_exact");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    blockchain::settings const s(domain::config::network::regtest);

    regtest_chain chain(140);
    chain.extend_to(133);
    auto const big = fat_block(chain, s.enabled_flags(), 32'000'000);
    REQUIRE(big.serialized_size() == 32'000'000u);
    CHECK(validate_chain(fixture, s, chain.blocks()) == error::success);
}

// The next case of BCHN's blockfail -- the same block with one more byte -- is
// rejected by BCHN and ACCEPTED here; see failed_regression
// block_size_limit_not_enforced in test/bchn_non_vm/coverage.json.

// -----------------------------------------------------------------------------
// Scenarios on small chains. Each pair is a control that passes the rule and the
// case that breaks it; the controls make sure the harness really validates.
// -----------------------------------------------------------------------------

namespace {

// Spend of coinbase `h`, back to the miner minus `fee`.
transaction spend_coinbase(regtest_chain const& chain, uint32_t h, uint64_t fee, domain::script_flags_t flags) {
    auto const& cb = chain.coinbase_at(h);
    output::list outs;
    outs.emplace_back(cb.outputs()[0].value() - fee, miner_script(), domain::chain::token_data_opt{});
    return signed_spend(cb, 0, std::move(outs), flags);
}

code validate_fresh(regtest_chain const& chain, blockchain::settings const& s, char const* tag) {
    chain_fixture fixture(tag);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    return validate_chain(fixture, s, chain.blocks());
}

} // namespace

// feature_block.py b9/b10/b11: coinbase value is the subsidy plus the fees, no more.
TEST_CASE("BCHN feature_block - coinbase pays exactly subsidy plus fees is accepted",
          "[bchn_non_vm][block][coinbase_value]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    chain.append({spend_coinbase(chain, 1, 1000, s.enabled_flags())}, 1000);
    CHECK(validate_fresh(chain, s, "bchn_cb_ok") == error::success);
}

TEST_CASE("BCHN feature_block - coinbase paying one satoshi over subsidy plus fees is rejected",
          "[bchn_non_vm][block][coinbase_value]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    chain.append({spend_coinbase(chain, 1, 1000, s.enabled_flags())}, 1001);   // bad-cb-amount
    CHECK(validate_fresh(chain, s, "bchn_cb_over") == error::coinbase_value_limit);
}

// feature_block.py b20: a block spending an immature coinbase.
TEST_CASE("BCHN feature_block - spend of a coinbase 100 blocks deep is accepted",
          "[bchn_non_vm][block][coinbase_maturity]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);                                    // coinbase 2 is 100 deep at height 102
    chain.append({spend_coinbase(chain, 2, 0, s.enabled_flags())});
    CHECK(validate_fresh(chain, s, "bchn_mature_ok") == error::success);
}

TEST_CASE("BCHN feature_block - spend of a coinbase 99 blocks deep is rejected",
          "[bchn_non_vm][block][coinbase_maturity]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(100);                                    // block 101 spends coinbase 2: only 99 deep
    chain.append({spend_coinbase(chain, 2, 0, s.enabled_flags())});   // bad-txns-premature-spend-of-coinbase
    CHECK(validate_fresh(chain, s, "bchn_mature_bad") == error::coinbase_maturity);
}

// feature_block.py b22: spending an output that is already spent in the same block.
TEST_CASE("BCHN feature_block - two spends of the same output in one block are rejected",
          "[bchn_non_vm][block][double_spend]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto first = spend_coinbase(chain, 1, 0, s.enabled_flags());
    auto second = spend_coinbase(chain, 1, 1000, s.enabled_flags());   // same prevout, other txid
    chain.append({std::move(first), std::move(second)}, 1000);
    CHECK(validate_fresh(chain, s, "bchn_dspend_block") == error::double_spend);   // bad-txns-inputs-missingorspent
}

// feature_block.py b17: spending an output that does not exist.
TEST_CASE("BCHN feature_block - spend of an output that does not exist is rejected",
          "[bchn_non_vm][block][missing_input]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    // A transaction that is valid on its own, spending the output of a made-up parent
    auto ghost_parent = spend_coinbase(chain, 1, 0, s.enabled_flags());
    auto orphan_child = signed_spend(ghost_parent, 0,
        output::list{output(ghost_parent.outputs()[0].value() - 1000, miner_script(), domain::chain::token_data_opt{})},
        s.enabled_flags());
    chain.append({std::move(orphan_child)}, 1000);   // ghost_parent is never mined
    CHECK(validate_fresh(chain, s, "bchn_missing_input") == error::missing_previous_output);   // bad-txns-inputs-missingorspent
}

// feature_block.py b16-ish / bad-txns-in-belowout: outputs worth more than the inputs.
TEST_CASE("BCHN feature_block - transaction paying out more than it spends is rejected",
          "[bchn_non_vm][block][overspend]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const& cb = chain.coinbase_at(1);
    output::list outs;
    outs.emplace_back(cb.outputs()[0].value() + 1, miner_script(), domain::chain::token_data_opt{});
    chain.append({signed_spend(cb, 0, std::move(outs), s.enabled_flags())});
    CHECK(validate_fresh(chain, s, "bchn_overspend") == error::spend_exceeds_value);   // bad-txns-in-belowout
}

// bad-txns-inputs-duplicate: one transaction naming the same input twice.
TEST_CASE("BCHN feature_block - transaction with a duplicated input is rejected",
          "[bchn_non_vm][block][duplicate_input]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const one = spend_coinbase(chain, 1, 0, s.enabled_flags());
    auto inputs = one.inputs();
    inputs.push_back(inputs[0]);
    chain.append({domain::chain::transaction(one.version(), one.locktime(), std::move(inputs),
                                              domain::chain::output::list(one.outputs()))});
    CHECK(validate_fresh(chain, s, "bchn_dup_input") == error::double_spend);
}

// A spend of an output created earlier in the same block is valid (CTOR sorts by txid).
TEST_CASE("BCHN feature_block - spend of an output created in the same block is accepted",
          "[bchn_non_vm][block][intra_block_spend]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto parent = spend_coinbase(chain, 1, 1000, s.enabled_flags());
    auto child = signed_spend(parent, 0,
        output::list{output(parent.outputs()[0].value() - 1000, miner_script(), domain::chain::token_data_opt{})},
        s.enabled_flags());
    std::vector<transaction> txs{std::move(parent), std::move(child)};
    sort_ctor(txs);
    chain.append(std::move(txs), 2000);
    REQUIRE(is_ctor_sorted(chain.at(chain.height())));
    CHECK(validate_fresh(chain, s, "bchn_intra_ok") == error::success);
}

// feature_block.py b20: a spend of the coinbase of the very same block.
TEST_CASE("BCHN feature_block - spend of the same block's coinbase is rejected",
          "[bchn_non_vm][block][coinbase_maturity]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const height = chain.height() + 1;
    auto cb = coinbase_tx(height, 0, domain::chain::block::subsidy(height, false));
    output::list outs;
    outs.emplace_back(cb.outputs()[0].value(), miner_script(), domain::chain::token_data_opt{});
    auto spend = signed_spend(cb, 0, std::move(outs), s.enabled_flags());
    domain::chain::transaction::list all;
    all.push_back(std::move(cb));
    all.push_back(std::move(spend));
    chain.append_raw(std::move(all));
    CHECK(validate_fresh(chain, s, "bchn_same_cb") == error::coinbase_maturity);
}

// feature_block.py b?? "Reject a block with duplicate transactions": the same transaction twice.
// BCHN rejects it as bad-txns-duplicate; here the second copy is a spend of an
// already spent output, so the block is rejected for double_spend.
TEST_CASE("BCHN feature_block - the same transaction twice in a block is rejected",
          "[bchn_non_vm][block][duplicate_tx]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const tx = spend_coinbase(chain, 1, 0, s.enabled_flags());
    chain.append({tx, tx}, 0);
    CHECK(validate_fresh(chain, s, "bchn_dup_tx") == error::double_spend);
}

// abc-transaction-ordering.py: with canonical ordering a block may list a child BEFORE its parent,
// whenever the child's txid sorts first. Both orders are built from the same parent by varying the
// child's fee until each comparator outcome appears, and both blocks must be valid.
TEST_CASE("BCHN abc-transaction-ordering - children before and after their parent, in canonical order",
          "[bchn_non_vm][block][ctor]") {
    blockchain::settings const s(domain::config::network::regtest);
    for (bool child_first : {false, true}) {
        DYNAMIC_SECTION((child_first ? "child sorts before its parent" : "child sorts after its parent") << "") {
            regtest_chain chain(110);
            chain.extend_to(101);
            auto parent = spend_coinbase(chain, 1, 1000, s.enabled_flags());
            std::optional<transaction> chosen;
            for (uint64_t extra = 0; extra < 400 && ! chosen; ++extra) {
                auto child = signed_spend(parent, 0,
                    output::list{output(parent.outputs()[0].value() - 1000 - extra, miner_script(), domain::chain::token_data_opt{})},
                    s.enabled_flags());
                if (ctor_less(child, parent) == child_first) chosen = child;
            }
            REQUIRE(chosen);
            std::vector<transaction> txs{parent, *chosen};
            sort_ctor(txs);
            REQUIRE((txs.front().hash() == chosen->hash()) == child_first);
            // the coinbase claims everything the two transactions leave unspent
            auto const fees = chain.coinbase_at(1).outputs()[0].value() - chosen->outputs()[0].value();
            chain.append(std::move(txs), fees);
            REQUIRE(is_ctor_sorted(chain.at(chain.height())));
            CHECK(validate_fresh(chain, s, "bchn_ctor_family") == error::success);
        }
    }
}
