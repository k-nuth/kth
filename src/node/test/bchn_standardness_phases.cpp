// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// IsStandardTx in the phases before Upgrade 12 and before Upgrade 9 of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b,
// src/policy/policy.cpp, src/script/standard.cpp Solver; src/test/transaction_tests.cpp test_IsStandard, whose flags omit SCRIPT_ENABLE_MAY2026,
// SCRIPT_ENABLE_P2SH_32 and SCRIPT_ENABLE_TOKENS in its sub-cases; test/functional/abc-wallet-standardness.py, which forces Upgrades 11 and 12 off).
// The phases are built with Knuth's own per-upgrade settings (blockchain::settings bch_leibniz / bch_galois / bch_lobachevski / bch_descartes), switched off
// before the chain starts together with every later upgrade because the upgrade masks are cumulative; the harness requires the bits of the chain state the
// transactions are validated against (see bchn_standardness_harness.hpp). This reproduces the normative flag phase of BCHN's test, not the height switch.
// Controls here: what BCHN accepts in the phase is accepted, and the funding block (which carries outputs the phase calls non-standard) is valid by consensus.
// BCHN refusals are checked by policy::check_transaction_standardness_bchn; Knuth need not enforce that profile.
// Original: Copyright (c) 2009-present The Bitcoin Core / Bitcoin developers, MIT license.

#include "bchn_standardness_harness.hpp"

using namespace kth;
using namespace kth::test::standardness;

TEST_CASE("BCHN test_IsStandard before Upgrade 12 - what is standard is accepted", "[bchn_non_vm][standardness][phase]") {
    harness h("bchn_std_u11", true, phase::before_u12);
    auto const ok = [&](char const* what, chain::output::list outs) {
        DYNAMIC_SECTION(what) {
            auto const tx = h.spend(0, std::move(outs));
            CHECK(domain::policy::check_transaction_standardness_bchn(tx, h.flags()) == error::success);
            CHECK(h.validate(tx) == error::success);
        }
    };
    ok("P2PKH", harness::pay(p2pkh(), 90'000'000));
    ok("P2PK", harness::pay(p2pk()));
    ok("bare 1-of-1 multisig", harness::pay(cat({{0x51, 0x21}, pubkey_bytes(), {0x51, 0xae}})));
    ok("P2SH", harness::pay(p2sh()));
    ok("P2SH32 (Upgrade 11 is active)", harness::pay(p2sh32()));
    ok("null data of 223 bytes", harness::pay(null_data(223), 0));
    DYNAMIC_SECTION("a token output (Upgrade 9 is active)") {
        chain::output::list outs;
        outs.emplace_back(1'000'000, script_of(p2pkh()), chain::token_data_opt{chain::make_fungible(h.funding.hash(), 1)});
        CHECK(h.validate(h.spend(0, std::move(outs))) == error::success);
    }
    DYNAMIC_SECTION("a scriptSig of about 1607 bytes (three pushes of 500 and the redeem script, a P2SH spend) is within the 1650-byte legacy limit") {
        auto const tx = h.spend_p2sh_padded(3);
        CHECK(tx.inputs()[0].script().serialized_size(false) < 1650);
        CHECK(h.validate(tx) == error::success);
    }
}

TEST_CASE("BCHN test_IsStandard before Upgrade 9 - what is standard is accepted", "[bchn_non_vm][standardness][phase]") {
    harness h("bchn_std_u8", true, phase::before_u9);
    auto const ok = [&](char const* what, chain::output::list outs, uint32_t version = 1) {
        DYNAMIC_SECTION(what) {
            auto const tx = h.spend(0, std::move(outs), version);
            CHECK(domain::policy::check_transaction_standardness_bchn(tx, h.flags()) == error::success);
            CHECK(h.validate(tx) == error::success);
        }
    };
    ok("P2PKH", harness::pay(p2pkh(), 90'000'000));
    ok("P2SH", harness::pay(p2sh()));
    ok("transaction version 1", harness::pay(p2pkh()), 1);
    ok("transaction version 2", harness::pay(p2pkh()), 2);
    ok("null data of 223 bytes", harness::pay(null_data(223), 0));
}

// The general switch off, in the phases: what BCHN's IsStandardTx refuses there is relayed with -acceptnonstdtxn (BCHN's regtest default), and the same
// non-standard outputs are valid by consensus (the funding block of the harness, connected under the phase flags, carries 201- and 202-byte scripts).
TEST_CASE("BCHN test_IsStandard in the phases - with the standardness switch off non-standard transactions are accepted", "[bchn_non_vm][standardness][phase]") {
    {
        harness h("bchn_std_u11_off", /* require_standard */ false, phase::before_u12);
        CHECK(h.validate(h.spend(0, harness::pay(bare_one()))) == error::success);
        CHECK(h.validate(h.spend(0, harness::pay(p2pk(false)))) == error::success);
        CHECK(h.validate(h.spend(4, harness::pay(p2pkh()))) == error::success);                  // spend of the 201-byte output
        CHECK(h.validate(h.spend_p2sh_padded(4)) == error::success);                              // scriptSig above 1650 bytes
    }
    {
        harness h("bchn_std_u8_off", /* require_standard */ false, phase::before_u9);
        CHECK(h.validate(h.spend(0, harness::pay(p2sh32()))) == error::success);
        CHECK(h.validate(h.spend(0, harness::pay(p2pkh()), 3)) == error::success);
    }
}
