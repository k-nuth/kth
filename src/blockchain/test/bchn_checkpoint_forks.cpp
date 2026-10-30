// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Ports of Bitcoin Cash Node src/test/checkpoints_tests.cpp (commit 07576013c91ff4a3a74acd85f189c69121cdad1b)
// ban_fork_at_genesis_block and ban_fork_prior_to_and_at_checkpoints through the real header_organizer::add_headers.
// Original: Copyright (c) 2011-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.
//
// Adaptation: BCHN uses four precomputed mainnet headers; the same shape is built here on regtest with mined
// headers (G -> A -> AA, G -> B, A -> AB) and a checkpoint configured at height 2 on the hash of AA.
//     G ---> A ---> AA (checkpointed)
//      \       \
//       \--> B  \-> AB
// The third case of the original, a header forking below an accepted checkpoint, is a failed regression kept as
// evidence outside this tree: Knuth stores such a header (see the matrix entry).
// The reject reasons of BCHN (REJECT_CHECKPOINT) have no Knuth equivalent; what is compared is that the header is
// refused and not stored.

#include <test_helpers.hpp>

#include "reorg_chain_fixture.hpp"
#include "regtest_miner.hpp"
#include <kth/infrastructure/utility/timer.hpp>

using namespace kth;
using namespace kth::test;

namespace {

struct forks {
    domain::chain::block A, AA, B, AB, other_genesis;
    forks() : A(), AA(), B(), AB(), other_genesis() {
        auto const g = domain::chain::block::genesis_regtest();
        auto const t = g.header().timestamp();
        A = mine_block(g.hash(), 1, t + 600, 1, {}, 0);
        AA = mine_block(A.hash(), 2, t + 1200, 1, {}, 0);
        B = mine_block(g.hash(), 1, t + 600, 2, {}, 0);
        AB = mine_block(A.hash(), 2, t + 1200, 2, {}, 0);
        // the regtest genesis with another time and nonce: previous hash null
        other_genesis = mine_block(null_hash, 0, t + 7, 3, {}, 0);
    }
};

domain::message::header::list one(domain::chain::block const& b) {
    domain::message::header::list l;
    l.push_back(b.header());
    return l;
}

bool stored(chain_fixture& f, domain::chain::block const& b) {
    return f.chain().headers().find(b.hash()) != blockchain::header_index::null_index;
}

} // namespace

TEST_CASE("BCHN checkpoints_tests - ban_fork_at_genesis_block", "[bchn_non_vm][checkpoints]") {
    forks const f;
    chain_fixture fixture("bchn_cp_genesis");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const r = fixture.organizer().add_headers(one(f.other_genesis));
    CHECK(r.headers_added == 0);
    CHECK(r.error);
    CHECK_FALSE(stored(fixture, f.other_genesis));
}

TEST_CASE("BCHN checkpoints_tests - ban_fork_prior_to_and_at_checkpoints (fork at the checkpoint)", "[bchn_non_vm][checkpoints]") {
    forks const f;
    chain_fixture fixture("bchn_cp_at");
    REQUIRE(fixture.created());
    fixture.settings().checkpoints_sorted = {infrastructure::config::checkpoint(f.AA.hash(), 2)};
    REQUIRE(fixture.start());

    // Headers A and AA are accepted
    CHECK(fixture.organizer().add_headers(one(f.A)).headers_added == 1);
    auto const aa = fixture.organizer().add_headers(one(f.AA));
    CHECK_FALSE(aa.error);
    CHECK(aa.headers_added == 1);

    // AB, a sibling of AA, mismatches the checkpoint at its height
    auto const ab = fixture.organizer().add_headers(one(f.AB));
    CHECK(ab.error == error::checkpoints_failed);
    CHECK(ab.headers_added == 0);
    CHECK_FALSE(stored(fixture, f.AB));
}

// =============================================================================
// bchn-finalize-headers.py with -finalizeheaders=1 (the only mode Knuth has: header finalization cannot be switched off)
// =============================================================================
//
// 2 * 10 + 1 blocks are mined with the finalization delay at 0, so the block 10 below the tip is finalized. Headers that
// replace a block between the tip and the last non-finalized block are accepted (stored as side branches); a header that
// would replace the finalized block, or one deeper, is refused. The -finalizeheaders=0 half and the DoS penalty/disconnect
// of the peer are not ported (Knuth has no such switch, and peer scoring is not consensus).
// Original: Copyright (c) 2022 The Bitcoin Cash Node developers, MIT license.
TEST_CASE("BCHN bchn-finalize-headers - headers replacing the finalized block are refused", "[bchn_non_vm][finalization]") {
    chain_fixture fixture("bchn_finalize_headers");
    REQUIRE(fixture.created());
    fixture.settings().finalization_delay_seconds = 0;
    REQUIRE(fixture.start());

    constexpr uint32_t tip = 21;
    auto const g = domain::chain::block::genesis_regtest();
    auto const t0 = uint32_t(kth::zulu_time()) - 3600;
    std::vector<domain::chain::block> chain;
    auto prev = g.hash();
    for (uint32_t h = 1; h <= tip; ++h) {
        chain.push_back(mine_block(prev, h, t0 + h, 0, {}, 0));
        prev = chain.back().hash();
    }
    domain::message::header::list headers;
    for (auto const& b : chain) headers.push_back(b.header());
    REQUIRE(fixture.organizer().add_headers(headers).headers_added == tip);
    // the node reports how far blocks are validated; finalization follows that height, not the header tip
    fixture.organizer().note_block_validated(int32_t(tip));

    // a replacement header for height `h`: same parent as the chain's block, another coinbase
    auto const replacement = [&](uint32_t h) {
        auto const parent = h == 1 ? g.hash() : chain[h - 2].hash();
        return mine_block(parent, h, t0 + h, 7, {}, 0);
    };

    // heights tip-1 .. tip-9: accepted, stored beside the chain
    for (uint32_t i = 1; i < 10; ++i) {
        auto const r = replacement(tip - i);
        auto const res = fixture.organizer().add_headers(one(r));
        INFO("replacing height " << tip - i);
        CHECK(res.error != error::finalized_header_violation);
        CHECK(stored(fixture, r));
    }
    // the header that replaces the finalized block (height 10 here; BCHN: tip - DEFAULT_MAXREORGDEPTH - 1): refused, not stored
    auto const below = replacement(tip - 11);
    auto const res = fixture.organizer().add_headers(one(below));
    CHECK(res.error == error::finalized_header_violation);
    CHECK_FALSE(stored(fixture, below));
}
