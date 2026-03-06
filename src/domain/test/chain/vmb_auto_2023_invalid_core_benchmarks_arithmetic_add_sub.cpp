// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VMB tests (2023_invalid) - DO NOT EDIT
// Source: test/scripts/vmb_tests/bch_2023_invalid/core.benchmarks.arithmetic.add-sub.vmb_tests.json

#include "vmb_tests.hpp"

namespace {
static constexpr auto flags = kth::domain::to_flags(kth::domain::machine::upgrade::bch_descartes) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule;
struct vmb_entry { char const* ident; char const* description; char const* tx_hex; char const* utxos_hex; uint32_t input_num; bool script_only; };
static vmb_entry const entries[] = {
    {"des0rv", "Transaction validation benchmarks: Within BCH_2025_05 P2SH/standard, single-input limits, maximize OP_ADD operand bytes (P2SH20)", "0200000001010000000000000000000000000000000000000000000000000000000000000000000000180f3fffffffffffffffffffffffffffff0702102780bc769300000000010000000000000000016a00000000", "01102700000000000017a914a2dfabf348cd511ad771f045493959d2a7baf67f87", 0, false},
    {"xg7206", "Transaction validation benchmarks: Within BCH_2025_05 P2SH/standard, single-input limits, maximize OP_ADD operand bytes (P2SH32)", "0200000001010000000000000000000000000000000000000000000000000000000000000000000000180f3fffffffffffffffffffffffffffff0702102780bc769300000000010000000000000000016a00000000", "01102700000000000023aa2007a228237bb7b63e3cebd643c0d7dd2c3bc4fef5384abfd0490c5b35c20c4e0887", 0, false},
    {"v8lscg", "Transaction validation benchmarks: Within BCH_2025_05 P2SH/standard, single-input limits, maximize OP_SUB operand bytes (P2SH20)", "0200000001010000000000000000000000000000000000000000000000000000000000000000000000180fffffffffffffffffffffffffffffff07020f2780bc769300000000010000000000000000016a00000000", "01102700000000000017a914d2fa4bdedba95c2b948b0bbb7c1a8c988778937f87", 0, false},
    {"2xhecr", "Transaction validation benchmarks: Within BCH_2025_05 P2SH/standard, single-input limits, maximize OP_SUB operand bytes (P2SH32)", "0200000001010000000000000000000000000000000000000000000000000000000000000000000000180fffffffffffffffffffffffffffffff07020f2780bc769300000000010000000000000000016a00000000", "01102700000000000023aa20e9879d45c8ba7f645f4ea95a4008264653dda24a6d857ebac7c2537bb064824c87", 0, false},
    {"y54lkh", "Transaction validation benchmarks: Within BCH_2025_05 P2SH/standard, single-input limits, maximize (OP_DUP OP_CAT) OP_ADD operand bytes (P2SH20)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000001e04ffffffff18767e767e767e767e767e767e767e767e767e767e767e769300000000010000000000000000016a00000000", "01102700000000000017a914eef8f2cfb5472d853508f372297f540ec57a5ec887", 0, false},
    {"9vkccn", "Transaction validation benchmarks: Within BCH_2025_05 P2SH/standard, single-input limits, maximize (OP_DUP OP_CAT) OP_ADD operand bytes (P2SH32)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000001e04ffffffff18767e767e767e767e767e767e767e767e767e767e767e769300000000010000000000000000016a00000000", "01102700000000000023aa20cba089dad7ccbceb6a9d22d73e572689ef4b6e31b05fef2bc5adfee7a47f32bb87", 0, false},
    {"53s3xu", "Transaction validation benchmarks: Within BCH_2025_05 P2SH20/standard, single-input limits, maximize (OP_DUP OP_CAT) OP_SUB operand bytes (OP_DUP OP_SUB OP_NOT) (P2SH20)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000001f04ffffffff19767e767e767e767e767e767e767e767e767e767e767e76949100000000010000000000000000016a00000000", "01102700000000000017a914abfad42e307b0a182cc91f200b48e35b074cf5ca87", 0, false},
    {"f7qhsg", "Transaction validation benchmarks: Within BCH_2025_05 P2SH20/standard, single-input limits, maximize (OP_DUP OP_CAT) OP_SUB operand bytes (<1> OP_SUB) (P2SH20)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000001e04ffffffff18767e767e767e767e767e767e767e767e767e767e767e519400000000010000000000000000016a00000000", "01102700000000000017a9148da7b3605f41c059af57cc13c7a3d2e96d5ba41187", 0, false},
    {"09macl", "Transaction validation benchmarks: Within BCH_2025_05 P2SH20/standard, single-input limits, balance (OP_DUP OP_CAT) OP_ADD density and operand bytes (P2SH20)", "02000000010100000000000000000000000000000000000000000000000000000000000000000000007a04ffffffff4c73767e767e767e767e767e767e767e766e6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f93939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939393939300000000010000000000000000016a00000000", "01102700000000000017a91432353d7232e62f627f466558666e2c82749be0db87", 0, false},
};
} // anonymous namespace

TEST_CASE("VMB 2023 invalid - core.benchmarks.arithmetic.add-sub", "[vmb][2023][invalid]") {
    for (auto const& e : entries) {
        DYNAMIC_SECTION("VMB " << e.ident << ": " << e.description) {
            run_vmb_test({e.ident, e.description, e.tx_hex, e.utxos_hex, e.input_num, flags, false, e.script_only});
        }
    }
}
