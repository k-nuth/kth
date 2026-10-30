// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <vector>

#include <kth/blockchain.hpp>
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
#include <kth/domain/chain/block_ticks.hpp>
#endif

using namespace kth;
using namespace kth::blockchain;
using kth::database::header_index;

namespace {

constexpr int32_t max_reorg_depth = 10;
constexpr int64_t delay = 7200;          // finalization delay (2h), BCHN default
constexpr int64_t now = 1'000'000;       // fixed wall-clock for determinism

hash_digest make_hash(uint32_t id) {
    hash_digest h{};
    h[0] = uint8_t(id);
    h[1] = uint8_t(id >> 8);
    h[2] = uint8_t(id >> 16);
    h[3] = uint8_t(id >> 24);
    return h;
}

domain::chain::header make_header(hash_digest const& prev, uint32_t id) {
    return domain::chain::header{1, prev, make_hash(id + 100000), id * 600, 0x1d00ffff, id};
}

// Build a linear chain of `length` headers off `first_prev`, all with reception
// time `received`. Returns the per-height index_t vector (index 0 = first block).
std::vector<header_index::index_t> build_chain(
    header_index& index, size_t length, hash_digest first_prev, uint32_t id_base, uint32_t received) {
    std::vector<header_index::index_t> idxs;
    hash_digest prev = first_prev;
    for (uint32_t i = 0; i < length; ++i) {
        auto hdr = make_header(prev, id_base + i);
        auto hash = kth::domain::chain::hash(hdr);
        auto r = index.add(hash, hdr);
        REQUIRE(r.inserted);
        index.set_received_time(r.index, received);
        idxs.push_back(r.index);
        prev = hash;
    }
    return idxs;
}

} // namespace

// =============================================================================
// finalization — depth + time rules (mirrors BCHN FindBlockToFinalize)
// =============================================================================

TEST_CASE("finalization advances to tip minus max_reorg_depth", "[finalization]") {
    header_index index;
    auto chain = build_chain(index, 20, null_hash, 0, /*received=*/0);  // 0 = from disk
    finalization f(index, max_reorg_depth, delay, /*startup=*/0);

    f.maybe_advance(chain.back(), /*block_valid_height=*/19, now);

    // Disk-loaded headers (received 0) are immediately eligible, so the finalized
    // block is exactly the depth candidate: 19 - 10 = 9.
    REQUIRE(f.finalized_height() == 9);
    REQUIRE(f.finalized() == chain[9]);
}

TEST_CASE("finalization respects the header-age (time) rule", "[finalization]") {
    header_index index;
    // Every header too recent to finalize (received `now`), except height 3.
    auto chain = build_chain(index, 20, null_hash, 0, /*received=*/uint32_t(now));
    index.set_received_time(chain[3], uint32_t(now - (delay + 800)));  // old enough
    finalization f(index, max_reorg_depth, delay, /*startup=*/0);

    f.maybe_advance(chain.back(), 19, now);

    // Depth candidate is height 9, but 9..4 are too recent; the walk descends to
    // the first time-eligible block, height 3.
    REQUIRE(f.finalized_height() == 3);
}

TEST_CASE("finalization does nothing during the startup window", "[finalization]") {
    header_index index;
    auto chain = build_chain(index, 20, null_hash, 0, 0);
    finalization f(index, max_reorg_depth, delay, /*startup=*/now);  // just started

    f.maybe_advance(chain.back(), 19, now);   // now < startup + delay

    REQUIRE(f.finalized() == finalization::null_index);
    REQUIRE(f.finalized_height() == -1);
}

TEST_CASE("finalization is disabled when max_reorg_depth < 0", "[finalization]") {
    header_index index;
    auto chain = build_chain(index, 20, null_hash, 0, 0);
    finalization f(index, /*max_reorg_depth=*/-1, delay, 0);

    f.maybe_advance(chain.back(), 19, now);

    REQUIRE(f.finalized() == finalization::null_index);
}

TEST_CASE("finalization pointer is monotonic", "[finalization]") {
    header_index index;
    auto chain = build_chain(index, 30, null_hash, 0, 0);
    finalization f(index, max_reorg_depth, delay, 0);

    f.maybe_advance(chain.back(), 25, now);
    REQUIRE(f.finalized_height() == 15);

    // A lower validated height must NOT move the pointer backward.
    f.maybe_advance(chain.back(), 18, now);
    REQUIRE(f.finalized_height() == 15);

    // A higher validated height advances it.
    f.maybe_advance(chain.back(), 29, now);
    REQUIRE(f.finalized_height() == 19);
}

// =============================================================================
// finalization — queries used by reorg admission
// =============================================================================

TEST_CASE("finalization is_finalized / descends_from_finalized", "[finalization]") {
    header_index index;
    auto chain = build_chain(index, 20, null_hash, 0, 0);
    finalization f(index, max_reorg_depth, delay, 0);
    f.maybe_advance(chain.back(), 19, now);
    REQUIRE(f.finalized_height() == 9);

    // is_finalized: at/below the finalization point on its chain.
    REQUIRE(f.is_finalized(chain[9]));      // the finalized block itself
    REQUIRE(f.is_finalized(chain[5]));      // an ancestor
    REQUIRE_FALSE(f.is_finalized(chain[12])); // above the finalized point

    // descends_from_finalized: at/above the finalization point on its chain.
    REQUIRE(f.descends_from_finalized(chain[15]));
    REQUIRE(f.descends_from_finalized(chain[9]));
    REQUIRE_FALSE(f.descends_from_finalized(chain[5]));  // below the finalized point
}

TEST_CASE("finalization rejects a branch forking below the finalized block", "[finalization]") {
    header_index index;
    auto chain = build_chain(index, 20, null_hash, 0, 0);
    finalization f(index, max_reorg_depth, delay, 0);
    f.maybe_advance(chain.back(), 19, now);
    REQUIRE(f.finalized_height() == 9);

    // Side branch off height 3 (below the finalized height 9). Its headers do not
    // descend from the finalized block, so header admission must reject them.
    auto branch = build_chain(index, 12, index.get_hash(chain[3]), /*id_base=*/500, 0);
    REQUIRE_FALSE(f.descends_from_finalized(branch.back()));

    // A branch off height 12 (above the finalized point) is admissible.
    auto ok_branch = build_chain(index, 3, index.get_hash(chain[12]), /*id_base=*/700, 0);
    REQUIRE(f.descends_from_finalized(ok_branch.back()));
}

TEST_CASE("finalization retreat_to moves the pointer back on disconnect", "[finalization]") {
    header_index index;
    auto chain = build_chain(index, 20, null_hash, 0, 0);
    finalization f(index, max_reorg_depth, delay, 0);
    f.maybe_advance(chain.back(), 19, now);
    REQUIRE(f.finalized_height() == 9);

    f.retreat_to(chain[5]);
    REQUIRE(f.finalized_height() == 5);

    f.retreat_to(finalization::null_index);
    REQUIRE(f.finalized() == finalization::null_index);
}

#if defined(KTH_CURRENCY_BCH)
namespace {

// The original API has no cadence context. Keep the same behavioral control
// runnable on that base: there it exercises the old block-count implementation;
// once the schedule API exists it supplies the identical chain's tick schedule.
// Both variants assert the finalized block, never skip the observation.
template <typename Advance>
void check_short_interval_finalization(Advance advance) {
    header_index index(200);
    std::vector<header_index::index_t> chain;
    hash_digest previous = null_hash;
    for (uint32_t height = 0; height < 122; ++height) {
        auto const timestamp = height <= 20 ? height * 600 : 20 * 600 + (height - 20) * 60;
        domain::chain::header header{1, previous, make_hash(height + 100000), timestamp, 0x1d00ffff, height};
        previous = domain::chain::hash(header);
        auto result = index.add(previous, header);
        REQUIRE(result.inserted);
        index.set_received_time(result.index, 0);
        chain.push_back(result.index);
    }
    finalization f(index, max_reorg_depth, delay, 0);

    // Block 20 is the final 600-tick block; 21..119 represent 99 * 60
    // ticks on top of it. It is still too shallow to finalize block 20.
    advance(f, chain[119], 119, now);
    REQUIRE(f.finalized_height() == 19);
    REQUIRE_FALSE(f.is_finalized(chain[20]));

    // The 100th new interval reaches exactly 6000 ticks, excluding the
    // candidate block's own interval. Its child needs one more block.
    advance(f, chain[120], 120, now);
    REQUIRE(f.finalized_height() == 20);
    REQUIRE(f.finalized() == chain[20]);
    REQUIRE_FALSE(f.is_finalized(chain[21]));
    advance(f, chain[121], 121, now);
    REQUIRE(f.finalized_height() == 21);
}

} // namespace

TEST_CASE("finalization preserves six thousand ticks across the interval change", "[finalization][faster_blocks]") {
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    check_short_interval_finalization([](auto& f, auto tip, auto height, auto time) {
        f.maybe_advance(tip, height, time, domain::chain::block_ticks::activated_at(20));
    });
#else
    check_short_interval_finalization([](auto& f, auto tip, auto height, auto time) {
        f.maybe_advance(tip, height, time);
    });
#endif
}
#endif

#if defined(KTH_CURRENCY_BCH) && __has_include(<kth/domain/chain/block_ticks.hpp>)
TEST_CASE("short interval finalization keeps validated depth separate from header progress and age", "[finalization][faster_blocks]") {
    header_index index(200);
    auto chain = build_chain(index, 151, null_hash, 0, uint32_t(now));
    auto const schedule = domain::chain::block_ticks::activated_at(20);
    finalization f(index, max_reorg_depth, delay, 0);

    // Headers lead validation by 30 blocks. Only block 20 is old enough;
    // the depth is exactly 6000 ticks from validated block 120.
    index.set_received_time(chain[20], uint32_t(now - delay));
    f.maybe_advance(chain.back(), 119, now, schedule);
    CHECK(f.finalized() == finalization::null_index);
    f.maybe_advance(chain.back(), 120, now, schedule);
    REQUIRE(f.finalized() == chain[20]);

    // Even on the new cadence a restart's two-hour grace period binds.
    finalization restarted(index, max_reorg_depth, delay, now);
    restarted.maybe_advance(chain.back(), 120, now + delay - 1, schedule);
    CHECK(restarted.finalized() == finalization::null_index);
    restarted.maybe_advance(chain.back(), 120, now + delay, schedule);
    CHECK(restarted.finalized() == chain[20]);
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("header organizer finalization uses the validated branch tick schedule", "[finalization][faster_blocks][header_organizer]") {
    header_index index(200);
    hash_digest previous = null_hash;
    for (uint32_t height = 0; height < 151; ++height) {
        auto const timestamp = height <= 20 ? height * 600 : 20 * 600 + (height - 20) * 60;
        domain::chain::header header{1, previous, make_hash(height + 100000), timestamp, 0x207fffff, height};
        previous = domain::chain::hash(header);
        auto const result = index.add(previous, header);
        REQUIRE(result.inserted);
        index.set_received_time(result.index, 0);
    }
    settings configuration(domain::config::network::regtest);
    configuration.finalization_delay_seconds = 0;
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    // MTP(20) is timestamp(15), so 20 is the final old-interval block.
    configuration.cantor_activation_time = 9000;
#endif
    header_organizer organizer(index, configuration, domain::config::network::regtest);
    organizer.sync_tip();
    organizer.note_block_validated(119);
    CHECK(organizer.finalized_height() == 19);
    organizer.note_block_validated(120);
    CHECK(organizer.finalized_height() == 20);
}
#endif
