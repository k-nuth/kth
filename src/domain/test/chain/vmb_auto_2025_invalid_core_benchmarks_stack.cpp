// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

#include "vmb_vectors.hpp"
#include <kth/domain/machine/script_flags.hpp>

namespace kth::test::data_vmb_2025_invalid_9 {
// Source: test/scripts/vmb_tests/bch_2025_invalid/core.benchmarks.stack.vmb_tests.json
static constexpr vmb_vector rows_0[] = {
    {"yudc3j", "Transaction validation benchmarks: Within BCH_2023_05 P2S/nonstandard, single-input limits, maximize bytes pushed to the stack (P2S)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000000056a0361616100000000", "011027000000000000cd515102080280766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d", 0, false},
};
static constexpr vmb_vector_group group = {
    "test/scripts/vmb_tests/bch_2025_invalid/core.benchmarks.stack.vmb_tests.json", rows_0, kth::domain::to_flags(kth::domain::machine::upgrade::bch_galois) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule, false
};
} // namespace kth::test::data_vmb_2025_invalid_9
