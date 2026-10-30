// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/blockchain.hpp>
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
#include <kth/domain/chain/block_ticks.hpp>
#endif

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

// Every header carries the same difficulty, so one block is worth one unit of
// work and the thresholds below can be written in whole blocks.
domain::chain::header make_header(hash_digest const& prev, uint32_t id) {
    return domain::chain::header{1, prev, make_hash(id + 100000), id * 600, 0x1d00ffff, id};
}

// A linear chain of `length` headers off null_hash. Index == height here, which
// is what lets the tests name entries by height.
hash_digest build_chain(header_index& index, size_t length) {
    hash_digest prev = null_hash;
    for (uint32_t i = 0; i < length; ++i) {
        auto hdr = make_header(prev, i);
        prev = kth::domain::chain::hash(hdr);
        REQUIRE(index.add(prev, hdr).inserted);
    }
    return prev;
}

domain::message::header::list make_branch(hash_digest fork_prev, size_t length, uint32_t id_base) {
    domain::message::header::list branch;
    hash_digest prev = fork_prev;
    for (uint32_t i = 0; i < length; ++i) {
        auto hdr = make_header(prev, id_base + i);
        prev = kth::domain::chain::hash(hdr);
        branch.push_back(std::move(hdr));
    }
    return branch;
}

settings make_test_settings() {
    settings s(domain::config::network::mainnet);
    // Clears the list the config exposes, not the sorted copy validation reads
    // (settings(network) derives that one). Header validation therefore still
    // sees mainnet's checkpoints and skips proof-of-work for these heights,
    // which is the only reason headers with made-up hashes are accepted here.
    //
    // These tests are about the parking rule, not header validation, so that is
    // survivable — but it is a property of the shared helper, not of the headers,
    // and worth knowing before reading a passing run as "these headers are
    // valid". test/header_organizer.cpp leans on the same thing; both want the
    // regtest miner now that there is one.
    s.checkpoints.clear();
    s.finalization_delay_seconds = 0;   // finalize as soon as depth allows
    return s;
}

} // namespace

// =============================================================================
// is_deep_reorg — what counts as "more than one block rewound"
// =============================================================================

// Rewinding one block or none is not a deep reorg; two or more is. Asserted at
// compile time because callers rely on it being a pure height comparison.
static_assert( ! parking::is_deep_reorg(29, 29));   // fork is the validated tip
static_assert( ! parking::is_deep_reorg(28, 29));   // one block rewound
static_assert(parking::is_deep_reorg(27, 29));      // two blocks rewound
static_assert(parking::is_deep_reorg(0, 29));

// A branch forking *above* the validated tip rewinds nothing, however far the
// headers run ahead — the IBD case, where headers lead blocks by a wide margin.
static_assert( ! parking::is_deep_reorg(500'000, 100'000));

// =============================================================================
// required_work — BCHN FindMostWorkChain's unpark threshold
// =============================================================================

TEST_CASE("a deep fork must carry twice the work gained since the fork", "[parking]") {
    header_index index;
    (void)build_chain(index, 10);   // heights 0..9

    // uint256_t is an expression-template number, so every computed value below is
    // given an explicit type: `auto`, or an expression written inline in a CHECK,
    // captures the expression tree and reads it after its temporaries are gone.
    //
    // The entry at height 0 has no parent in the index, so it carries no work and
    // work(h) == h blocks. Only differences matter to the rule, and the offset is
    // the same for every branch.
    uint256_t const w = index.get_chain_work(1);           // one block's work
    uint256_t const nine_blocks = 9 * w;
    REQUIRE(index.get_chain_work(9) == nine_blocks);       // pins the linear-work assumption

    // Fork at height 5, validated tip at height 9: the chain gained 4 blocks since
    // the fork, so the branch needs those 4 again on top of the tip.
    uint256_t const thirteen_blocks = 13 * w;
    CHECK(parking::required_work(index, 5, 9) == thirteen_blocks);

    // Depth 10 (the deepest reorg finalization allows): 10 blocks on top of a tip
    // that is itself 10 blocks past the fork.
    header_index deep;
    (void)build_chain(deep, 21);
    uint256_t const thirty_blocks = 30 * deep.get_chain_work(1);
    CHECK(parking::required_work(deep, 10, 20) == thirty_blocks);
}

TEST_CASE("a fork one to three blocks deep is charged half a block", "[parking]") {
    header_index index;
    (void)build_chain(index, 10);
    uint256_t const w = index.get_chain_work(1);
    uint256_t const expected = 9 * w + (w >> 1);

    // All three walk back to the fork's child, so the penalty is one block wide
    // regardless of depth. A near-tip race is normal network behaviour: demanding
    // a 2x lead there would strand the node on a branch already abandoned.
    CHECK(parking::required_work(index, 8, 9) == expected);   // depth 1
    CHECK(parking::required_work(index, 7, 9) == expected);   // depth 2
    CHECK(parking::required_work(index, 6, 9) == expected);   // depth 3

    // Depth 4 crosses over to the full penalty, which is strictly harsher.
    CHECK(parking::required_work(index, 5, 9) > expected);
}

// =============================================================================
// header_organizer — promotion is gated on the threshold
// =============================================================================

TEST_CASE("a heavier branch forking deep is not promoted to a reorg candidate", "[parking][fork]") {
    header_index index;
    (void)build_chain(index, 30);                 // tip at height 29
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(29);

    // Off height 22, 8 blocks: head at height 30 with 31 blocks of work, which
    // beats the 30-block tip — but rewinds 7 validated blocks with a single block
    // of surplus. The threshold is 37; this is a branch to wait on, not follow.
    auto branch = make_branch(index.get_hash(22), 8, 200);
    auto const result = organizer.add_headers(branch);

    CHECK_FALSE(result.reorg_candidate);
    CHECK(result.error == error::stale_chain);
    CHECK(organizer.header_height() == 29);       // still on the original chain

    // Parked, not rejected: the headers are stored so the branch can keep
    // accumulating work and be reconsidered on the next batch.
    CHECK(index.size() == 38);
}

TEST_CASE("a parked branch is promoted once it clears the threshold", "[parking][fork]") {
    header_index index;
    (void)build_chain(index, 30);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(29);

    // Same fork point, but 15 blocks: head at height 37, 38 blocks of work against
    // a threshold of 37. Nothing was unparked — the branch is simply not parked on
    // this evaluation, which is how the recomputed rule expresses BCHN's unpark.
    auto branch = make_branch(index.get_hash(22), 15, 300);
    auto const result = organizer.add_headers(branch);

    CHECK(result.reorg_candidate);
    CHECK(result.reorg_fork_height == 22);
    CHECK(result.reorg_branch_head == index.find(kth::domain::chain::hash(branch.back())));
}

TEST_CASE("a branch forking above the validated tip is never parked", "[parking][fork]") {
    header_index index;
    (void)build_chain(index, 30);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    // Blocks lag headers, as during IBD. A fork at height 25 rewinds no validated
    // block, so greater work is enough on its own.
    organizer.note_block_validated(20);

    auto branch = make_branch(index.get_hash(25), 6, 400);
    auto const result = organizer.add_headers(branch);

    CHECK(result.reorg_candidate);
    CHECK(result.reorg_fork_height == 25);
}

TEST_CASE("a one-block reorg is not parked", "[parking][fork]") {
    header_index index;
    (void)build_chain(index, 30);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(29);

    // Two blocks off height 28: the ordinary case of two miners finding a block at
    // once and one branch pulling ahead. Parking this would leave the node behind
    // the network for no benefit.
    auto branch = make_branch(index.get_hash(28), 2, 500);
    auto const result = organizer.add_headers(branch);

    CHECK(result.reorg_candidate);
    CHECK(result.reorg_fork_height == 28);
}

TEST_CASE("a rewound validated height releases a branch it had parked", "[parking][fork]") {
    // What a reorg leaves behind: the switch rewinds the validated tip to the
    // fork, and the rule has to measure against the new height. Reading a height
    // left over from the branch the node just abandoned would keep charging a
    // rewind cost for validated blocks that are no longer connected.
    header_index index;
    (void)build_chain(index, 30);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    organizer.note_block_validated(29);
    REQUIRE(organizer.validated_height() == 29);

    auto branch = make_branch(index.get_hash(22), 8, 700);
    REQUIRE_FALSE(organizer.add_headers(branch).reorg_candidate);   // parked: 7 blocks rewound

    // A switch rewound the validated tip to 15. The same branch now forks above
    // it, so it rewinds nothing and greater work is enough on its own.
    organizer.note_block_validated(15);
    CHECK(organizer.validated_height() == 15);

    // Re-presenting the branch is what a peer's next announcement does; the
    // headers are already stored, so this re-runs the decision on them.
    auto const result = organizer.add_headers(branch);
    CHECK(result.reorg_candidate);
    CHECK(result.reorg_fork_height == 22);
}

TEST_CASE("nothing is parked before the first block is validated", "[parking][fork]") {
    header_index index;
    (void)build_chain(index, 30);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    // No note_block_validated yet: at startup the node has validated nothing, so
    // there is no validated chain to measure a rewind against.
    auto branch = make_branch(index.get_hash(22), 8, 600);
    auto const result = organizer.add_headers(branch);

    CHECK(result.reorg_candidate);
    CHECK(result.reorg_fork_height == 22);
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("parking uses tick depth on the defending faster branch", "[parking][faster_blocks]") {
    header_index index(200);
    (void)build_chain(index, 101);
    auto const tip = header_index::index_t(100);
    auto const fork = header_index::index_t(80);
    uint256_t const defending_work = index.get_chain_work(tip) - index.get_chain_work(fork);
    uint256_t const expected = index.get_chain_work(tip) + defending_work / 4;

    // Preserve this scenario on the original API: without an interval schedule
    // it charges the old full-work penalty instead of the 1200-tick ramp value.
    // The assertion always observes the actual chain-selection threshold.
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    auto const required = parking::required_work(index, fork, tip,
        domain::chain::block_ticks::activated_at(0));
#else
    auto const required = parking::required_work(index, fork, tip);
#endif
    CHECK(required == expected);
    CHECK(required < index.get_chain_work(tip) + defending_work);
}
#endif

#if defined(KTH_CURRENCY_BCH) && __has_include(<kth/domain/chain/block_ticks.hpp>)
TEST_CASE("parking tick ramp has exact floor anchors and saturation", "[parking][faster_blocks]") {
    header_index index(100);
    (void)build_chain(index, 61);
    auto const schedule = domain::chain::block_ticks::activated_at(0);
    auto const tip = header_index::index_t(60);
    uint256_t const tip_work = index.get_chain_work(tip);
    struct anchor { uint32_t depth; uint64_t numerator; uint64_t denominator; };
    for (auto const& point : {anchor{2, 1, 16}, anchor{10, 1, 16}, anchor{20, 1, 4},
                              anchor{30, 9, 16}, anchor{40, 1, 1}, anchor{50, 1, 1}}) {
        auto const fork = header_index::index_t(60 - point.depth);
        uint256_t const work = tip_work - index.get_chain_work(fork);
        uint256_t const penalty = work * point.numerator / point.denominator;
        CHECK(parking::required_work(index, fork, tip, schedule) == tip_work + penalty);
    }
}

TEST_CASE("parking switches at the defending activation successor and measures a mixed span", "[parking][faster_blocks]") {
    header_index index(100);
    (void)build_chain(index, 62);
    auto const schedule = domain::chain::block_ticks::activated_at(60);

    // H_a is not a short block: retain the pre-upgrade count-keyed policy.
    CHECK(parking::required_work(index, 58, 60, schedule) == parking::required_work(index, 58, 60));

    // The first short block switches the policy. Fork59 -> tip61 spans one
    // 600-tick interval plus one 60-tick interval, not two equal intervals.
    uint256_t const work = index.get_chain_work(61) - index.get_chain_work(59);
    uint256_t const penalty = work * 660 * 660 / 5'760'000;
    CHECK(parking::required_work(index, 59, 61, schedule) == index.get_chain_work(61) + penalty);
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("header organizer unparks only after exceeding the defending tick ramp", "[parking][faster_blocks][header_organizer]") {
    header_index index(128);
    (void)build_chain(index, 61);
    auto configuration = make_test_settings();
    configuration.max_reorg_depth = -1;  // isolate parking from finalization
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    configuration.cantor_activation_time = 0;
#endif
    header_organizer organizer(index, configuration, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(60);

    // Reannounce stored branch headers so this control exercises only chain
    // selection. Header-validity and PoW tests have their own mined fixtures.
    // Defending20 * 60 ticks => penalty5 blocks; equal25 stays parked.
    auto branch = make_branch(index.get_hash(40), 25, 1000);
    for (auto const& header : branch) {
        REQUIRE(index.add(domain::chain::hash(header), header).inserted);
    }
    CHECK_FALSE(organizer.add_headers(branch).reorg_candidate);

    // The 26th work unit crosses the strict threshold. Legacy policy would
    // still demand more than40, the regression this scenario discriminates.
    auto const parent = domain::chain::hash(branch.back());
    branch.push_back(make_header(parent, 1025));
    REQUIRE(index.add(domain::chain::hash(branch.back()), branch.back()).inserted);
    auto const result = organizer.add_headers(branch);
    CHECK(result.reorg_candidate);
    CHECK(result.reorg_fork_height == 40);
}
#endif
