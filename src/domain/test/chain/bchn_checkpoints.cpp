// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Checkpoints of Bitcoin Cash Node (src/chainparams.cpp, commit 07576013c91ff4a3a74acd85f189c69121cdad1b) against
// the lists Knuth ships, and src/test/checkpoints_tests.cpp, sanity. The table comes from
// test/scripts/bchn_checkpoints/generate_checkpoints.py.
// Original: Copyright (c) 2009-present The Bitcoin Core / Bitcoin developers, MIT license.
//
// A BCHN checkpoint missing from Knuth's list, or with another hash, is a consensus-parameter difference: the
// chain below the last checkpoint is accepted without proof-of-work validation on the strength of these hashes.

#include <test_helpers.hpp>

#include <kth/domain/config/parser.hpp>

#include "bchn_checkpoints_data.hpp"

#include <algorithm>
#include <string>

using namespace kth;
using kth::domain::config::network;

namespace {

network network_named(std::string const& name) {
    if (name == "mainnet") return network::mainnet;
    if (name == "testnet") return network::testnet;
    if (name == "testnet4") return network::testnet4;
    if (name == "scalenet") return network::scalenet;
    return network::chipnet;
}

} // namespace

// Where both nodes have a checkpoint at the same height, the hash is the same.
TEST_CASE("BCHN chainparams - checkpoints present in both lists have the same hash", "[bchn_non_vm][checkpoints]") {
    size_t common = 0;
    for (auto const& row : bchn_checkpoints::rows) {
        auto const list = kth::domain::config::default_checkpoints(network_named(row.network));
        auto const it = std::find_if(list.begin(), list.end(), [&](auto const& c) { return c.height() == row.height; });
        if (it == list.end()) continue;
        ++common;
        INFO(row.network << " height " << row.height);
        CHECK(encode_hash(it->hash()) == std::string(row.hash));
    }
    CHECK(common > 40);
}

// Not a rule: BCHN checkpoints that Knuth does not carry. A missing checkpoint costs validation work (the range
// between checkpoints is fully validated) and never makes a valid chain invalid. Run it to list them:
//     kth_domain_test "[bchn_checkpoint_gaps]"
TEST_CASE("BCHN chainparams - list of BCHN checkpoints missing from Knuth", "[.][bchn_checkpoint_gaps][bchn_non_vm][checkpoints]") {
    for (auto const& row : bchn_checkpoints::rows) {
        auto const list = kth::domain::config::default_checkpoints(network_named(row.network));
        auto const it = std::find_if(list.begin(), list.end(), [&](auto const& c) { return c.height() == row.height; });
        if (it == list.end()) WARN("missing: " << row.network << " " << row.height << " " << row.hash);
    }
    SUCCEED();
}

// checkpoints_tests/sanity
TEST_CASE("BCHN checkpoints_tests - sanity", "[bchn_non_vm][checkpoints]") {
    auto const list = kth::domain::config::default_checkpoints(network::mainnet);
    auto const p11111 = "0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d"_hash;
    auto const p134444 = "00000000000005b12ffd4cd315cd34ffd4a594f430ac814c91184a0d42d2b0fe"_hash;
    using checkpoint = kth::infrastructure::config::checkpoint;
    CHECK(checkpoint::validate(p11111, 11111, list));
    CHECK(checkpoint::validate(p134444, 134444, list));
    // Wrong hashes at checkpoints fail
    CHECK( ! checkpoint::validate(p134444, 11111, list));
    CHECK( ! checkpoint::validate(p11111, 134444, list));
    // ... but any hash not at a checkpoint succeeds
    CHECK(checkpoint::validate(p134444, 11111 + 1, list));
    CHECK(checkpoint::validate(p11111, 134444 + 1, list));
}
