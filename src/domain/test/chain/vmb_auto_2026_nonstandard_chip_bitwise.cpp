// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VMB tests (2026_nonstandard) - DO NOT EDIT
// Source: test/scripts/vmb_tests/bch_2026_nonstandard/chip.bitwise.vmb_tests.json

#include "vmb_tests.hpp"

namespace {
static constexpr auto flags = kth::domain::to_flags(kth::domain::machine::upgrade::bch_leibniz) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule;
struct vmb_entry { char const* ident; char const* description; char const* tx_hex; char const* utxos_hex; uint32_t input_num; bool script_only; };
static vmb_entry const entries[] = {
    {"3f26q4", "OP_INVERT: 10kb 0x00... -> 10KB 0xff... (excessive hashing) (P2S)", "020000000201000000000000000000000000000000000000000000000000000000000000000000000064417dfb529d352908ee0a88a0074c216b09793d6aa8c94c7640bb4ced51eaefc75d0aef61f7685d0307491e2628da3d4f91e86329265a4a58ca27a41ec0b8910779c32103a524f43d6166ad3567f18b0a5c769c6ab4dc02149f4d5095ccf4e8ffa293e78500000000010000000000000000000000000000000000000000000000000000000000000001000000fd10014d0d010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100000000000000000a6a08766d625f7465737400000000", "0210270000000000001976a91460011c6bf3f1dd98cff576437b9d85de780f497488ac1027000000000000280210278083a8202bf7158cb3d8f419f1e19ee71df61927cc17017f37ea8820f3bd719d2b4f88f887", 1, false},
};
} // anonymous namespace

TEST_CASE("VMB 2026 nonstandard - chip.bitwise", "[vmb][2026][nonstandard]") {
    for (auto const& e : entries) {
        DYNAMIC_SECTION("VMB " << e.ident << ": " << e.description) {
            run_vmb_test({e.ident, e.description, e.tx_hex, e.utxos_hex, e.input_num, flags, true, e.script_only});
        }
    }
}
