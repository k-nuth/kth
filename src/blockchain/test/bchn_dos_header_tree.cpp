// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// test/functional/p2p_dos_header_tree.py of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b) with its real data: the
// 546 testnet3 headers from the genesis to the first checkpoint, and two headers that fork off the genesis at heights 1 and 2.
// Original: Copyright (c) 2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.
//
// Through header_organizer::add_headers on a header_index that holds the testnet genesis. Passing parts (committed here):
// the chain is accepted up to the checkpoint, and the fork headers are accepted when no checkpoint is configured ("-nocheckpoints")
// and when no chain has been fed yet ("no checkpoint has been reached yet by a chain tip"). The refusal of the fork headers once
// the chain has passed the checkpoint is a failed regression kept outside this tree.
// Not ported: the peer disconnect after the refusal.

#include <test_helpers.hpp>

#include <kth/blockchain.hpp>

#include "bchn_testnet3_headers_data.hpp"

using namespace kth;
using namespace kth::blockchain;
using kth::database::header_index;

namespace {

domain::chain::header header_of(char const* hex) {
    auto const raw = decode_base16(hex);
    REQUIRE(raw);
    byte_reader reader(*raw);
    auto parsed = domain::chain::header::from_data(reader);
    REQUIRE(parsed);
    return std::move(*parsed);
}

domain::message::header::list headers(char const* const* hexes, size_t count) {
    domain::message::header::list list;
    for (size_t i = 0; i < count; ++i) list.push_back(header_of(hexes[i]));
    return list;
}

struct testnet_organizer {
    header_index index;
    settings config;
    std::unique_ptr<header_organizer> organizer;

    explicit testnet_organizer(bool with_checkpoint) : config(domain::config::network::testnet) {
        auto const genesis = domain::chain::block::genesis_testnet().header();
        REQUIRE(index.add(domain::chain::hash(genesis), genesis).inserted);
        config.checkpoints_sorted.clear();
        config.checkpoints_sorted.emplace_back(domain::chain::hash(genesis), 0);
        if (with_checkpoint) {
            auto const last = header_of(bchn_testnet3_headers::chain_headers[bchn_testnet3_headers::chain_length - 1]);
            config.checkpoints_sorted.emplace_back(domain::chain::hash(last), bchn_testnet3_headers::chain_length);
        }
        organizer = std::make_unique<header_organizer>(index, config, domain::config::network::testnet);
        REQUIRE(organizer->start());
        organizer->sync_tip();
    }

    header_organize_result feed_chain() {
        return organizer->add_headers(headers(bchn_testnet3_headers::chain_headers, bchn_testnet3_headers::chain_length));
    }
    header_organize_result feed_fork() {
        return organizer->add_headers(headers(bchn_testnet3_headers::fork_headers, 2));
    }
};

} // namespace

TEST_CASE("BCHN p2p_dos_header_tree - the chain up to the first checkpoint is accepted", "[bchn_non_vm][checkpoints][testnet3]") {
    testnet_organizer t(true);
    auto const r = t.feed_chain();
    CHECK_FALSE(r.error);
    CHECK(r.headers_added == bchn_testnet3_headers::chain_length);
    CHECK(t.organizer->header_height() == int32_t(bchn_testnet3_headers::chain_length));
}

TEST_CASE("BCHN p2p_dos_header_tree - the fork headers are accepted without checkpoints", "[bchn_non_vm][checkpoints][testnet3]") {
    testnet_organizer t(false);
    REQUIRE( ! t.feed_chain().error);
    auto const r = t.feed_fork();
    INFO("error: " << r.error.message());
    CHECK(r.error != error::checkpoints_failed);
    CHECK(t.index.size() == bchn_testnet3_headers::chain_length + 1 + 2);       // genesis, chain, the two fork headers
}

TEST_CASE("BCHN p2p_dos_header_tree - the fork headers are accepted when no chain has reached the checkpoint", "[bchn_non_vm][checkpoints][testnet3]") {
    testnet_organizer t(true);
    auto const r = t.feed_fork();
    CHECK(r.headers_added == 2);
    CHECK_FALSE(r.error);
}
