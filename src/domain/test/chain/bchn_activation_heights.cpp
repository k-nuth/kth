// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Activation boundaries of the network upgrades, checked against the consensus
// parameters of Bitcoin Cash Node (src/chainparams.cpp, commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b) that src/test/activation_tests.cpp
// builds its cases from. Original: Copyright (c) 2019-2026 The Bitcoin developers, MIT license.
//
// BCHN decides an upgrade from the PARENT block (`pindexPrev->nHeight >= H`), so the
// first block validated under the new rules is H + 1. chain_state::is_*_enabled
// takes the height of the block itself. Each row is therefore checked at H (off)
// and H + 1 (on).
//
// UAHF, DAA, Magnetic Anomaly and the 2019-2026 upgrades are decided from the PARENT, the CSV rules from the
// block itself (see bchn_activation_flags.cpp).
// Upgrades BCHN activates by median time past (Axion, Upgrade 8 and the 2027 one)
// have no fixed height in chainparams and are classified separately.

#include <test_helpers.hpp>

#if defined(KTH_CURRENCY_BCH)

#include <kth/domain/chain/chain_state.hpp>

using namespace kth;
using kth::domain::chain::chain_state;
using kth::domain::config::network;

namespace {

struct row {
    char const* upgrade;      // BCHN name
    network net;
    size_t bchn_height;       // chainparams value (the last block BEFORE the rules)
    bool (*knuth)(size_t, network);
};

#define ROW(name, net, height, fn) row{name, network::net, height, &chain_state::fn}

} // namespace

TEST_CASE("BCHN activation_tests - upgrade heights of chainparams.cpp", "[bchn_non_vm][activation]") {
    static row const rows[] = {
        // CSV (BIP68/112/113) is not in this table: BCHN decides it from the block's own height, see
        // bchn_activation_flags.cpp and the failed_regression csv_rules_activation in coverage.json.
        // UAHF
        ROW("uahfHeight", mainnet, 478558, is_uahf_enabled),
        ROW("uahfHeight", testnet, 1155875, is_uahf_enabled),
        // uahfHeight 6 on testnet4, scalenet and chipnet: Knuth enables it one block early
        // (failed_regression uahf_height_off_by_one_test_networks in coverage.json)
        // DAA (cw-144)
        ROW("daaHeight", mainnet, 504031, is_daa_cw144_enabled),
        ROW("daaHeight", testnet, 1188697, is_daa_cw144_enabled),
        ROW("daaHeight", testnet4, 3000, is_daa_cw144_enabled),
        ROW("daaHeight", scalenet, 3000, is_daa_cw144_enabled),
        ROW("daaHeight", chipnet, 3000, is_daa_cw144_enabled),
        // Magnetic Anomaly (CTOR, 100-byte minimum transaction)
        ROW("magneticAnomalyHeight", mainnet, 556766, is_euclid_enabled),
        ROW("magneticAnomalyHeight", testnet, 1267996, is_euclid_enabled),
        ROW("magneticAnomalyHeight", testnet4, 4000, is_euclid_enabled),
        ROW("magneticAnomalyHeight", scalenet, 4000, is_euclid_enabled),
        ROW("magneticAnomalyHeight", chipnet, 4000, is_euclid_enabled),
        // Graviton (Nov 2019)
        ROW("gravitonHeight", mainnet, 609135, is_mersenne_enabled),
        ROW("gravitonHeight", testnet, 1341711, is_mersenne_enabled),
        ROW("gravitonHeight", testnet4, 5000, is_mersenne_enabled),
        ROW("gravitonHeight", scalenet, 5000, is_mersenne_enabled),
        ROW("gravitonHeight", chipnet, 5000, is_mersenne_enabled),
        // Phonon (May 2020)
        ROW("phononHeight", mainnet, 635258, is_fermat_enabled),
        ROW("phononHeight", testnet, 1378460, is_fermat_enabled),
        // Upgrade 9 (May 2023, CashTokens)
        ROW("upgrade9Height", mainnet, 792772, is_descartes_enabled),
        ROW("upgrade9Height", testnet, 1552787, is_descartes_enabled),
        ROW("upgrade9Height", testnet4, 148043, is_descartes_enabled),
        ROW("upgrade9Height", scalenet, 10006, is_descartes_enabled),
        ROW("upgrade9Height", chipnet, 121956, is_descartes_enabled),
        // Upgrade 10 (May 2024, ABLA)
        ROW("upgrade10Height", mainnet, 845890, is_lobachevski_enabled),
        ROW("upgrade10Height", testnet, 1605520, is_lobachevski_enabled),
        ROW("upgrade10Height", testnet4, 200740, is_lobachevski_enabled),
        ROW("upgrade10Height", scalenet, 10006, is_lobachevski_enabled),
        ROW("upgrade10Height", chipnet, 174519, is_lobachevski_enabled),
        // Upgrade 11 (May 2025)
        ROW("upgrade11Height", mainnet, 898373, is_galois_enabled),
        ROW("upgrade11Height", testnet, 1658049, is_galois_enabled),
        ROW("upgrade11Height", testnet4, 253318, is_galois_enabled),
        ROW("upgrade11Height", scalenet, 10006, is_galois_enabled),
        ROW("upgrade11Height", chipnet, 227228, is_galois_enabled),
        // Upgrade 12 (May 2026)
        ROW("upgrade12Height", mainnet, 951144, is_leibniz_enabled),
        ROW("upgrade12Height", testnet, 1710482, is_leibniz_enabled),
        ROW("upgrade12Height", testnet4, 305847, is_leibniz_enabled),
        ROW("upgrade12Height", scalenet, 10006, is_leibniz_enabled),
        ROW("upgrade12Height", chipnet, 279791, is_leibniz_enabled),
    };

    for (auto const& r : rows) {
        DYNAMIC_SECTION(r.upgrade << " on network " << int(r.net) << " at " << r.bchn_height) {
            CHECK( ! r.knuth(r.bchn_height, r.net));
            CHECK(r.knuth(r.bchn_height + 1, r.net));
        }
    }
}

#endif // defined(KTH_CURRENCY_BCH)
