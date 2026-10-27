// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of Bitcoin Cash Node src/test/finalization_tests.cpp, finalizationDelay (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b): automatic finalization of the block 10 below the tip, only once the
// 2-hour finalization delay has expired, remaining fixed while later blocks are too recent and moving again after
// the next expiry.
// Original: Copyright (c) 2018-2026 The Bitcoin developers, MIT license.
//
// BCHN drives it with blocks mined on a TestChain100Setup and a mocked clock; here the headers are added to a
// header_index with an explicit reception time and the clock is the `now` argument of finalization::maybe_advance.

#include <test_helpers.hpp>

#include <vector>

#include <kth/blockchain.hpp>

using namespace kth;
using namespace kth::blockchain;
using kth::database::header_index;

namespace {

constexpr int32_t max_reorg_depth = 10;       // DEFAULT_MAX_REORG_DEPTH
constexpr int64_t delay = 2 * 60 * 60;        // DEFAULT_MIN_FINALIZATION_DELAY

domain::chain::header make_header(hash_digest const& prev, uint32_t id) {
    hash_digest merkle{};
    merkle[0] = uint8_t(id);
    merkle[1] = uint8_t(id >> 8);
    return domain::chain::header{1, prev, merkle, id * 600, 0x1d00ffff, id};
}

} // namespace

TEST_CASE("BCHN finalization_tests - finalizationDelay", "[bchn_non_vm][finalization]") {
    header_index index;
    finalization f(index, max_reorg_depth, delay, /*startup*/ 0);
    std::vector<header_index::index_t> chain;
    hash_digest prev = null_hash;
    int64_t clock = 1'000'000;

    // Connect one more block received "now" and let finalization see the new tip, as the node does per block
    auto const connect = [&] {
        auto const hdr = make_header(prev, uint32_t(chain.size()));
        auto const hash = domain::chain::hash(hdr);
        auto const added = index.add(hash, hdr);
        REQUIRE(added.inserted);
        index.set_received_time(added.index, uint32_t(clock));
        chain.push_back(added.index);
        prev = hash;
        f.maybe_advance(chain.back(), int32_t(chain.size() - 1), clock);
    };
    auto const tip_height = [&] { return int32_t(chain.size() - 1); };

    // The 100 blocks of the test setup are too close to "now": nothing is finalized
    for (int i = 0; i < 100; ++i) connect();
    CHECK(f.finalized() == finalization::null_index);

    // maxreorgdepth more blocks: the delay has not expired
    for (int32_t i = 0; i < max_reorg_depth; ++i) {
        connect();
        CHECK(f.finalized() == finalization::null_index);
    }

    // Let the finalization delay expire: the next maxreorgdepth blocks finalize the block maxreorgdepth below the tip
    clock += delay + 1;
    auto to_finalize = tip_height() - max_reorg_depth;
    for (int32_t i = 0; i < max_reorg_depth; ++i) {
        ++to_finalize;
        connect();
        CHECK(f.finalized_height() == to_finalize);
        CHECK(f.finalized() == chain[size_t(to_finalize)]);
    }

    // The next blocks do not finalize anything: the delay has not expired again
    for (int32_t i = 0; i < max_reorg_depth; ++i) {
        connect();
        CHECK(f.finalized_height() == to_finalize);
    }

    // Let it expire again: finalization moves again
    clock += delay + 1;
    to_finalize = tip_height() - max_reorg_depth;
    for (int32_t i = 0; i < max_reorg_depth; ++i) {
        ++to_finalize;
        connect();
        CHECK(f.finalized_height() == to_finalize);
    }
}

// finalization_header_tests/headerFinalizationEnabledDefault and EnabledNonDefault: with header finalization on (the
// BCHN default), a header whose parent is below the finalized block is refused, one on or above it is admissible.
// BCHN's candidate is the ancestor `maxreorgdepth` below the tip, so depth 10 (default), 1 and 0 finalize tip-10,
// tip-1 and the tip itself. Disabled* (-finalizeheaders=0 accepting every fork) has no Knuth switch: coverage gap.
TEST_CASE("BCHN finalization_header_tests - forks below the finalized block", "[bchn_non_vm][finalization]") {
    for (int32_t depth : {10, 1, 0}) {
        DYNAMIC_SECTION("max reorg depth " << depth) {
            header_index index;
            finalization f(index, depth, /*delay*/ 0, /*startup*/ 0);
            std::vector<header_index::index_t> chain;
            hash_digest prev = null_hash;
            for (uint32_t i = 0; i < 40; ++i) {
                auto const hdr = make_header(prev, i);
                auto const hash = domain::chain::hash(hdr);
                auto const added = index.add(hash, hdr);
                REQUIRE(added.inserted);
                index.set_received_time(added.index, 0);
                chain.push_back(added.index);
                prev = hash;
                f.maybe_advance(chain.back(), int32_t(i), /*now*/ 1'000'000);
            }
            int32_t const finalized_height = 39 - depth;
            REQUIRE(f.finalized_height() == finalized_height);

            // a new header on top of every ancestor, from the tip down to the first block
            for (int32_t parent = 39; parent >= 0; --parent) {
                auto const hdr = make_header(index.get_hash(chain[size_t(parent)]), uint32_t(1000 + parent));
                auto const added = index.add(domain::chain::hash(hdr), hdr);
                REQUIRE(added.inserted);
                bool const admissible = parent >= finalized_height;
                CAPTURE(parent, finalized_height);
                CHECK(f.descends_from_finalized(added.index) == admissible);
            }
        }
    }
}
