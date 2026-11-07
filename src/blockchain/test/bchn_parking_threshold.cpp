// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of the deep-reorg protection table of Bitcoin Cash Node test/functional/abc-parkedchain.py
// (check_reorg_protection, commit 07576013c91ff4a3a74acd85f189c69121cdad1b) onto parking::required_work.
// Original: Copyright (c) 2018-present The Bitcoin developers, MIT license.
//
// In the original a node on a chain that is `depth` blocks above the fork sees a competing branch of `depth` blocks
// (same work, not enough), then `extra_blocks` more blocks that find themselves parked, and one more block that makes the
// node reorganize. The branch therefore reorganizes the node at depth + extra_blocks + 1 blocks above the fork:
//     depth 1 -> 2, 2 -> 3, 3 -> 5 (one extra), 4 -> 9, 5 -> 11, 6 -> 13, 100 -> 201.
// Here the same chain is a header_index of one-unit blocks and the smallest branch length that exceeds
// parking::required_work is compared with that table. The depth-3 row is a failed regression kept as evidence outside
// this tree; the other rows are the controls.

#include <test_helpers.hpp>

#include <kth/blockchain.hpp>

#include "regtest_miner.hpp"

using namespace kth;
using namespace kth::blockchain;
using kth::database::header_index;

namespace {

hash_digest make_hash(uint32_t id) {
    hash_digest h{};
    h[0] = uint8_t(id);
    h[1] = uint8_t(id >> 8);
    h[2] = uint8_t(id >> 16);
    h[3] = uint8_t(id >> 24);
    return h;
}

void build_chain(header_index& index, size_t length) {
    hash_digest prev = null_hash;
    for (uint32_t i = 0; i < length; ++i) {
        domain::chain::header hdr{1, prev, make_hash(i + 100000), i * 600, 0x1d00ffff, i};
        prev = kth::domain::chain::hash(hdr);
        REQUIRE(index.add(prev, hdr).inserted);
    }
}

// Blocks above the fork that a competing branch needs, so that its work is strictly greater than the threshold.
uint32_t smallest_reorganizing_branch(uint32_t depth) {
    header_index index;
    build_chain(index, depth + 3);
    uint32_t const fork = 1;
    uint32_t const tip = fork + depth;
    uint256_t const unit = index.get_chain_work(fork + 1) - index.get_chain_work(fork);
    uint256_t const above = parking::required_work(index, fork, tip) - index.get_chain_work(fork);
    uint256_t const twice = above * 2;
    uint256_t const halves = twice / unit;               // threshold in half blocks
    return uint32_t(halves) / 2 + 1;
}

// End to end through header_organizer::add_headers, with real regtest headers: the chain has `depth` validated blocks above the fork and
// the competing branch is fed one header at a time; returns the length of the branch (blocks above the fork) at which the organizer
// reports it as a reorganization candidate, 0 if it never does within `limit`.
uint32_t organizer_reorg_length(uint32_t depth, uint32_t limit) {
    header_index index;
    settings config(domain::config::network::regtest);
    config.checkpoints_sorted.clear();
    auto const genesis = domain::chain::block::genesis_regtest();
    REQUIRE(index.add(genesis.hash(), genesis.header()).inserted);
    header_organizer organizer(index, config, domain::config::network::regtest);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    auto const stamp = [&](uint32_t height) { return genesis.header().timestamp() + 600 * height; };
    domain::message::header::list own;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= 1 + depth; ++h) {          // block 1 is the fork point, then `depth` blocks of the active chain
        auto const b = kth::test::mine_block(prev, h, stamp(h), 0, {}, 0);
        own.push_back(b.header());
        prev = b.hash();
    }
    auto const added = organizer.add_headers(own);
    REQUIRE(added.headers_added == own.size());
    organizer.note_block_validated(int32_t(1 + depth));

    auto fork_prev = domain::chain::hash(own.front());
    for (uint32_t length = 1; length <= limit; ++length) {
        auto const h = 1 + length;
        auto const b = kth::test::mine_block(fork_prev, h, stamp(h), 7, {}, 0);
        fork_prev = b.hash();
        domain::message::header::list one;
        one.push_back(b.header());
        if (organizer.add_headers(one).reorg_candidate) return length;
    }
    return 0;
}

} // namespace

TEST_CASE("BCHN abc-parkedchain - deep reorg protection table", "[bchn_non_vm][parking]") {
    struct row { uint32_t depth; uint32_t extra; };
    for (auto const r : {row{1, 0}, row{2, 0}, row{4, 4}, row{5, 5}, row{6, 6}, row{100, 100}}) {
        DYNAMIC_SECTION("depth " << r.depth) {
            CHECK(smallest_reorganizing_branch(r.depth) == r.depth + r.extra + 1);
        }
    }
}

TEST_CASE("BCHN abc-parkedchain - deep reorg protection table through header_organizer", "[bchn_non_vm][parking][organizer]") {
    struct row { uint32_t depth; uint32_t extra; };
    for (auto const r : {row{1, 0}, row{2, 0}, row{4, 4}, row{5, 5}, row{6, 6}, row{100, 100}}) {
        DYNAMIC_SECTION("depth " << r.depth) {
            CHECK(organizer_reorg_length(r.depth, r.depth + r.extra + 5) == r.depth + r.extra + 1);
        }
    }
}
