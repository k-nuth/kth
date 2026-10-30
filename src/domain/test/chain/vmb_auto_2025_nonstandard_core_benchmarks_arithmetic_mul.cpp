// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

#include "vmb_vectors.hpp"
#include <kth/domain/machine/script_flags.hpp>

namespace kth::test::data_vmb_2025_nonstandard_3 {
// Source: test/scripts/vmb_tests/bch_2025_nonstandard/core.benchmarks.arithmetic.mul.vmb_tests.json
static constexpr vmb_vector rows_0[] = {
    {"snngzt", "Transaction validation benchmarks: Within BCH_2023_05 P2SH/standard, single-input limits, maximize OP_MUL (P2S)", "020000000101000000000000000000000000000000000000000000000000000000000000000000000002018200000000010000000000000000036a016100000000", "011027000000000000ec766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f7605ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957505ffffffff7f9595959595959595959595959595959595959595959595957504ffffff7f9595959595959595959595959595959595959595959595959595", 0, false},
};
static constexpr vmb_vector_group group = {
    "test/scripts/vmb_tests/bch_2025_nonstandard/core.benchmarks.arithmetic.mul.vmb_tests.json", rows_0, kth::domain::to_flags(kth::domain::machine::upgrade::bch_galois) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule, true
};
} // namespace kth::test::data_vmb_2025_nonstandard_3
