// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <future>
#include <vector>

#include <kth/domain/math/mmr.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

TEST_CASE("SHV follows real block connection, disconnection and startup", "[shv][reorg][restart]") {
    chain_fixture fixture("shv_chain");
    fixture.settings().mmr_index = true;
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    constexpr uint32_t fork_height = 12;
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 50 * block_spacing;
    auto previous = genesis.hash();
    std::vector<domain::chain::block> trunk;
    for (uint32_t height = 1; height <= fork_height; ++height) {
        trunk.push_back(mine_block(previous, height, base_time + height * block_spacing, 0, {}, 0));
        previous = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk.size());
    persist_headers(fixture, trunk, 1);
    REQUIRE_FALSE(fixture.chain().shv_root(fork_height));
    connect_bodies(fixture, trunk, 1);
    auto const original = fixture.chain().shv_proof(5, fork_height, true);
    REQUIRE(original);
    auto const fork_root = fixture.chain().shv_root(fork_height);
    REQUIRE(fork_root);

    std::vector<domain::chain::block> branch_a;
    for (uint32_t height = 13; height <= 14; ++height) {
        branch_a.push_back(mine_block(previous, height, base_time + height * block_spacing, 1, {}, 0));
        previous = branch_a.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(branch_a)).headers_added == branch_a.size());
    persist_headers(fixture, branch_a, 13);
    connect_bodies(fixture, branch_a, 13);
    auto const a_root = fixture.chain().shv_root(14);
    REQUIRE(a_root);
    REQUIRE(fixture.restart());
    REQUIRE(fixture.chain().shv_root(14) == a_root);

    std::vector<domain::chain::block> branch_b;
    previous = trunk.back().hash();
    for (uint32_t height = 13; height <= 15; ++height) {
        branch_b.push_back(mine_block(previous, height, base_time + height * block_spacing + 60, 2, {}, 0));
        previous = branch_b.back().hash();
    }
    auto const candidate = fixture.organizer().add_headers(headers_of(branch_b));
    REQUIRE(candidate.reorg_candidate);
    REQUIRE(fixture.chain().shv_root(14) == a_root);

    ::asio::io_context context;
    auto switched = ::asio::co_spawn(context,
        node::sync::execute_reorg(fixture.chain(), fixture.organizer(), candidate.reorg_branch_head,
            fork_height, [] { return false; }, real_persister(fixture.chain())),
        ::asio::use_future);
    context.run_for(std::chrono::seconds(30));
    REQUIRE(switched.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    auto const outcome = switched.get();
    REQUIRE(outcome.result.ok);
    REQUIRE_FALSE(outcome.fatal);

    REQUIRE(fixture.chain().shv_root(fork_height) == fork_root);
    auto const rewound = fixture.chain().shv_proof(5, fork_height, true);
    REQUIRE(rewound);
    REQUIRE(rewound->header == original->header);
    REQUIRE(rewound->proof == original->proof);
    REQUIRE(rewound->target == original->target);
    REQUIRE_FALSE(fixture.chain().shv_root(13));
    REQUIRE_FALSE(fixture.chain().shv_proof(branch_a.back().hash(), 14, true));
    REQUIRE_FALSE(fixture.chain().shv_proof(branch_b.back().hash(), 15, true));

    // Restart while the header tip is ahead of the connected tip. The rebuilt
    // index must stop at the fork until the replacement blocks connect.
    REQUIRE(fixture.restart());
    REQUIRE(fixture.chain().shv_root(fork_height) == fork_root);
    REQUIRE_FALSE(fixture.chain().shv_root(13));
    connect_bodies(fixture, branch_b, 13);
    REQUIRE_FALSE(fixture.chain().shv_proof(branch_a.back().hash(), 15, true));
    auto const proof = fixture.chain().shv_proof(branch_b.back().hash(), 15, true);
    REQUIRE(proof);
    domain::mmr_accumulator client;
    REQUIRE(client.extend(genesis.hash()));
    for (auto const& block : trunk) {
        REQUIRE(client.extend(block.hash()));
    }
    for (auto const& block : branch_b) {
        REQUIRE(client.extend(block.hash()));
    }
    REQUIRE(proof->target == client.root());
    REQUIRE(client.verify_proof_to_root(15, branch_b.back().hash(), proof->proof));
    REQUIRE(fixture.restart());
    REQUIRE(fixture.chain().shv_root(15) == client.root());
}
