// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

#include "vmb_vectors.hpp"
#include <kth/domain/machine/script_flags.hpp>

namespace kth::test::data_vmb_2025_standard_47 {
// Source: test/scripts/vmb_tests/bch_2025_standard/core.formatting.vmb_tests.json
static constexpr vmb_vector rows_0[] = {
    {"u265dp", "Formatting: OP_NUM2BIN allows 32-byte number inputs (P2SH20)", "020000000201000000000000000000000000000000000000000000000000000000000000000000000064417dfb529d352908ee0a88a0074c216b09793d6aa8c94c7640bb4ced51eaefc75d0aef61f7685d0307491e2628da3d4f91e86329265a4a58ca27a41ec0b8910779c32103a524f43d6166ad3567f18b0a5c769c6ab4dc02149f4d5095ccf4e8ffa293e785000000000100000000000000000000000000000000000000000000000000000000000000010000002e000208022980aa02080280aa201ad88784b424b39ad15854e96346fc94f73db487c165f0a9bdd5f348ad4c463c87000000000100000000000000000a6a08766d625f7465737400000000", "0210270000000000001976a91460011c6bf3f1dd98cff576437b9d85de780f497488ac102700000000000017a914009f1d1d7516b4ae51b397167374fc2c05cee3e687", 1, false},
    {"wsc3e7", "Formatting: OP_NUM2BIN allows 32-byte number inputs (P2SH32)", "020000000201000000000000000000000000000000000000000000000000000000000000000000000064417dfb529d352908ee0a88a0074c216b09793d6aa8c94c7640bb4ced51eaefc75d0aef61f7685d0307491e2628da3d4f91e86329265a4a58ca27a41ec0b8910779c32103a524f43d6166ad3567f18b0a5c769c6ab4dc02149f4d5095ccf4e8ffa293e785000000000100000000000000000000000000000000000000000000000000000000000000010000002e000208022980aa02080280aa201ad88784b424b39ad15854e96346fc94f73db487c165f0a9bdd5f348ad4c463c87000000000100000000000000000a6a08766d625f7465737400000000", "0210270000000000001976a91460011c6bf3f1dd98cff576437b9d85de780f497488ac102700000000000023aa2056b5b9c8a86ef61be4a591e8fa4d607a2a708d74d7aa7b99acd7462c7561187587", 1, false},
};
static constexpr vmb_vector_group group = {
    "test/scripts/vmb_tests/bch_2025_standard/core.formatting.vmb_tests.json", rows_0, kth::domain::to_flags(kth::domain::machine::upgrade::bch_galois) | kth::domain::machine::script_flags::bip16_rule | kth::domain::machine::script_flags::bip65_rule | kth::domain::machine::script_flags::bip66_rule | kth::domain::machine::script_flags::bip68_rule | kth::domain::machine::script_flags::bip112_rule | kth::domain::machine::script_flags::bip113_rule, true
};
} // namespace kth::test::data_vmb_2025_standard_47
