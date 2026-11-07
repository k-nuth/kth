// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <vector>

#include <kth/infrastructure/utility/timer.hpp>
#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/pools/header_organizer.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::blockchain;
using namespace kth::test;

// =============================================================================
// A refused block does not come back as a header
// =============================================================================
//
// When the connect path refuses a block in context, the execution layer flags
// its index entry `failed_valid` and adopts the parent as the tip. The
// organizer skips a header it already holds on the strength of having
// validated it once — and a re-sent refused header, with the parent now the
// tip, is a forward extension: without the flag being honoured, the skip
// followed by the tip publication put the refused block straight back on the
// active chain, with nothing re-checked, and the next valid block at that
// height was then a side branch.
//
// Every case does exactly what the rejection does to the index — flag, then
// adopt the parent — and then offers headers the way a peer or a submitter
// would, through add_headers.

namespace {

constexpr uint32_t spacing = 120;

domain::message::header::list headers_of(std::vector<domain::chain::block> const& blocks) {
    domain::message::header::list out;
    for (auto const& blk : blocks) {
        out.push_back(blk.header());
    }
    return out;
}

struct refused_tip {
    chain_fixture fixture;
    std::vector<domain::chain::block> trunk;    // heights 1..3, the third refused
    uint32_t base_time;

    explicit refused_tip(char const* tag)
        : fixture(tag)
        , base_time(uint32_t(zulu_time()) - 40u * spacing) {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());

        auto prev = domain::chain::block::genesis_regtest().hash();
        for (uint32_t h = 1; h <= 3; ++h) {
            trunk.push_back(mine_block(prev, h, base_time + h * spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
        REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == 3u);
        REQUIRE(index().active_tip_height() == 3);

        // Exactly the rejection's two steps, in its order.
        auto const refused = index().find(trunk[2].hash());
        REQUIRE(refused != header_index::null_index);
        auto const parent = index().active_at(2);
        REQUIRE(parent != header_index::null_index);
        index().add_status(refused, header_status::failed_valid);
        fixture.organizer().adopt_tip(parent);

        REQUIRE(index().active_tip_height() == 2);
        REQUIRE(fixture.organizer().header_height() == 2);
        REQUIRE(index().active_at(3) == header_index::null_index);
    }

    header_index& index() { return fixture.chain().headers(); }
    header_organizer& organizer() { return fixture.organizer(); }

    domain::chain::block mined_on(domain::chain::block const& parent, uint32_t height, uint32_t salt = 0) {
        return mine_block(parent.hash(), height, base_time + height * spacing, salt, {}, 0);
    }

    void check_still_stepped_back() {
        CHECK(index().active_tip_height() == 2);
        CHECK(organizer().header_height() == 2);
        CHECK(index().active_at(3) == header_index::null_index);
        CHECK(index().get_hash(organizer().tip_index()) == trunk[1].hash());
    }
};

} // namespace

TEST_CASE("header_organizer: a refused header re-sent as a tip extension does not move the tip",
          "[header_organizer][failed]") {
    refused_tip state("organizer_failed_resent");

    // Its parent is the tip, so this is a forward extension of a known header:
    // the path that took the skip and published the known entry.
    auto const result = state.organizer().add_headers(headers_of({state.trunk[2]}));

    CHECK(result.error == error::duplicate_block);
    CHECK(result.headers_added == 0u);
    CHECK_FALSE(result.reorg_candidate);
    state.check_still_stepped_back();
}

TEST_CASE("header_organizer: a new header on a refused parent is neither stored nor adopted",
          "[header_organizer][failed]") {
    refused_tip state("organizer_failed_child");
    auto const four = state.mined_on(state.trunk[2], 4);

    SECTION("offered alone, anchored on the refused block") {
        auto const result = state.organizer().add_headers(headers_of({four}));
        CHECK(result.error == error::invalid_previous_block);
        CHECK(result.headers_added == 0u);
    }

    SECTION("offered behind the refused header, as a contiguous run") {
        auto const result = state.organizer().add_headers(headers_of({state.trunk[2], four}));
        CHECK(result.error == error::duplicate_block);
        CHECK(result.headers_added == 0u);
    }

    CHECK(state.index().find(four.hash()) == header_index::null_index);
    state.check_still_stepped_back();
}

TEST_CASE("header_organizer: a known descendant of a refused block is flagged when walked onto, and refused",
          "[header_organizer][failed]") {
    // The descendant was stored BEFORE the refusal — a peer extended the block
    // while it was still the tip — so it carries no flag of its own. The first
    // batch that walks from the refused parent onto it flags it, and any batch
    // anchored on it afterwards is refused on the flag alone.
    chain_fixture fixture("organizer_failed_descendant");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& index = fixture.chain().headers();
    auto& organizer = fixture.organizer();
    auto const base_time = uint32_t(zulu_time()) - 40u * spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= 4; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(organizer.add_headers(headers_of(trunk)).headers_added == 4u);

    // Block 3 refused after 4 was already known: flag 3, adopt 2.
    auto const refused = index.find(trunk[2].hash());
    auto const descendant = index.find(trunk[3].hash());
    index.add_status(refused, header_status::failed_valid);
    organizer.adopt_tip(index.active_at(2));
    REQUIRE(index.active_tip_height() == 2);
    REQUIRE_FALSE(index.has_status(descendant, header_status::failed_child));

    // Through the refused parent: refused at the parent, tip unmoved.
    auto const through = organizer.add_headers(headers_of({trunk[2], trunk[3]}));
    CHECK(through.error == error::duplicate_block);
    CHECK(through.headers_added == 0u);
    CHECK(index.active_tip_height() == 2);
    CHECK_FALSE(index.has_status(descendant, header_status::failed_child));

    // Anchored on the descendant itself — a new header on top of it — is the
    // first time a batch stands on it. Its parent's flag is what refuses it,
    // and it is flagged in turn, so the next batch anchored on it is refused
    // on its own flag.
    auto const five = mine_block(trunk[3].hash(), 5, base_time + 5 * spacing, 0, {}, 0);
    auto const on_descendant = organizer.add_headers(headers_of({five}));
    CHECK(on_descendant.error == error::invalid_previous_block);
    CHECK(on_descendant.headers_added == 0u);
    CHECK(index.find(five.hash()) == header_index::null_index);
    CHECK(index.has_status(descendant, header_status::failed_child));
    CHECK(index.active_tip_height() == 2);

    // Anchored on the refused block, walking onto the descendant: refused at
    // the anchor, before anything is looked at.
    auto const from_refused = organizer.add_headers(headers_of({trunk[3], five}));
    CHECK(from_refused.error == error::invalid_previous_block);
    CHECK(from_refused.headers_added == 0u);
    CHECK(index.find(five.hash()) == header_index::null_index);
    CHECK(index.active_tip_height() == 2);
}

TEST_CASE("header_organizer: after a refusal, a valid block at the same height extends the tip",
          "[header_organizer][failed]") {
    // The counterpart that keeps the flag honest: the refusal is about one
    // block, not about the height, and the chain goes on from the parent.
    refused_tip state("organizer_failed_recovery");
    auto const alternative = state.mined_on(state.trunk[1], 3, /*salt*/ 7);
    REQUIRE(alternative.hash() != state.trunk[2].hash());

    auto const result = state.organizer().add_headers(headers_of({alternative}));
    CHECK_FALSE(result.error);
    CHECK(result.headers_added == 1u);
    CHECK(state.index().active_tip_height() == 3);
    CHECK(state.index().get_hash(state.index().active_at(3)) == alternative.hash());

    // And the refused one still cannot come back, now as a side branch.
    auto const again = state.organizer().add_headers(headers_of({state.trunk[2]}));
    CHECK(again.error == error::duplicate_block);
    CHECK(again.headers_added == 0u);
    CHECK(state.index().get_hash(state.index().active_at(3)) == alternative.hash());

    // Nor can anything built on it, which would otherwise be a heavier side
    // branch and a reorg candidate.
    auto const on_refused = state.mined_on(state.trunk[2], 4);
    auto const candidate = state.organizer().add_headers(headers_of({on_refused}));
    CHECK(candidate.error == error::invalid_previous_block);
    CHECK_FALSE(candidate.reorg_candidate);
    CHECK(state.index().find(on_refused.hash()) == header_index::null_index);
}

// =============================================================================
// A known header is followed only where it belongs
// =============================================================================

TEST_CASE("header_organizer: a known header that does not follow the one before it cannot move the tip",
          "[header_organizer][contiguity]") {
    // Trunk 1..5 active, a valid sibling of 3 stored as a side branch. A batch
    // that extends the tip with a new 6 and then names the sibling — known,
    // but not 6's child — used to move the running parent to the sibling and,
    // because the batch extended the tip, publish the sibling as the tip: an
    // active chain of height 3 on another branch, with no work compared.
    chain_fixture fixture("organizer_known_jump");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& index = fixture.chain().headers();
    auto& organizer = fixture.organizer();
    auto const base_time = uint32_t(zulu_time()) - 40u * spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(organizer.add_headers(headers_of(trunk)).headers_added == 5u);

    auto const sibling = mine_block(trunk[1].hash(), 3, base_time + 3 * spacing + 30, /*salt*/ 9, {}, 0);
    auto const stored = organizer.add_headers(headers_of({sibling}));
    REQUIRE(stored.error == error::stale_chain);          // a side branch, not heavier
    REQUIRE(index.find(sibling.hash()) != header_index::null_index);

    auto const six = mine_block(trunk[4].hash(), 6, base_time + 6 * spacing, 0, {}, 0);
    auto const result = organizer.add_headers(headers_of({six, sibling}));

    // The valid prefix moved the tip; the jump was refused.
    CHECK(result.error == error::invalid_previous_block);
    CHECK(result.headers_added == 1u);
    CHECK(index.active_tip_height() == 6);
    CHECK(index.get_hash(index.active_at(6)) == six.hash());
    CHECK(index.get_hash(index.active_at(3)) == trunk[2].hash());
    CHECK(index.get_hash(organizer.tip_index()) == six.hash());
}

TEST_CASE("header_organizer: a contiguous retry of known headers is still nothing to act on",
          "[header_organizer][contiguity]") {
    // Honest retries walk the chain of previous-hashes the batch carries, so
    // the contiguity rule leaves them exactly as they were: nothing added,
    // nothing moved, stale_chain.
    chain_fixture fixture("organizer_known_retry");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& index = fixture.chain().headers();
    auto& organizer = fixture.organizer();
    auto const base_time = uint32_t(zulu_time()) - 40u * spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(organizer.add_headers(headers_of(trunk)).headers_added == 5u);

    // From the tip's own parent: a batch that starts below the tip and re-walks it.
    std::vector<domain::chain::block> const retry(trunk.begin() + 2, trunk.end());
    auto const result = organizer.add_headers(headers_of(retry));
    CHECK(result.error == error::stale_chain);
    CHECK(result.headers_added == 0u);
    CHECK(index.active_tip_height() == 5);
    CHECK(index.get_hash(organizer.tip_index()) == trunk.back().hash());

    // And a known side-branch header followed by its own new child is the
    // ordinary side-branch case, stored and judged on work as before.
    auto const sibling = mine_block(trunk[1].hash(), 3, base_time + 3 * spacing + 30, 9, {}, 0);
    REQUIRE(organizer.add_headers(headers_of({sibling})).error == error::stale_chain);
    auto const child = mine_block(sibling.hash(), 4, base_time + 4 * spacing + 30, 9, {}, 0);
    auto const branch = organizer.add_headers(headers_of({sibling, child}));
    CHECK(branch.error == error::stale_chain);
    CHECK(index.find(child.hash()) != header_index::null_index);
    CHECK(index.active_tip_height() == 5);
}
