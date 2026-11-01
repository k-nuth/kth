// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Which rule flags a block carries at the activation heights of Bitcoin Cash Node's chainparams.cpp
// (commit 07576013c91ff4a3a74acd85f189c69121cdad1b), checked on chain_state::activation, the function
// that turns height, version history and time into the flags a block is validated with.
// Original: Copyright (c) 2009-present The Bitcoin Core / Bitcoin developers, MIT license.
//
// BCHN: GetNextBlockScriptFlags(parent) enables BIP16, BIP66, BIP65 and CSV script rules when
// `parent.height + 1 >= Height`, i.e. FOR THE BLOCK AT `Height` (validation.cpp); ConnectBlock adds
// BIP68 sequence locks for `block.height >= CSVHeight` and ContextualCheckBlockHeader BIP113 median
// time for `height >= CSVHeight`. The block at CSVHeight is the first one carrying all three.

#include <test_helpers.hpp>

#if defined(KTH_CURRENCY_BCH)

#include <kth/domain/chain/chain_state.hpp>

using namespace kth;
namespace chain = kth::domain::chain;
using kth::domain::machine::script_flags;

namespace {

struct chain_state_test_access : chain::chain_state {
    using chain::chain_state::activation;
};

chain::chain_state::data data_at(size_t height) {
    chain::chain_state::data values{};
    values.height = height;
    values.hash = null_hash;
    values.allow_collisions_hash = null_hash;
    values.bits.self = 0x1d00ffff;
    values.version.self = 4;
    values.timestamp.self = 1'500'000'000;
    values.timestamp.retarget = 1'500'000'000;
    for (size_t i = 0; i < 11; ++i) {
        values.bits.ordered.push_back(0x1d00ffff);
        values.version.ordered.push_back(4);
        values.timestamp.ordered.push_back(1'500'000'000 + uint32_t(i));
    }
    return values;
}

bool has(domain::script_flags_t flags, script_flags bit) {
    return chain::script::is_enabled(flags, bit);
}

constexpr domain::script_flags_t everything = ~domain::script_flags_t(0);

} // namespace

TEST_CASE("BCHN chainparams - BIP66 and BIP65 script rules on mainnet at their heights", "[bchn_non_vm][activation][flags]") {
    using domain::config::network;
    auto const flags_at = [](size_t h) {
        return chain_state_test_access::activation(data_at(h), everything, network::mainnet, bch_cantor_activation_time).flags;
    };
    // BCHN: BIP66Height 363725, BIP65Height 388381, enforced by the block AT that height
    CHECK( ! has(flags_at(363724), script_flags::bip66_rule));
    CHECK(has(flags_at(363725), script_flags::bip66_rule));
    CHECK( ! has(flags_at(388380), script_flags::bip65_rule));
    CHECK(has(flags_at(388381), script_flags::bip65_rule));
}

// The CSV start (BIP68 sequence locks, BIP112, BIP113 at CSVHeight on every network) is not here: Knuth enables
// BIP112 one block late and never sets the BIP68/BIP113 rule flags for BCH. failed_regression
// csv_rules_activation in coverage.json, ported as evidence.

#endif // defined(KTH_CURRENCY_BCH)
