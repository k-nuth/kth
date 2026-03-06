// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VMB tests (2023_standard) - DO NOT EDIT
// Source: test/scripts/vmb_tests/bch_2023_standard/core.benchmarks.baseline.vmb_tests.json

#include "vmb_tests.hpp"

namespace {
static constexpr auto flags = kth::domain::to_flags(kth::domain::machine::upgrade::bch_descartes) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule;
struct vmb_entry { char const* ident; char const* description; char const* tx_hex; char const* utxos_hex; uint32_t input_num; bool script_only; };
static vmb_entry const entries[] = {
    {"trxhzt", "Transaction validation benchmarks: [baseline] 2 P2PKH inputs, 2 P2PKH outputs (one Schnorr signature, one ECDSA signature) (nonP2SH)", "02000000020100000000000000000000000000000000000000000000000000000000000000000000006a47304402204a86326ea6e2abb2ba73d490cd3293bdb7ff35886f9571064fb61e3dc64cb28b0220239338de5a5b1d54f7ff07196e16d10456da74b11ef1a79fc1bb02a084a977fd412103a524f43d6166ad3567f18b0a5c769c6ab4dc02149f4d5095ccf4e8ffa293e785000000000100000000000000000000000000000000000000000000000000000000000000010000006441de6174892e09d0b5d48c69d76cd4510d0254fd4a35edb6283454b0be48aa8db13c7d5b4cc84019cdf82a87c5bef2fc7768a7f249b681be49480e61a0b093b2a6412103a524f43d6166ad3567f18b0a5c769c6ab4dc02149f4d5095ccf4e8ffa293e7850000000002a0860100000000001976a9144af864646d46ee5a12f4695695ae78f993cad77588ac32850100000000001976a9144af864646d46ee5a12f4695695ae78f993cad77588ac00000000", "02a0860100000000001976a91460011c6bf3f1dd98cff576437b9d85de780f497488aca0860100000000001976a91460011c6bf3f1dd98cff576437b9d85de780f497488ac", 1, false},
};
} // anonymous namespace

TEST_CASE("VMB 2023 standard - core.benchmarks.baseline", "[vmb][2023][standard]") {
    for (auto const& e : entries) {
        DYNAMIC_SECTION("VMB " << e.ident << ": " << e.description) {
            run_vmb_test({e.ident, e.description, e.tx_hex, e.utxos_hex, e.input_num, flags, true, e.script_only});
        }
    }
}
