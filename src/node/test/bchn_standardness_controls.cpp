// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The standard side of IsStandardTx (Bitcoin Cash Node commit 07576013c91ff4a3a74acd85f189c69121cdad1b, src/policy/policy.cpp, driven by
// src/test/transaction_tests.cpp test_IsStandard and test/functional/abc-wallet-standardness.py, bchn-opreturn.py, feature_tx_version.py):
// transactions BCHN relays are accepted by Knuth's real admission with the standardness switch on. The refusals are in the failed
// regression kept outside this tree (evidence round7/standardness). Post-activation phase only (regtest, every upgrade through 2026 active):
// see bchn_standardness_harness.hpp. In that phase BCHN's Solver treats any script of at most 201 bytes as pay-to-script (standard), so the
// bare OP_1, non-minimal push, 1-of-5 multisig and OP_RETURN OP_RETURN outputs of the originals are accepted by BCHN as well.
// Original: Copyright (c) 2009-present The Bitcoin Core / Bitcoin developers, MIT license.

#include "bchn_standardness_harness.hpp"

using namespace kth;
using namespace kth::test::standardness;

TEST_CASE("BCHN test_IsStandard - standard transactions are accepted with the standardness switch on", "[bchn_non_vm][standardness]") {
    harness h("bchn_std_controls");
    auto const ok = [&](char const* what, uint32_t index, chain::output::list outs, uint32_t version = 1) {
        DYNAMIC_SECTION(what) { CHECK(h.validate(h.spend(index, std::move(outs), version)) == error::success); }
    };
    ok("P2SH output of 540 satoshis (mempool_accept: the standard side of the dust threshold of a 32-byte output)", 0, harness::pay(p2sh(), 540));
    ok("P2PKH output (the 90 CENT transaction of test_IsStandard)", 0, harness::pay(p2pkh(), 90'000'000));
    ok("dust threshold: 546 satoshis to a P2PKH output", 0, harness::pay(p2pkh(), 546));
    ok("P2PK output (abc-wallet-standardness)", 0, harness::pay(p2pk()));
    ok("bare 1-of-1 multisig output (abc-wallet-standardness)", 0, harness::pay(cat({{0x51, 0x21}, pubkey_bytes(), {0x51, 0xae}})));
    ok("P2SH output", 0, harness::pay(p2sh()));
    ok("P2SH32 output after its activation", 0, harness::pay(p2sh32()));
    ok("transaction version 2", 0, harness::pay(p2pkh()), 2);
    ok("null data of MAX_OP_RETURN_RELAY (223) bytes", 0, harness::pay(null_data(223), 0));
    ok("OP_RETURN with no push", 0, harness::pay({0x6a}, 0));
    ok("OP_RETURN with an empty push", 0, harness::pay({0x6a, 0x00}, 0));
    ok("a 201-byte output script (pay-to-script, the longest standard one after Upgrade 12)", 0, harness::pay(anyone_script(201)));
    ok("bare OP_1 output (pay-to-script after Upgrade 12)", 0, harness::pay(bare_one()));
    ok("P2PK with a PUSHDATA1 public key push (pay-to-script after Upgrade 12)", 0, harness::pay(p2pk(false)));
    ok("bare 1-of-5 multisig (173 bytes: pay-to-script after Upgrade 12)", 0,
       harness::pay(cat({{0x51}, {0x21}, pubkey_bytes(), {0x21}, pubkey_bytes(), {0x21}, pubkey_bytes(), {0x21}, pubkey_bytes(), {0x21}, pubkey_bytes(), {0x55, 0xae}})));
    ok("OP_RETURN OP_RETURN (not null data, but a 2-byte pay-to-script after Upgrade 12)", 0, harness::pay({0x6a, 0x6a}, 0));
    ok("OP_RETURN with two pushes", 0, harness::pay({0x6a, 0x01, 0x00, 0x01, 0x01}, 0));
    ok("OP_RETURN with OP_RESERVED, OP_1NEGATE and small numbers (all push opcodes)", 0,
       harness::pay({0x6a, 0x50, 0x4f, 0x00, 0x01, 0x01, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60}, 0));

    DYNAMIC_SECTION("null data of 223 bytes split over three outputs (standard after the May 2021 upgrade)") {
        chain::output::list outs;
        outs.emplace_back(0, script_of(null_data_short(24)), chain::token_data_opt{});
        outs.emplace_back(0, script_of(null_data_short(24)), chain::token_data_opt{});
        outs.emplace_back(0, script_of(null_data(223 - 52)), chain::token_data_opt{});
        CHECK(h.validate(h.spend(0, std::move(outs))) == error::success);
    }
    DYNAMIC_SECTION("two null data outputs") {
        chain::output::list outs;
        outs.emplace_back(0, script_of(null_data_short(30)), chain::token_data_opt{});
        outs.emplace_back(0, script_of(null_data_short(30)), chain::token_data_opt{});
        CHECK(h.validate(h.spend(0, std::move(outs))) == error::success);
    }
    for (int count : {1, 2, 44}) {
        DYNAMIC_SECTION("bchn-opreturn: " << count << " outputs of OP_RETURN with the push xyz (" << count * 5 << " script bytes)") {
            chain::output::list outs;
            for (int i = 0; i < count; ++i) outs.emplace_back(1000, script_of({0x6a, 0x03, 'x', 'y', 'z'}), chain::token_data_opt{});
            CHECK(h.validate(h.spend(0, std::move(outs))) == error::success);
        }
    }
    DYNAMIC_SECTION("223 outputs of a bare OP_RETURN (one byte each, nMaxDatacarrierBytes)") {
        chain::output::list outs;
        for (int i = 0; i < 223; ++i) outs.emplace_back(0, script_of({0x6a}), chain::token_data_opt{});
        CHECK(h.validate(h.spend(0, std::move(outs))) == error::success);
    }
    DYNAMIC_SECTION("spend of a 201-byte (pay-to-script) output: AreInputsStandard accepts it") {
        CHECK(h.validate(h.spend(4, harness::pay(p2pkh()))) == error::success);
    }
    DYNAMIC_SECTION("a standard P2PKH scriptSig (signature, public key)") {
        CHECK(h.validate(h.spend(1, harness::pay(p2pkh()))) == error::success);
    }
    DYNAMIC_SECTION("dust below the limit is refused by both: 200 satoshis to a P2PK output") {
        CHECK(h.validate(h.spend(0, harness::pay(p2pk(), 200))) == error::dusty_transaction);
    }
    DYNAMIC_SECTION("transaction versions outside 1..2 are refused by both (consensus after Upgrade 9)") {
        CHECK(h.validate(h.spend(0, harness::pay(p2pkh()), 3)) == error::invalid_tx_version);
        CHECK(h.validate(h.spend(0, harness::pay(p2pkh()), 0)) == error::invalid_tx_version);
    }
    DYNAMIC_SECTION("a scriptSig that is not push only (signature, public key, OP_NOP) is refused by both: consensus since the 2018 November upgrade (SIGPUSHONLY), not only policy") {
        domain::machine::operation::list extra;
        extra.emplace_back(domain::machine::opcode::nop);
        CHECK(h.validate(h.spend(1, harness::pay(p2pkh()), 1, std::move(extra))) == error::sig_pushonly);
    }
    DYNAMIC_SECTION("a token output after the token activation (genesis from output 0 of the funding transaction)") {
        chain::output::list outs;
        outs.emplace_back(1'000'000, script_of(p2pkh()), chain::token_data_opt{chain::make_fungible(h.funding.hash(), 1)});
        CHECK(h.validate(h.spend(0, std::move(outs))) == error::success);
    }
}

// The general switch (-acceptnonstdtxn) off: what BCHN refuses as non-standard is relayed, as in BCHN's own regtest default. Consensus-valid
// non-standard outputs are also valid in blocks: the funding block of the harness carries a 202-byte output script.
TEST_CASE("BCHN test_IsStandard - with the standardness switch off non-standard transactions are accepted", "[bchn_non_vm][standardness]") {
    harness h("bchn_std_off", /* require_standard */ false);
    CHECK(h.validate(h.spend(0, harness::pay(anyone_script(202)))) == error::success);
    CHECK(h.validate(h.spend(0, harness::pay(null_data(224), 0))) == error::success);
    CHECK(h.validate(h.spend(2, harness::pay(p2pkh()))) == error::success);                     // spend of the 202-byte output
}

// test/functional/abc-segwit-recovery.py: spends of P2SH outputs whose redeem script is a witness program (the scriptSig is that single push) are valid by
// consensus (segwit recovery): a block that carries both is connected. Relay refuses them (policy flags); that is in the failed regression outside this tree.
TEST_CASE("BCHN abc-segwit-recovery - a block with segwit recovery spends is connected", "[bchn_non_vm][standardness]") {
    harness h("bchn_std_segwit_block");
    auto const height = h.connect_block({h.spend_segwit_recovery(5), h.spend_segwit_recovery(6)}, 2000);
    CHECK(height == h.tip_height + 1);
}
