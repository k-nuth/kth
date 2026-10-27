// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// feature_block.py b61 of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b), BIP30: a block whose coinbase has the same txid as an
// earlier coinbase that is not fully spent is refused (bad-txns-BIP30). On the real node path (headers, storage, utxo_build_task over
// validate_block_batch and the UTXO delta). Control: the same duplicate is valid once the earlier coinbase is fully spent.
// In Knuth the refusal is made by the UTXO delta (UTXO-Z insert: duplicated_key) and stops the UTXO build, not by validate_block_batch, which
// accepts the duplicate on its own; the outcome (the block is not connected) is the one of BCHN, the reason (bad-txns-BIP30) is not reproduced.
// Original: Copyright (c) 2015-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.

#include <algorithm>

#include "bchn_reorg_scenario.hpp"

using namespace kth;
using namespace kth::test;
using kth::test::bchn_reorg::scenario;

TEST_CASE("BCHN feature_block b61 - a coinbase repeating an unspent coinbase txid is refused", "[bchn_non_vm][bip30]") {
    scenario s("bchn_ev_bip30");
    // the coinbase of trunk block 2 is unspent; block 106 repeats it exactly (BCHN regtest does not require the height in the coinbase)
    domain::chain::transaction::list all;
    all.push_back(s.trunk[1].transactions().front());
    domain::chain::block const dup = [&] {
        domain::chain::block candidate(domain::chain::header{0x20000000, s.trunk.back().hash(), null_hash,
            s.base_time + 106 * block_spacing, regtest_bits, 0}, std::move(all));
        auto const merkle = candidate.generate_merkle_root();
        for (uint32_t nonce = 0;; ++nonce) {
            domain::chain::header header{0x20000000, s.trunk.back().hash(), merkle, s.base_time + 106 * block_spacing, regtest_bits, nonce};
            if (header.is_valid_proof_of_work(domain::chain::hash(header), false)) {
                return domain::chain::block(header, domain::chain::transaction::list(candidate.transactions()));
            }
        }
    }();
    REQUIRE(dup.transactions().front().hash() == s.trunk[1].transactions().front().hash());
    REQUIRE(s.headers({dup}).headers_added == 1);
    persist_headers(s.fixture, {dup}, 106);
    run_connect_tasks(s.fixture, {dup}, 106, [](std::string const&) {});
    CHECK(s.connected_height() == 105);
}

TEST_CASE("BCHN feature_block b61 - control: a coinbase repeating a fully spent coinbase txid is valid", "[bchn_non_vm][bip30]") {
    scenario s("bchn_ev_bip30_spent");
    // block 106 spends the coinbase of trunk block 1; block 107 then repeats that coinbase exactly
    auto const spender = s.next(s.trunk.back().hash(), 106, 1, 1);
    REQUIRE(s.headers({spender}).headers_added == 1);
    persist_headers(s.fixture, {spender}, 106);
    connect_bodies(s.fixture, {spender}, 106);
    CHECK_FALSE(s.unspent(s.trunk[0].transactions().front().hash()));

    domain::chain::transaction::list all;
    all.push_back(s.trunk[0].transactions().front());
    domain::chain::block candidate(domain::chain::header{0x20000000, spender.hash(), null_hash, s.base_time + 107 * block_spacing, regtest_bits, 0}, std::move(all));
    auto const merkle = candidate.generate_merkle_root();
    domain::chain::block dup = candidate;
    for (uint32_t nonce = 0;; ++nonce) {
        domain::chain::header header{0x20000000, spender.hash(), merkle, s.base_time + 107 * block_spacing, regtest_bits, nonce};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), false)) {
            dup = domain::chain::block(header, domain::chain::transaction::list(candidate.transactions()));
            break;
        }
    }
    REQUIRE(s.headers({dup}).headers_added == 1);
    persist_headers(s.fixture, {dup}, 107);
    run_connect_tasks(s.fixture, {dup}, 107, [](std::string const&) {});
    CHECK(s.connected_height() == 107);
}
