// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VMB tests (2025_nonstandard) - DO NOT EDIT
// Source: test/scripts/vmb_tests/bch_2025_nonstandard/core.benchmarks.arithmetic.mul.vmb_tests.json

#include "vmb_tests.hpp"

namespace {
static constexpr auto flags = kth::domain::to_flags(kth::domain::machine::upgrade::bch_galois) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule;
struct vmb_entry { char const* ident; char const* description; char const* tx_hex; char const* utxos_hex; uint32_t input_num; bool script_only; };
static vmb_entry const entries[] = {
    {"snngzt", "Transaction validation benchmarks: Within BCH_2023_05 P2SH/standard, single-input limits, maximize OP_MUL (P2S)", "020000000101000000000000000000000000000000000000000000000000000000000000000000000002018200000000010000000000000000036a016100000000", "011027000000000000ec766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f7605ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957504ffffff7f9595959595959595959595959595959595959595959595959595", 0, false},
};
} // anonymous namespace

TEST_CASE("VMB 2025 nonstandard - core.benchmarks.arithmetic.mul", "[vmb][2025][nonstandard]") {
    for (auto const& e : entries) {
        DYNAMIC_SECTION("VMB " << e.ident << ": " << e.description) {
            run_vmb_test({e.ident, e.description, e.tx_hex, e.utxos_hex, e.input_num, flags, true, e.script_only});
        }
    }
}
