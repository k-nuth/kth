// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VMB tests (2026_nonstandard) - DO NOT EDIT
// Source: test/scripts/vmb_tests/bch_2026_nonstandard/core.benchmarks.arithmetic.add-sub.vmb_tests.json

#include "vmb_tests.hpp"

namespace {
static constexpr auto flags = kth::domain::to_flags(kth::domain::machine::upgrade::bch_leibniz) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule;
struct vmb_entry { char const* ident; char const* description; char const* tx_hex; char const* utxos_hex; uint32_t input_num; bool script_only; };
static vmb_entry const entries[] = {
    {"u33zrd", "Transaction validation benchmarks: Within BCH_2023_05 P2S/nonstandard, single-input limits, maximize OP_ADD (P2S)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000000706ffffffffff7f000000000100000000000000000000000000", "011027000000000000c9766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393", 0, false},
    {"vd72vn", "Transaction validation benchmarks: Within BCH_2023_05 P2S/nonstandard, single-input limits, maximize OP_SUB (P2S)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000000706ffffffffff7f000000000100000000000000000000000000", "011027000000000000c9766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494", 0, false},
};
} // anonymous namespace

TEST_CASE("VMB 2026 nonstandard - core.benchmarks.arithmetic.add-sub", "[vmb][2026][nonstandard]") {
    for (auto const& e : entries) {
        DYNAMIC_SECTION("VMB " << e.ident << ": " << e.description) {
            run_vmb_test({e.ident, e.description, e.tx_hex, e.utxos_hex, e.input_num, flags, true, e.script_only});
        }
    }
}
