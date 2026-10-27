// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Real mempool admission with the standardness switch ON (accept_non_standard_transactions = false), for the IsStandardTx and
// AreInputsStandard scenarios of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b): src/test/transaction_tests.cpp
// test_IsStandard, src/policy/policy.cpp IsStandardTx / AreInputsStandard, test/functional/abc-wallet-standardness.py, bchn-opreturn.py
// and feature_tx_version.py.
// Original: Copyright (c) 2009-present The Bitcoin Core / Bitcoin developers, MIT license.
//
// The chain is 100 regtest blocks plus a funding block with five outputs (three P2PKH, a 202-byte anyone-can-spend script and a 201-byte one).
// Both regtests, BCHN's and Knuth's, have every upgrade through 2026 active from height 0 (BCHN chainparams.cpp CRegTestParams; Knuth
// chain_state::is_rule_enabled returns true for regtest), so the post-activation side of every phase-gated rule is what runs here; the
// pre-activation side cannot be reached with this fixture, which only builds regtest chains. In that phase (after Upgrade 12) BCHN's
// Solver (script/standard.cpp) treats any output script of at most 201 bytes that is not another template as pay-to-script, standard;
// the scriptpubkey refusals that abc-wallet-standardness and test_IsStandard assert (bare OP_1, non-minimal pushes, 1-of-5 multisig) belong
// to the phase before Upgrade 12, which those tests force (upgrade12activationheight=2000000000) or leave out of the flags.
// Every transaction is built signed with the existing miner helpers and judged by block_chain::transaction_validate, which does not admit it.

#ifndef KTH_NODE_TEST_BCHN_STANDARDNESS_HARNESS_HPP
#define KTH_NODE_TEST_BCHN_STANDARDNESS_HARNESS_HPP

#include <test_helpers.hpp>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include "bchn_reorg_scenario.hpp"

namespace kth::test::standardness {

namespace chain = kth::domain::chain;

template <typename Make>
code run_on_chain(blockchain::block_chain& c, Make make) {
    return ::asio::co_spawn(c.executor(),
        [make = std::move(make)]() -> ::asio::awaitable<code> { co_return co_await make(); },
        ::asio::use_future).get();
}

inline chain::script script_of(data_chunk const& raw) {
    byte_reader reader(raw);
    auto parsed = chain::script::from_data(reader, false);
    REQUIRE(parsed);
    return std::move(*parsed);
}

inline data_chunk cat(std::initializer_list<data_chunk> parts) {
    data_chunk out;
    for (auto const& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

inline data_chunk pubkey_bytes() { return miner_public().to_data(); }
inline data_chunk pubkey_hash() { auto h = bitcoin_short_hash(pubkey_bytes()); return data_chunk(h.begin(), h.end()); }

// scriptPubKey templates, with the exact push opcodes of the originals
inline data_chunk p2pk(bool minimal = true) {
    return minimal ? cat({{0x21}, pubkey_bytes(), {0xac}}) : cat({{0x4c, 0x21}, pubkey_bytes(), {0xac}});
}
inline data_chunk p2pkh(bool minimal = true) {
    return minimal ? cat({{0x76, 0xa9, 0x14}, pubkey_hash(), {0x88, 0xac}}) : cat({{0x76, 0xa9, 0x4c, 0x14}, pubkey_hash(), {0x88, 0xac}});
}
inline data_chunk p2sh() { return cat({{0xa9, 0x14}, data_chunk(20, 0x42), {0x87}}); }
inline data_chunk p2sh32() { return cat({{0xaa, 0x20}, data_chunk(32, 0x42), {0x87}}); }
inline data_chunk bare_one() { return {0x51}; }
// the two witness programs of abc-segwit-recovery.py, as the redeem scripts of P2SH outputs (P2SH-P2WPKH and P2SH-P2WSH shapes)
inline data_chunk witness_program_0() { return {0x00, 0x14, 0xfc, 0xf9, 0x96, 0x9c, 0xe1, 0xc9, 0x8a, 0x13, 0x5e, 0xd2, 0x93, 0x71, 0x97, 0x21, 0xfb, 0x69, 0xf0, 0xb6, 0x86, 0xcb}; }
inline data_chunk witness_program_1() {
    return {0x00, 0x20, 0xfc, 0x8b, 0x08, 0xed, 0x63, 0x6c, 0xb2, 0x3a, 0xfc, 0xb4, 0x25, 0xff, 0x26, 0x0b, 0x3a, 0xbd, 0x03, 0x38, 0x0a, 0x23, 0x33, 0xb5, 0x4c, 0xfa, 0x5d, 0x51, 0xac, 0x52, 0xd8, 0x03, 0xba, 0xf4};
}
inline data_chunk drop_redeem(size_t drops) { data_chunk r(drops, 0x75); r.push_back(0x51); return r; }   // OP_DROP * drops, OP_1
inline data_chunk p2sh_of(data_chunk const& redeem) {
    auto h = bitcoin_short_hash(redeem);
    return cat({{0xa9, 0x14}, data_chunk(h.begin(), h.end()), {0x87}});
}
// an anyone-can-spend script of `total` bytes: OP_NOP * (total - 1) then OP_1
inline data_chunk anyone_script(size_t total) { data_chunk out(total - 1, 0x61); out.push_back(0x51); return out; }
// null data of exactly `total` script bytes: OP_RETURN, then one PUSHDATA1 push (total >= 3)
inline data_chunk null_data(size_t total) {
    data_chunk out{0x6a, 0x4c, uint8_t(total - 3)};
    out.insert(out.end(), total - 3, 0x77);
    return out;
}
inline data_chunk null_data_short(size_t push) {      // OP_RETURN, direct push of `push` bytes
    data_chunk out{0x6a, uint8_t(push)};
    out.insert(out.end(), push, 0x77);
    return out;
}

// The upgrade phase the fixture validates in. Knuth's regtest activates every height rule (chain_state::is_rule_enabled returns true), but the flags a
// block or a transaction is validated with are chain_state::activation's `to_flags(upgrade) & settings.enabled_flags()`, and the settings carry one switch per
// upgrade (bch_descartes = Upgrade 9, bch_lobachevski = 10, bch_galois = 11, bch_leibniz = 12). The upgrade masks are cumulative, so a phase switches off the
// named upgrade and every later one; the effective bits are then read back from the chain state and required.
enum class phase { all, before_u12, before_u11, before_u9 };

struct harness {
    test::chain_fixture fixture;
    std::vector<chain::block> trunk;
    chain::transaction funding;
    hash_digest tip_hash;
    uint32_t tip_height{0};
    uint32_t base_time{0};
    static constexpr uint64_t funded = 100'000'000;

    explicit harness(char const* tag, bool require_standard = true, phase ph = phase::all, float byte_fee = -1.0f) : fixture(tag) {
        REQUIRE(fixture.created());
        fixture.settings().accept_non_standard_transactions = ! require_standard;
        auto& cfg = fixture.settings();
        if (byte_fee >= 0) cfg.byte_fee_satoshis = byte_fee;
        if (ph != phase::all) cfg.bch_leibniz = false;
        if (ph == phase::before_u11 || ph == phase::before_u9) cfg.bch_galois = false;
        if (ph == phase::before_u9) { cfg.bch_lobachevski = false; cfg.bch_descartes = false; }
        REQUIRE(fixture.start());
        check_effective_flags(ph);
        constexpr uint32_t trunk_len = 100;
        auto prev = chain::block::genesis_regtest().hash();
        auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;
        for (uint32_t h = 1; h <= trunk_len; ++h) {
            trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
        REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
        persist_headers(fixture, trunk, 1);
        connect_bodies(fixture, trunk, 1);

        // funding transaction: outputs 0, 1 and 3 pay the miner key (P2PKH); output 2 is a 202-byte and output 4 a 201-byte anyone-can-spend script;
        // outputs 5 and 6 are P2SH of the two witness programs of abc-segwit-recovery.py, outputs 7 and 8 P2SH of redeem scripts that drop 3 and 4 items
        auto const& cb = trunk.front().transactions().front();
        chain::output::list outs;
        outs.emplace_back(funded, script_of(p2pkh()), chain::token_data_opt{});
        outs.emplace_back(funded, script_of(p2pkh()), chain::token_data_opt{});
        outs.emplace_back(funded, script_of(anyone_script(202)), chain::token_data_opt{});
        outs.emplace_back(funded, script_of(p2pkh()), chain::token_data_opt{});
        outs.emplace_back(funded, script_of(anyone_script(201)), chain::token_data_opt{});
        outs.emplace_back(funded, script_of(p2sh_of(witness_program_0())), chain::token_data_opt{});
        outs.emplace_back(funded, script_of(p2sh_of(witness_program_1())), chain::token_data_opt{});
        outs.emplace_back(funded, script_of(p2sh_of(drop_redeem(3))), chain::token_data_opt{});
        outs.emplace_back(funded, script_of(p2sh_of(drop_redeem(4))), chain::token_data_opt{});
        funding = bchn_reorg::signed_spend_to(cb, 0, std::move(outs), flags());
        auto const fee = cb.outputs()[0].value() - 9 * funded;
        auto const block = mine_block(prev, trunk_len + 1, base_time + (trunk_len + 1) * block_spacing, 0, {funding}, fee);
        REQUIRE(fixture.organizer().add_headers(headers_of({block})).headers_added == 1);
        persist_headers(fixture, {block}, trunk_len + 1);
        connect_bodies(fixture, {block}, trunk_len + 1);
        tip_hash = block.hash();
        tip_height = trunk_len + 1;
        this->base_time = base_time;
    }

    // Mine `txs` on the tip (the coinbase claims `fee`) and connect it through the node path; returns the connected height.
    uint32_t connect_block(std::vector<chain::transaction> txs, uint64_t fee) {
        auto const block = mine_block(tip_hash, tip_height + 1, base_time + (tip_height + 1) * block_spacing, 0, std::move(txs), fee);
        REQUIRE(fixture.organizer().add_headers(headers_of({block})).headers_added == 1);
        persist_headers(fixture, {block}, tip_height + 1);
        run_connect_tasks(fixture, {block}, tip_height + 1, [](std::string const&) {});
        auto const heights = fixture.chain().get_last_heights();
        REQUIRE(heights);
        return uint32_t(heights->block);
    }

    // The flags of the state a transaction is validated against (the chain state after the tip), not only the settings mask
    void check_effective_flags(phase ph) {
        using domain::machine::script_flags;
        auto const view = fixture.chain().chain_view();
        REQUIRE(view);
        auto const f = view->state->enabled_flags();
        auto const on = [&](script_flags bit) -> bool { return (domain::script_flags_t(f) & domain::script_flags_t(bit)) != 0; };
        // always present: the Upgrade 8 bits and the 2018 rules
        REQUIRE(on(script_flags::bch_64bit_integers) == true);
        REQUIRE(on(script_flags::bch_sigpushonly) == true);
        bool const u9 = ph == phase::all || ph == phase::before_u12 || ph == phase::before_u11;
        bool const u11 = ph == phase::all || ph == phase::before_u12;
        bool const u12 = ph == phase::all;
        CHECK(on(script_flags::bch_p2sh_32) == u9);
        CHECK(on(script_flags::bch_tokens) == u9);
        CHECK(on(script_flags::bch_vm_limits) == u11);
        CHECK(on(script_flags::bch_bigint) == u11);
        CHECK(on(script_flags::bch_loops) == u12);
        CHECK(on(script_flags::bch_subroutines) == u12);
        CHECK(on(script_flags::bch_bitwise_ops) == u12);
        CHECK(on(script_flags::bch_p2s) == u12);
    }

    domain::script_flags_t flags() { return fixture.chain().chain_settings().enabled_flags(); }

    // A transaction that spends funding output `index` and pays `outs`. P2PKH inputs are signed; the anyone-can-spend inputs (2 and 4) have an empty scriptSig; 5 and 6 are spent by spend_segwit_recovery.
    // `extra` operations are appended to the scriptSig (a signed P2PKH one).
    chain::transaction spend(uint32_t index, chain::output::list outs, uint32_t version = 1,
                             domain::machine::operation::list extra = {}) {
        chain::input::list inputs;
        inputs.emplace_back(chain::output_point{funding.hash(), index}, chain::script{}, 0xffffffff);
        chain::transaction unsigned_tx(version, 0, std::move(inputs), chain::output::list(outs));
        unsigned_tx.inputs()[0].previous_output().validation.cache = funding.outputs()[index];
        chain::script unlocking;
        if (index != 2 && index != 4) {
            auto sig = chain::script::create_endorsement(miner_secret(), miner_script(), unsigned_tx, 0,
                uint8_t(infrastructure::machine::sighash_algorithm::forkid_all), flags(), funding.outputs()[index].value());
            REQUIRE(sig);
            auto ops = chain::script::to_pay_public_key_hash_pattern_unlocking(*sig, miner_public());
            for (auto& op : extra) ops.push_back(std::move(op));
            unlocking = chain::script(std::move(ops));
        }
        chain::input::list signed_ins;
        signed_ins.emplace_back(chain::output_point{funding.hash(), index}, std::move(unlocking), 0xffffffff);
        return chain::transaction(version, 0, std::move(signed_ins), std::move(outs));
    }

    // The spend of funding output 7 (three pushes) or 8 (four pushes): a P2SH output whose redeem script drops that many items and leaves OP_1. The scriptSig is
    // `pushes` pushes of 500 bytes (each within the element size limit) and the push of the redeem script: valid by consensus, push only, clean stack, with a
    // scriptSig of about 503 * pushes + 6 bytes.
    chain::transaction spend_p2sh_padded(size_t pushes) {
        uint32_t const index = pushes == 3 ? 7 : 8;
        auto const redeem = drop_redeem(pushes);
        domain::machine::operation::list ops;
        for (size_t i = 0; i < pushes; ++i) ops.emplace_back(data_chunk(500, 0x07));
        ops.emplace_back(redeem);
        chain::input::list inputs;
        inputs.emplace_back(chain::output_point{funding.hash(), index}, chain::script(std::move(ops)), 0xffffffff);
        return chain::transaction(1, 0, std::move(inputs), pay(p2pkh(), funded - 1000));
    }

    // one output of `script_bytes` worth `value`
    static chain::output::list pay(data_chunk const& script_bytes, uint64_t value = 10000) {
        chain::output::list outs;
        outs.emplace_back(value, script_of(script_bytes), chain::token_data_opt{});
        return outs;
    }

    // The spend of the P2SH output `index` (5 or 6) whose redeem script is a witness program: the scriptSig is that one push (segwit recovery)
    chain::transaction spend_segwit_recovery(uint32_t index) {
        auto const redeem = index == 5 ? witness_program_0() : witness_program_1();
        chain::input::list inputs;
        inputs.emplace_back(chain::output_point{funding.hash(), index}, chain::script(domain::machine::operation::list{domain::machine::operation(redeem)}), 0xffffffff);
        return chain::transaction(1, 0, std::move(inputs), pay(p2pkh(), funded - 1000));
    }

    // The relay entry: block_chain::organize(tx) (what sendrawtransaction and the persisted-mempool revalidation use). It runs the same checks as
    // transaction_validate in the same order and then inserts into the mempool and announces. `subscribe()` must be called before to observe the announcement.
    code organize(chain::transaction const& tx) {
        auto ptr = std::make_shared<domain::message::transaction>(tx);
        return run_on_chain(fixture.chain(), [this, ptr] { return fixture.chain().organize(ptr); });
    }
    bool pooled(chain::transaction const& tx) { return fixture.chain().mempool_ref().contains(tx.hash()); }
    size_t pool_size() { return fixture.chain().mempool_ref().size(); }
    blockchain::block_chain::transaction_channel_ptr subscribe() { return fixture.chain().subscribe_transaction(); }
    static bool announced(blockchain::block_chain::transaction_channel_ptr const& channel, hash_digest const& hash) {
        bool seen = false;
        while (channel->try_receive([&](std::error_code, domain::message::transaction::const_ptr tx) { seen = seen || tx->hash() == hash; })) {}
        return seen;
    }

    code validate(chain::transaction const& tx) {
        auto ptr = std::make_shared<domain::message::transaction>(tx);
        return run_on_chain(fixture.chain(), [this, ptr] { return fixture.chain().transaction_validate(ptr); });
    }
};

} // namespace kth::test::standardness

#endif
