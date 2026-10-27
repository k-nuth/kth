// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// feature_csv_activation.py (Bitcoin Cash Node, commit 07576013c91ff4a3a74acd85f189c69121cdad1b), BIP68 half, passing parts: every version 1
// transaction of the matrix is valid at any distance; every version 2 transaction with the disable flag is valid at any distance; at
// distance 10 (height and time satisfied) the whole matrix is valid; and before the activation height (576 in BCHN regtest) a locked
// transaction is valid, which in Knuth holds because sequence locks are enforced at no height. The refusals are a failed regression kept
// outside this tree. BIP112 (OP_CHECKSEQUENCEVERIFY) is the interpreter and BIP113 is covered in the BIP113 evidence.
// Original: Copyright (c) 2015-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.

#include "bchn_csv_matrix.hpp"

using namespace kth;
using namespace kth::test;
using namespace kth::test::csv_matrix;

TEST_CASE("BCHN feature_csv_activation - BIP68 matrix, transactions that are valid at every distance", "[bchn_non_vm][csv]") {
    blockchain::settings const s(domain::config::network::regtest);
    funded_chain f(590, s);                            // the coin is in block 590, after the activation height 576
    auto const rows = matrix();
    for (uint32_t i = 0; i < 16; ++i) {
        DYNAMIC_SECTION("version 1, sequence " << rows[i].sequence) {
            CHECK(f.validate_at(8, f.spend(i, 1, rows[i].sequence, s.enabled_flags()), s, "bchn_csv_v1") == error::success);
        }
        if (rows[i].disabled) {
            DYNAMIC_SECTION("version 2 with the disable flag, sequence " << rows[i].sequence) {
                CHECK(f.validate_at(8, f.spend(16 + i, 2, rows[i].sequence, s.enabled_flags()), s, "bchn_csv_v2_sdf") == error::success);
            }
        }
    }
}

TEST_CASE("BCHN feature_csv_activation - BIP68 matrix, version 2 at distance 10", "[bchn_non_vm][csv]") {
    blockchain::settings const s(domain::config::network::regtest);
    funded_chain f(590, s);
    auto const rows = matrix();
    for (uint32_t i = 0; i < 16; ++i) {
        DYNAMIC_SECTION("version 2, sequence " << rows[i].sequence) {
            CHECK(f.validate_at(10, f.spend(16 + i, 2, rows[i].sequence, s.enabled_flags()), s, "bchn_csv_d10") == error::success);
        }
    }
}

TEST_CASE("BCHN feature_csv_activation - before the activation height a locked transaction is valid", "[bchn_non_vm][csv]") {
    blockchain::settings const s(domain::config::network::regtest);
    funded_chain f(560, s);                            // coin in block 560, test block 561: below 576
    auto const rows = matrix();
    for (uint32_t i = 0; i < 16; ++i) {
        DYNAMIC_SECTION("version 2, sequence " << rows[i].sequence) {
            CHECK(f.validate_at(1, f.spend(16 + i, 2, rows[i].sequence, s.enabled_flags()), s, "bchn_csv_pre") == error::success);
        }
    }
}
