// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <vector>

#include <kth/blockchain.hpp>

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
