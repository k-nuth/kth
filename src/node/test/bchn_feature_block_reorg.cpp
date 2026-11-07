// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of the fork and reorganization sequence b1 .. b8 of Bitcoin Cash Node test/functional/feature_block.py
// (commit 07576013c91ff4a3a74acd85f189c69121cdad1b), driven through the real node stack: header organizer,
// reorg switch, flat-file store, undo records and UTXO-Z (the harness of reorg_cycle.cpp).
// Original: Copyright (c) 2015-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.
//
//     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6 (3)
//                                         \-> b7 (2) -> b8 (4)
//                      \-> b3 (1) -> b4 (2)
//
// (the number in parentheses is the spendable coinbase each block spends).
// Adaptation: BCHN builds 99 blocks plus 33 spendable coinbases on top of regtest; here the trunk is 105 blocks and
// the spent outputs are the coinbases of heights 1..6, which are mature by the height of b1. Fork choice is by
// accumulated work with equal-work ties kept on the first seen tip, as in the original.

#include <algorithm>

#include "bchn_reorg_scenario.hpp"

using namespace kth;
using namespace kth::test;
using kth::test::bchn_reorg::scenario;
using kth::test::bchn_reorg::trunk_len;
using kth::test::bchn_reorg::signed_spend_to;

// genesis -> b1 -> b2, then the equal-work b3 does not displace b2, the longer b3 -> b4 does, and b5 -> b6 takes
// the chain back.
TEST_CASE("BCHN feature_block - b1 .. b6 forks and reorganizations", "[bchn_non_vm][bchn_reorg]") {
    scenario s("bchn_fb_b1_b6");
    auto const prev = s.trunk.back().hash();

    auto const b1 = s.next(prev, 106, 1, 1);
    auto const b2 = s.next(b1.hash(), 107, 2, 2);
    REQUIRE(s.headers({b1, b2}).headers_added == 2);
    persist_headers(s.fixture, {b1, b2}, 106);
    connect_bodies(s.fixture, {b1, b2}, 106);
    CHECK(s.connected_height() == 107);
    CHECK(s.active_hash(107) == b2.hash());
    auto const cb2_out = s.trunk[1].transactions().front().hash();   // the output b2 (and b3) spend
    CHECK_FALSE(s.unspent(cb2_out));

    // "Don't reorg to a chain of the same length": b3 spends the same output as b2 and has the same work.
    auto const b3 = s.next(b1.hash(), 107, 3, 2);
    auto const r3 = s.headers({b3});
    CHECK(s.stored(b3));
    CHECK_FALSE(r3.reorg_candidate);
    CHECK(s.active_hash(107) == b2.hash());
    CHECK(s.connected_height() == 107);

    // "Reorg to a longer chain": b4 on b3.
    auto const b4 = s.next(b3.hash(), 108, 4, 3);
    auto const r4 = s.headers({b4});
    REQUIRE(r4.reorg_candidate);
    CHECK(r4.reorg_fork_height == 106);
    auto const sw4 = s.switch_to(r4);
    REQUIRE(sw4.ok);
    CHECK(s.connected_height() == 106);
    CHECK(s.unspent(cb2_out));                                   // b2's spend was undone
    connect_bodies(s.fixture, {b3, b4}, 107);
    CHECK(s.connected_height() == 108);
    CHECK(s.active_hash(107) == b3.hash());
    CHECK(s.active_hash(108) == b4.hash());
    CHECK_FALSE(s.unspent(cb2_out));                              // spent again, now by b3
    CHECK(s.unspent(b3.transactions()[1].hash()));
    CHECK_FALSE(s.unspent(b2.transactions()[1].hash()));          // b2's payment is not in this chain

    // "... and back to the first chain": b5 on b2 ties b4, b6 outweighs it.
    auto const b5 = s.next(b2.hash(), 108, 5, 3);
    auto const r5 = s.headers({b5});
    CHECK(s.stored(b5));
    CHECK_FALSE(r5.reorg_candidate);
    CHECK(s.active_hash(108) == b4.hash());

    auto const b6 = s.next(b5.hash(), 109, 6, 4);
    auto const r6 = s.headers({b6});
    REQUIRE(r6.reorg_candidate);
    CHECK(r6.reorg_fork_height == 106);
    auto const sw6 = s.switch_to(r6);
    REQUIRE(sw6.ok);
    CHECK(s.connected_height() == 106);
    connect_bodies(s.fixture, {b2, b5, b6}, 107);
    CHECK(s.connected_height() == 109);
    CHECK(s.active_hash(107) == b2.hash());
    CHECK(s.active_hash(108) == b5.hash());
    CHECK(s.active_hash(109) == b6.hash());
    CHECK(s.unspent(b2.transactions()[1].hash()));
    CHECK(s.unspent(b6.transactions()[1].hash()));
    CHECK_FALSE(s.unspent(b3.transactions()[1].hash()));
    CHECK_FALSE(s.unspent(b4.transactions()[1].hash()));
    // the abandoned branch stays stored
    auto& index = s.fixture.chain().headers();
    CHECK(index.has_block_data(index.find(b4.hash())));

    // b17: a block that spends a transaction of an abandoned fork is refused. Control first: a block on b6 that spends b6's own transaction
    // is connected; then the block that spends b3's transaction (b3 was connected, then disconnected by the return to b6) is not.
    {
        using domain::chain::output;
        auto const flags = s.fixture.chain().chain_settings().enabled_flags();
        auto const spend_of = [&](domain::chain::transaction const& parent) {
            output::list outs;
            outs.emplace_back(parent.outputs()[0].value() - 1000, miner_script(), domain::chain::token_data_opt{});
            return signed_spend_to(parent, 0, std::move(outs), flags);
        };
        auto const control = mine_block(b6.hash(), 110, s.base_time + 110 * block_spacing, 17, {spend_of(b6.transactions()[1])}, 1000);
        REQUIRE(s.headers({control}).headers_added == 1);
        persist_headers(s.fixture, {control}, 110);
        connect_bodies(s.fixture, {control}, 110);
        CHECK(s.connected_height() == 110);

        auto const b17 = mine_block(control.hash(), 111, s.base_time + 111 * block_spacing, 18, {spend_of(b3.transactions()[1])}, 1000);
        REQUIRE(s.headers({b17}).headers_added == 1);
        persist_headers(s.fixture, {b17}, 111);
        run_connect_tasks(s.fixture, {b17}, 111, [](std::string const&) {});
        CHECK(s.connected_height() == 110);
        CHECK_FALSE(s.unspent(b17.transactions()[1].hash()));
    }
}

// Control for b7 / b8: the same shape with outputs that are free, so the longer sibling branch is valid and adopted.
TEST_CASE("BCHN feature_block - a valid longer sibling branch is adopted (control of b7 / b8)", "[bchn_non_vm][bchn_reorg]") {
    scenario s("bchn_fb_b7_b8_control");
    auto const prev = s.trunk.back().hash();
    auto const b1 = s.next(prev, 106, 1, 1);
    auto const b2 = s.next(b1.hash(), 107, 2, 2);
    auto const b5 = s.next(b2.hash(), 108, 5, 3);
    auto const b6 = s.next(b5.hash(), 109, 6, 4);
    REQUIRE(s.headers({b1, b2, b5, b6}).headers_added == 4);
    persist_headers(s.fixture, {b1, b2, b5, b6}, 106);
    connect_bodies(s.fixture, {b1, b2, b5, b6}, 106);
    REQUIRE(s.connected_height() == 109);

    auto const b7 = s.next(b5.hash(), 109, 7, 5);                    // spends a coinbase nobody spent
    auto const r7 = s.headers({b7});
    CHECK(s.stored(b7));
    CHECK_FALSE(r7.reorg_candidate);
    auto const b8 = s.next(b7.hash(), 110, 8, 6);
    auto const r8 = s.headers({b8});
    REQUIRE(r8.reorg_candidate);
    CHECK(r8.reorg_fork_height == 108);
    REQUIRE(s.switch_to(r8).ok);
    connect_bodies(s.fixture, {b7, b8}, 109);
    CHECK(s.connected_height() == 110);
    CHECK(s.active_hash(109) == b7.hash());
    CHECK(s.active_hash(110) == b8.hash());
}

// b84 .. b89a: reorganizing onto and off blocks whose transactions create OP_RETURN outputs (which are never in the UTXO set)
// and spend outputs of the same block, then a spend of an OP_RETURN output.
//     X -> b84 (1) -> b87 (4) -> b88 (5)
//      \-> b85 (2) -> b86 (3)
TEST_CASE("BCHN feature_block - b84 .. b89a reorganizations over OP_RETURN outputs", "[bchn_non_vm][bchn_reorg]") {
    using domain::chain::output;
    using domain::chain::script;
    using domain::chain::transaction;
    using domain::machine::opcode;
    using domain::machine::operation;

    scenario s("bchn_fb_b84_b89a");
    auto const flags = s.fixture.chain().chain_settings().enabled_flags();
    auto const op_return = [] { return output(0, script(operation::list{operation(opcode::return_)}), domain::chain::token_data_opt{}); };
    auto const op_true = [] { return output(0, script(operation::list{operation(opcode::push_positive_1)}), domain::chain::token_data_opt{}); };
    auto const free_spend = [](transaction const& parent, uint32_t index, output::list outs) {
        domain::chain::input::list ins;
        ins.emplace_back(domain::chain::output_point{parent.hash(), index}, script{}, 0xffffffff);
        return transaction(1, 0, std::move(ins), std::move(outs));
    };
    auto const ctor_sorted = [](std::vector<transaction> txs) {
        std::sort(txs.begin(), txs.end(), [](transaction const& a, transaction const& b) {
            auto const ha = a.hash();
            auto const hb = b.hash();
            return std::lexicographical_compare(ha.rbegin(), ha.rend(), hb.rbegin(), hb.rend());
        });
        return txs;
    };

    // X is the common ancestor
    auto const x = s.next(s.trunk.back().hash(), 106, 1, 6);
    REQUIRE(s.headers({x}).headers_added == 1);
    persist_headers(s.fixture, {x}, 106);
    connect_bodies(s.fixture, {x}, 106);

    // b84: tx1 spends coinbase 1 into [OP_RETURN, 4 x OP_TRUE]; tx2..tx5 spend those, creating more OP_RETURN outputs
    auto const& cb1 = s.trunk[0].transactions().front();
    output::list tx1_outs;
    tx1_outs.push_back(op_return());
    for (int i = 0; i < 4; ++i) tx1_outs.push_back(op_true());
    auto const tx1 = signed_spend_to(cb1, 0, tx1_outs, flags);
    auto const tx2 = free_spend(tx1, 1, {op_return(), op_return()});
    auto const tx3 = free_spend(tx1, 2, {op_return(), op_true()});
    auto const tx4 = free_spend(tx1, 3, {op_true(), op_return()});
    auto const tx5 = free_spend(tx1, 4, {op_return()});
    auto const fee1 = cb1.outputs()[0].value();
    auto const b84 = mine_block(x.hash(), 107, s.base_time + 107 * block_spacing, 84,
        ctor_sorted({tx1, tx2, tx3, tx4, tx5}), fee1);

    auto const b85 = s.next(x.hash(), 107, 85, 2);
    auto const b86 = s.next(b85.hash(), 108, 86, 3);
    auto const b87 = s.next(b84.hash(), 108, 87, 4);
    auto const b88 = s.next(b87.hash(), 109, 88, 5);

    REQUIRE(s.headers({b84}).headers_added == 1);
    persist_headers(s.fixture, {b84}, 107);
    connect_bodies(s.fixture, {b84}, 107);
    CHECK(s.connected_height() == 107);
    CHECK_FALSE(s.unspent(cb1.hash()));                       // coinbase 1 is spent by tx1
    CHECK_FALSE(s.unspent(tx1.hash(), 0));                              // OP_RETURN output: never in the set
    CHECK_FALSE(s.unspent(tx1.hash(), 1));                              // spent in the block
    CHECK(s.unspent(tx3.hash(), 1));                                    // OP_TRUE output left by tx3
    CHECK_FALSE(s.unspent(tx3.hash(), 0));
    CHECK(s.unspent(tx4.hash(), 0));
    CHECK_FALSE(s.unspent(tx4.hash(), 1));
    CHECK_FALSE(s.unspent(tx5.hash(), 0));

    // b85: same length, no switch. b86: longer, switch off b84
    auto const r85 = s.headers({b85});
    CHECK_FALSE(r85.reorg_candidate);
    auto const r86 = s.headers({b86});
    REQUIRE(r86.reorg_candidate);
    REQUIRE(s.switch_to(r86).ok);
    CHECK(s.connected_height() == 106);
    CHECK(s.unspent(cb1.hash()));                                  // the undo of b84 gave coinbase 1 back
    CHECK_FALSE(s.unspent(tx3.hash(), 1));
    CHECK_FALSE(s.unspent(tx4.hash(), 0));
    connect_bodies(s.fixture, {b85, b86}, 107);
    CHECK(s.connected_height() == 108);
    CHECK(s.active_hash(108) == b86.hash());

    // b87: same length as b86, no switch. b88: longer, switch back onto b84
    auto const r87 = s.headers({b87});
    CHECK_FALSE(r87.reorg_candidate);
    auto const r88 = s.headers({b88});
    REQUIRE(r88.reorg_candidate);
    REQUIRE(s.switch_to(r88).ok);
    connect_bodies(s.fixture, {b84, b87, b88}, 107);
    CHECK(s.connected_height() == 109);
    CHECK(s.active_hash(107) == b84.hash());
    CHECK(s.unspent(tx3.hash(), 1));
    CHECK(s.unspent(tx4.hash(), 0));
    CHECK_FALSE(s.unspent(tx1.hash(), 0));
    CHECK_FALSE(s.unspent(cb1.hash()));

    // b89a: a spend of the OP_RETURN output tx1:0 is refused; the tip stays on b88
    auto const spend_return = free_spend(tx1, 0, {op_true()});
    auto const b89a = mine_block(b88.hash(), 110, s.base_time + 110 * block_spacing, 89, {spend_return}, 0);
    REQUIRE(s.headers({b89a}).headers_added == 1);
    persist_headers(s.fixture, {b89a}, 110);
    run_connect_tasks(s.fixture, {b89a}, 110, [](std::string const&) {});
    CHECK(s.connected_height() == 109);
    CHECK_FALSE(s.unspent(spend_return.hash(), 0));
}

// "Test a re-org of one week's worth of blocks (1088 blocks)". VARIANT: BCHN runs this with -noparkdeepreorg and 1 MB
// blocks, and reorganizes onto an alternative chain one block longer. Knuth always parks a branch that rewinds more than
// one validated block until it has accumulated twice the work the active chain gained since the fork, so the alternative
// chain here has 2 * 1088 + 1 blocks, and the blocks are coinbase-only. The reorganization back onto the first chain of the
// original is ported with the same rule: the first chain, extended to 4355 blocks (more than twice the 2177 the active chain gained), wins
// back; 4354 are not enough. It runs in a few seconds.
TEST_CASE("BCHN feature_block - a re-org of 1088 blocks", "[bchn_non_vm][bchn_reorg][bchn_large_reorg]") {
    scenario s("bchn_fb_large_reorg");
    constexpr uint32_t depth = 1088;
    auto const make_chain = [&](uint32_t salt, uint32_t count) {
        std::vector<domain::chain::block> chain;
        auto prev = s.trunk.back().hash();
        for (uint32_t i = 0; i < count; ++i) {
            auto const h = trunk_len + 1 + i;
            // one second apart: the chain must stay within the clock, which spacing of 2 minutes over 2000 blocks would not
            chain.push_back(mine_block(prev, h, s.base_time + trunk_len * block_spacing + 1 + i, salt, {}, 0));
            prev = chain.back().hash();
        }
        return chain;
    };
    auto const first = make_chain(1, depth);
    REQUIRE(s.headers(first).headers_added == depth);
    persist_headers(s.fixture, first, trunk_len + 1);
    connect_bodies(s.fixture, first, trunk_len + 1);
    REQUIRE(s.connected_height() == trunk_len + depth);

    // the same length: no switch
    auto const alt = make_chain(2, 2 * depth + 1);
    std::vector<domain::chain::block> const same_length(alt.begin(), alt.begin() + depth);
    auto const r_same = s.headers(same_length);
    CHECK_FALSE(r_same.reorg_candidate);

    // the rest, up to twice the work gained since the fork plus one block
    std::vector<domain::chain::block> const rest(alt.begin() + depth, alt.end());
    auto const r = s.headers(rest);
    REQUIRE(r.reorg_candidate);
    CHECK(r.reorg_fork_height == int32_t(trunk_len));
    REQUIRE(s.switch_to(r).ok);
    CHECK(s.connected_height() == trunk_len);
    connect_bodies(s.fixture, alt, trunk_len + 1);
    CHECK(s.connected_height() == trunk_len + 2 * depth + 1);
    CHECK(s.active_hash(trunk_len + 2 * depth + 1) == alt.back().hash());

    // ... and back onto the first chain: 4354 blocks are not more than twice the work the active chain gained, 4355 are
    auto const back = make_chain(1, 2 * (2 * depth + 1) + 1);
    std::vector<domain::chain::block> const back_most(back.begin() + depth, back.end() - 1);
    auto const r_back_most = s.headers(back_most);
    CHECK_FALSE(r_back_most.reorg_candidate);
    auto const r_back = s.headers({back.back()});
    REQUIRE(r_back.reorg_candidate);
    CHECK(r_back.reorg_fork_height == int32_t(trunk_len));
    REQUIRE(s.switch_to(r_back).ok);
    CHECK(s.connected_height() == trunk_len);
    connect_bodies(s.fixture, back, trunk_len + 1);
    CHECK(s.connected_height() == trunk_len + back.size());
    CHECK(s.active_hash(trunk_len + uint32_t(back.size())) == back.back().hash());
}
