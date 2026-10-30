// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

#include "vmb_vectors.hpp"
#include <kth/domain/machine/script_flags.hpp>

namespace kth::test::data_vmb_2025_nonstandard_1 {
// Source: test/scripts/vmb_tests/bch_2025_nonstandard/core.benchmarks.arithmetic.add-sub.vmb_tests.json
static constexpr vmb_vector rows_0[] = {
    {"u33zrd", "Transaction validation benchmarks: Within BCH_2023_05 P2S/nonstandard, single-input limits, maximize OP_ADD (P2S)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000000706ffffffffff7f000000000100000000000000000000000000", "011027000000000000c9766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393", 0, false},
    {"vd72vn", "Transaction validation benchmarks: Within BCH_2023_05 P2S/nonstandard, single-input limits, maximize OP_SUB (P2S)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000000706ffffffffff7f000000000100000000000000000000000000", "011027000000000000c9766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494949494", 0, false},
};
static constexpr vmb_vector_group group = {
    "test/scripts/vmb_tests/bch_2025_nonstandard/core.benchmarks.arithmetic.add-sub.vmb_tests.json", rows_0, kth::domain::to_flags(kth::domain::machine::upgrade::bch_galois) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule, true
};
} // namespace kth::test::data_vmb_2025_nonstandard_1
