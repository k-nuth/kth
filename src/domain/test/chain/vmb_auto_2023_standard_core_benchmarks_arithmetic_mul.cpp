// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VMB tests (2023_standard) - DO NOT EDIT
// Source: test/scripts/vmb_tests/bch_2023_standard/core.benchmarks.arithmetic.mul.vmb_tests.json

#include "vmb_tests.hpp"

namespace {
static constexpr auto flags = kth::domain::to_flags(kth::domain::machine::upgrade::bch_descartes) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule;
struct vmb_entry { char const* ident; char const* description; char const* tx_hex; char const* utxos_hex; uint32_t input_num; bool script_only; };
static vmb_entry const entries[] = {
    {"swae3w", "Transaction validation benchmarks: Within BCH_2023_05 P2SH/standard, single-input limits, maximize OP_MUL (P2SH20)", "0200000001010000000000000000000000000000000000000000000000000000000000000000000000f001824cec766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f7605ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957504ffffff7f959595959595959595959595959595959595959595959595959500000000010000000000000000016a00000000", "01102700000000000017a914fd26373937cbd45cf947dcd24ededb757569603887", 0, false},
    {"qhx767", "Transaction validation benchmarks: Within BCH_2023_05 P2SH/standard, single-input limits, maximize OP_MUL (P2SH32)", "0200000001010000000000000000000000000000000000000000000000000000000000000000000000f001824cec766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f7605ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957504ffffff7f959595959595959595959595959595959595959595959595959500000000010000000000000000016a00000000", "01102700000000000023aa202d308f258e43fffc3838ffed2762707901fa9f8e07f2948d77328f377eaa9f1a87", 0, false},
};
} // anonymous namespace

TEST_CASE("VMB 2023 standard - core.benchmarks.arithmetic.mul", "[vmb][2023][standard]") {
    for (auto const& e : entries) {
        DYNAMIC_SECTION("VMB " << e.ident << ": " << e.description) {
            run_vmb_test({e.ident, e.description, e.tx_hex, e.utxos_hex, e.input_num, flags, true, e.script_only});
        }
    }
}
