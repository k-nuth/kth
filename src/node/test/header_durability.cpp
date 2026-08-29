// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <vector>

#include <kth/blockchain/detail/block_chain_internal.hpp>
#include <kth/node/detail/header_persist_test_seam.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// The chain state comes from memory; the checkpoint is what a restart reads (#697)
// =============================================================================
//
// Two invariants, deliberately separate, and each of these cases belongs to one.
//
// RUNTIME. `populate_chain_state` reads the header from `header_index`. A chain
// state that came from `internal_db` depended on how far a durable checkpoint
// happened to have got, and header sync writes that table in bulk once, when it
// declares itself complete: a run whose chain kept growing afterwards reached a
// height whose header had never been written, could not describe the batch it had
// just connected, and could not be reopened -- a start publishes through the same
// call.
//
// RESTART. Before a batch publishes its height markers and clears the transition
// record, the headers it covers must already be durable. That is what makes a
// crash recoverable, and it stays even though the chain state no longer depends
// on it.

namespace {

// The height the durable header table actually reaches, read back through the
// same accessor the node uses. Not the marker, and not the index: the table.
std::optional<uint32_t> durable_header_height(blockchain::block_chain& chain,
                                              uint32_t search_from) {
    std::optional<uint32_t> found;
    for (uint32_t h = 0; h <= search_from; ++h) {
        if (chain.get_header(h)) {
            found = h;
            continue;
        }
        break;
    }
    return found;
}

// A trunk of `len` empty blocks connected through the node's own path.
std::vector<domain::chain::block> connect_trunk(chain_fixture& fixture, uint32_t len) {
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (len + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= len; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }

    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);
    return trunk;
}

// Extend an already-connected trunk by the heights [from, to].
void extend_trunk(chain_fixture& fixture, uint32_t from, uint32_t to) {
    // Continued from the tip's own timestamp, not from a fresh base: a second
    // base would put these blocks before the ones they extend.
    auto prev = fixture.chain().get_block_hash(from - 1);
    REQUIRE(prev);
    auto const prev_time = fixture.chain().get_timestamp(from - 1);
    REQUIRE(prev_time);

    std::vector<domain::chain::block> more;
    auto hash = *prev;
    auto stamp = *prev_time;
    for (uint32_t h = from; h <= to; ++h) {
        stamp += block_spacing;
        more.push_back(mine_block(hash, h, stamp, 0, {}, 0));
        hash = more.back().hash();
    }

    REQUIRE(fixture.organizer().add_headers(headers_of(more)).headers_added == (to - from + 1));
    persist_headers(fixture, more, from);
    connect_bodies(fixture, more, from);
}

// Arms the persistence fault and always disarms it, including when a REQUIRE
// aborts the case or an exception unwinds. Left to a plain call after the helper
// returns, a failure part way through would leak the fault into every case that
// ran afterwards in the same process.
class persistence_fault {
public:
    explicit persistence_fault(uint32_t at_or_above) {
        node::sync::detail::fail_header_persistence_at_or_above(at_or_above);
    }
    ~persistence_fault() { node::sync::detail::clear_header_persistence_fault(); }

    persistence_fault(persistence_fault const&) = delete;
    persistence_fault& operator=(persistence_fault const&) = delete;
};

} // namespace

// -----------------------------------------------------------------------------
// Which source answers, and when
// -----------------------------------------------------------------------------

TEST_CASE("header durability: startup hydrates from the durable table",
          "[node][headers][hydration]") {
    chain_fixture fixture("hdr_hydrate");
    REQUIRE(fixture.created());

    // start() publishes a chain view before the organizer materialises the active
    // chain, so `active_at()` answers null for every height. The only reason that
    // start succeeds is the durable table -- which is what hydration is for, and
    // the one place it is allowed to be read.
    REQUIRE(fixture.start());

    // And the phase really was hydration while that happened: nothing else in
    // this suite would distinguish "the fallback ran" from "the index answered".
    CHECK(fixture.chain().get_header(0).has_value());
}

TEST_CASE("header durability: a running node answers from the index",
          "[node][headers][hydration]") {
    chain_fixture fixture("hdr_runtime");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // The node is up: the index is materialised and hydration is over.
    blockchain::detail::block_chain_internal::end_hydration(chain);
    REQUIRE_FALSE(chain.hydrating());

    connect_trunk(fixture, 3);

    // The chain state is published from the index for every connected height.
    // Under the defect this went to the durable table instead.
    CHECK_FALSE(chain.publish_chain_view(3));
}

TEST_CASE("header durability: a running node does not fall back to the table",
          "[node][headers][hydration]") {
    chain_fixture fixture("hdr_no_fallback");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const trunk = connect_trunk(fixture, 2);

    // A header written to the DURABLE TABLE ONLY, never given to the organizer,
    // so the index has nothing at that height and the checkpoint has everything.
    // That asymmetry is the whole point: a height absent from both would fail
    // either way and would prove nothing about which source was asked.
    auto const base_time = uint32_t(zulu_time()) - 20 * block_spacing;
    auto const orphan = mine_block(trunk.back().hash(), 3, base_time, 0, {}, 0);
    std::vector<domain::chain::block> const table_only{orphan};
    persist_headers(fixture, table_only, 3);
    REQUIRE(chain.get_header(3).has_value());

    // Still hydrating, and from this state the checkpoint answers: the call
    // succeeds even though the index has nothing at that height.
    REQUIRE(chain.hydrating());
    CHECK_FALSE(chain.publish_chain_view(3));

    // The phase ends -- once, and in the one direction it can go.
    blockchain::detail::block_chain_internal::end_hydration(chain);
    REQUIRE_FALSE(chain.hydrating());

    // Same state, same call, opposite answer. The index is the source of truth
    // now and it does not have height 3, so this must fail; under a fallback the
    // checkpoint would answer, which is the dependency the change removes. That
    // the two assertions differ with nothing between them but the phase is what
    // makes this about the phase and not about the entry.
    CHECK(chain.publish_chain_view(3));
}

// -----------------------------------------------------------------------------
// The durable barrier
// -----------------------------------------------------------------------------

TEST_CASE("header durability: a connected batch leaves its headers durable",
          "[node][headers][barrier]") {
    chain_fixture fixture("hdr_barrier");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 5);

    // Both markers moved...
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == 5u);

    // ...and the durable table reaches at least as far. This is the invariant a
    // restart depends on: the markers may never name a height the header table
    // cannot describe, which is the state D0 ended in (966107 against 966112).
    auto const durable = durable_header_height(chain, 10);
    REQUIRE(durable);
    CHECK(*durable >= *built);
}

TEST_CASE("header durability: the persist refuses when the chain moved under it",
          "[node][headers][barrier]") {
    chain_fixture fixture("hdr_barrier_refuses");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 3);
    auto const durable_before = durable_header_height(chain, 20);
    REQUIRE(durable_before);

    // Headers the index accepted and that are then taken off the active chain,
    // which is a condition persist_active_headers really guards: the chain moving
    // under a persist. The barrier is only as good as this refusal, so this is
    // what it is worth asserting.
    auto prev = chain.get_block_hash(3);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(3);
    REQUIRE(prev_time);

    std::vector<domain::chain::block> more;
    auto hash = *prev;
    auto stamp = *prev_time;
    for (uint32_t h = 4; h <= 5; ++h) {
        stamp += block_spacing;
        more.push_back(mine_block(hash, h, stamp, 0, {}, 0));
        hash = more.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(more)).headers_added == 2u);
    fixture.organizer().index().active_truncate(3);

    CHECK_FALSE(ensure_headers_persisted(chain, fixture.organizer().index(), 5));

    // And it wrote nothing on the way to refusing: a partial range would be the
    // half-written state the gap-only cursor exists to make impossible.
    auto const durable_after = durable_header_height(chain, 20);
    REQUIRE(durable_after);
    CHECK(*durable_after == *durable_before);

    // NOT asserted here: that utxo_build_task aborts before mutating when this
    // refusal reaches it. That path cannot be staged from outside -- taking the
    // heights off the active chain also stops the bodies being stored, so no
    // batch is ever formed and there is nothing for the barrier to refuse. An
    // earlier version of this case claimed it anyway by connecting a trunk and
    // checking nothing had moved, which is true with or without a barrier and
    // passed with the barrier deleted.
    //
    // What does pin the barrier is the index-only case below: with it the durable
    // table catches up as the batch connects, and without it the table stays
    // behind while both markers advance -- the D0 split.
}

// -----------------------------------------------------------------------------
// Continuous persistence, and why there is no debt to drain
// -----------------------------------------------------------------------------

TEST_CASE("header durability: every batch persists its own headers, not just the first",
          "[node][headers][barrier]") {
    chain_fixture fixture("hdr_continuous");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // Three separate connect rounds, the way a run that keeps receiving blocks
    // reaches them: the bulk catch-up covers a range frozen when header sync
    // ended, and everything after that arrives later. Each round has to leave its
    // own headers durable -- that the first one did says nothing about the rest,
    // and it was exactly the rest that was missing in the run this comes from.
    connect_trunk(fixture, 2);
    auto const after_first = durable_header_height(chain, 10);
    REQUIRE(after_first);
    CHECK(*after_first >= 2u);

    extend_trunk(fixture, 3, 4);
    auto const after_second = durable_header_height(chain, 10);
    REQUIRE(after_second);
    CHECK(*after_second >= 4u);

    extend_trunk(fixture, 5, 6);
    auto const after_third = durable_header_height(chain, 10);
    REQUIRE(after_third);
    CHECK(*after_third >= 6u);

    // And the markers never got ahead of it at any point.
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built <= *after_third);
}

TEST_CASE("header durability: the header marker moves with the headers, not after them",
          "[node][headers][barrier]") {
    chain_fixture fixture("hdr_marker_atomic");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 4);

    // This is why nothing is owed at shutdown. `last_header_height` is written in
    // the same transaction as the headers it describes, so it cannot name a
    // height the table does not hold -- there is no window in which a drain would
    // have something to flush. A restart interrupted anywhere finds the marker
    // and the table agreeing, and re-fetches whatever is above them.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    auto const durable = durable_header_height(chain, 10);
    REQUIRE(durable);
    CHECK(heights->header <= *durable);
}

// -----------------------------------------------------------------------------
// The state the run ended in
// -----------------------------------------------------------------------------

TEST_CASE("header durability: the markers never outrun the header table",
          "[node][headers][negation]") {
    chain_fixture fixture("hdr_h_plus_5");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 5);

    // The shape the mainnet run ended in: the header table at H while both height
    // markers said H+5. Consistent with each other, describing a block the table
    // could not produce, and therefore unopenable -- a start reconciles to the
    // marker and publishes a chain state for it.
    //
    // With the barrier before the first mutation that state cannot be produced:
    // the batch makes its headers durable or it does not run. So what is asserted
    // is the invariant itself, at the point the run violated it.
    auto const durable = durable_header_height(chain, 20);
    REQUIRE(durable);

    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);

    CHECK(heights->block <= *durable);
    CHECK(*built <= *durable);

    // And the state it leaves is one a start accepts, which is the half the run
    // did not have: same datadir, reopened.
    REQUIRE(fixture.restart());
    auto const reopened = fixture.chain().get_utxo_built_height();
    REQUIRE(reopened);
    CHECK(*reopened == *built);
}

TEST_CASE("header durability: a batch whose headers are only in the index persists them first",
          "[node][headers][negation]") {
    chain_fixture fixture("hdr_index_only");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 2);

    // The shape the run hit: headers in the index and NOT in the durable table.
    // Every other case here persists them first, which is what a bulk catch-up
    // does for the range it froze -- and is exactly why those cases cannot see
    // the barrier work. This one skips that step, the way a chain that keeps
    // growing after header sync ended does.
    auto prev = chain.get_block_hash(2);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(2);
    REQUIRE(prev_time);

    std::vector<domain::chain::block> more;
    auto hash = *prev;
    auto stamp = *prev_time;
    for (uint32_t h = 3; h <= 5; ++h) {
        stamp += block_spacing;
        more.push_back(mine_block(hash, h, stamp, 0, {}, 0));
        hash = more.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(more)).headers_added == 3u);

    // Deliberately NOT persisted here.
    auto const before = durable_header_height(chain, 20);
    REQUIRE(before);
    REQUIRE(*before == 2u);

    connect_bodies(fixture, more, 3);

    // The batch made them durable on its way in. Without the barrier it would
    // have mutated UTXO-Z and moved both markers to 5 while the table still said
    // 2 -- the H / H+5 split the mainnet run ended in, consistent and unopenable.
    auto const after = durable_header_height(chain, 20);
    REQUIRE(after);
    CHECK(*after >= 5u);

    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built <= *after);

    // And it reopens, which the run's datadir does not.
    REQUIRE(fixture.restart());
}

// -----------------------------------------------------------------------------
// Headers with no block behind them
// -----------------------------------------------------------------------------
//
// The per-batch barrier is driven by BODIES: it makes durable what a batch is
// about to connect. Headers can arrive for a long stretch with nothing behind
// them -- the whole distance between the connected tip and the header tip -- and
// no batch would ever ask for those. They have to be persisted on their own.

TEST_CASE("header durability: a header with no block behind it is persisted anyway",
          "[node][headers][drain]") {
    chain_fixture fixture("hdr_no_body");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 2);

    // A header the index accepts and whose block never arrives. No batch is
    // created for it, so nothing that depends on bodies will ever make it
    // durable.
    auto prev = chain.get_block_hash(2);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(2);
    REQUIRE(prev_time);

    std::vector<domain::chain::block> const orphan{
        mine_block(*prev, 3, *prev_time + block_spacing, 0, {}, 0)};
    REQUIRE(fixture.organizer().add_headers(headers_of(orphan)).headers_added == 1u);

    // The index has it; the table does not yet. Without this the case could pass
    // on a header that was never accepted at all.
    REQUIRE(fixture.organizer().index().active_tip_height() == 3);
    auto const before = durable_header_height(chain, 20);
    REQUIRE(before);
    REQUIRE(*before == 2u);

    // The drain the sync path performs when the header tip moves, and again on a
    // clean stop.
    REQUIRE(ensure_headers_persisted(chain, fixture.organizer().index(), 3));

    auto const after = durable_header_height(chain, 20);
    REQUIRE(after);
    CHECK(*after >= 3u);

    // And it survives the restart: the next run does not have to fetch it again.
    REQUIRE(fixture.restart());
    auto const reopened = durable_header_height(fixture.chain(), 20);
    REQUIRE(reopened);
    CHECK(*reopened >= 3u);
}

TEST_CASE("header durability: headers arriving alongside batches lose and duplicate nothing",
          "[node][headers][drain]") {
    chain_fixture fixture("hdr_interleaved");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 2);

    // Headers running ahead of bodies, the way they do during sync: the index
    // reaches 8 while only 2 blocks are connected.
    auto prev = chain.get_block_hash(2);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(2);
    REQUIRE(prev_time);

    std::vector<domain::chain::block> ahead;
    auto hash = *prev;
    auto stamp = *prev_time;
    for (uint32_t h = 3; h <= 8; ++h) {
        stamp += block_spacing;
        ahead.push_back(mine_block(hash, h, stamp, 0, {}, 0));
        hash = ahead.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(ahead)).headers_added == 6u);

    // Interleaved: the header-driven drain advances while batches connect behind
    // it, and both go through the one entry point. Asking for a range that is
    // already durable must be a cursor read, not a second write -- a duplicate
    // would be the overlapping writer this design exists to prevent.
    REQUIRE(ensure_headers_persisted(chain, fixture.organizer().index(), 5));
    connect_bodies(fixture, {ahead.begin(), ahead.begin() + 2}, 3);
    REQUIRE(ensure_headers_persisted(chain, fixture.organizer().index(), 8));
    connect_bodies(fixture, {ahead.begin() + 2, ahead.begin() + 4}, 5);

    // Every height from 1 to 8 is there exactly once, and the walk stops nowhere
    // in between: a lost range would show as a gap, and the loop that finds the
    // durable height would stop at it.
    auto const durable = durable_header_height(chain, 20);
    REQUIRE(durable);
    CHECK(*durable >= 8u);

    // Asking again for what is already durable changes nothing.
    REQUIRE(ensure_headers_persisted(chain, fixture.organizer().index(), 8));
    auto const unchanged = durable_header_height(chain, 20);
    REQUIRE(unchanged);
    CHECK(*unchanged == *durable);

    // The markers still describe only what was connected, never the headers that
    // ran ahead of them.
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built <= *durable);

    REQUIRE(fixture.restart());
}

TEST_CASE("header durability: nothing the index accepted is left only in memory",
          "[node][headers][drain]") {
    chain_fixture fixture("hdr_drain_final");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 2);

    // Headers running ahead, then more arriving after a first drain -- the shape
    // of a stop that overlaps a producer: read the tip, persist to it, and have
    // one more arrive before the channels go down.
    auto prev = chain.get_block_hash(2);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(2);
    REQUIRE(prev_time);

    std::vector<domain::chain::block> first;
    auto hash = *prev;
    auto stamp = *prev_time;
    for (uint32_t h = 3; h <= 4; ++h) {
        stamp += block_spacing;
        first.push_back(mine_block(hash, h, stamp, 0, {}, 0));
        hash = first.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(first)).headers_added == 2u);

    // A drain that reads the tip as it stands now.
    REQUIRE(ensure_headers_persisted(chain, fixture.organizer().index(), 4));

    // And then one more arrives, exactly the way a header in flight does while a
    // stop is under way.
    stamp += block_spacing;
    std::vector<domain::chain::block> const late{mine_block(hash, 5, stamp, 0, {}, 0)};
    REQUIRE(fixture.organizer().add_headers(headers_of(late)).headers_added == 1u);

    auto const tip = fixture.organizer().index().active_tip_height();
    REQUIRE(tip == 5);

    // The first drain did not cover it, which is the state the race leaves and
    // the reason the real drain runs AFTER the task join rather than before the
    // channels close: a tip read while producers are alive can move under it.
    auto const between = durable_header_height(chain, 20);
    REQUIRE(between);
    CHECK(*between == 4u);

    // The final drain, at the point where nothing can admit any more.
    REQUIRE(ensure_headers_persisted(chain, fixture.organizer().index(), uint32_t(tip)));

    // The invariant a clean stop has to leave: everything the index accepted is
    // on disk. Anything the index holds and the table does not is a header that
    // was accepted in memory only.
    auto const durable = durable_header_height(chain, 20);
    REQUIRE(durable);
    CHECK(int32_t(*durable) >= tip);

    // And it comes back that way.
    REQUIRE(fixture.restart());
    auto const reopened = durable_header_height(fixture.chain(), 20);
    REQUIRE(reopened);
    CHECK(int32_t(*reopened) >= tip);
}

TEST_CASE("header durability: a batch whose barrier refuses mutates nothing at all",
          "[node][headers][barrier]") {
    chain_fixture fixture("hdr_barrier_blocks");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 3);

    // Everything the batch must leave untouched, read before it runs.
    auto const built_before = chain.get_utxo_built_height();
    REQUIRE(built_before);
    auto const heights_before = chain.get_last_heights();
    REQUIRE(heights_before);
    auto const utxos_before = chain.utxo_count();
    REQUIRE(utxos_before);
    REQUIRE(*utxos_before > 0u);
    auto const durable_before = durable_header_height(chain, 20);
    REQUIRE(durable_before);

    // A real batch: headers in the index, bodies about to be stored, so the
    // build task forms it and reaches the barrier. Nothing about this block range
    // is unusual -- what is staged is the persistence refusing when the barrier
    // asks, which is the only moment that matters and the one no external
    // arrangement can reach.
    auto prev = chain.get_block_hash(3);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(3);
    REQUIRE(prev_time);

    std::vector<domain::chain::block> more;
    auto hash = *prev;
    auto stamp = *prev_time;
    for (uint32_t h = 4; h <= 5; ++h) {
        stamp += block_spacing;
        more.push_back(mine_block(hash, h, stamp, 0, {}, 0));
        hash = more.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(more)).headers_added == 2u);

    {
    persistence_fault const fault(4);

    // The build reports the barrier by name. run_connect_tasks_expect_fatal fails
    // the case if it stopped for anything else, so this is not "it stopped".
    run_connect_tasks_expect_fatal(fixture, more, 4,
        "a batch's headers could not be persisted before it mutated the stores");

    // The first mutation never happened. The count says so for this range, which
    // is purely additive -- but a count alone is not proof in general, because a
    // delta can preserve cardinality. So the specific outpoint the refused batch
    // would have created is asked for by name, and must not be there.
    auto const utxos_after = chain.utxo_count();
    REQUIRE(utxos_after);
    CHECK(*utxos_after == *utxos_before);

    auto const& refused_coinbase = more.front().transactions().front();
    auto const refused_txid = refused_coinbase.hash();
    auto const refused_key = utxoz::make_outpoint(
        std::span<uint8_t const, 32>{refused_txid.data(), refused_txid.size()}, 0);
    CHECK_FALSE(chain.find_utxo_raw(refused_key, 5).has_value());

    // No marker advanced...
    auto const built_after = chain.get_utxo_built_height();
    REQUIRE(built_after);
    CHECK(*built_after == *built_before);

    auto const heights_after = chain.get_last_heights();
    REQUIRE(heights_after);
    CHECK(heights_after->block == heights_before->block);

    // ...no transition was opened, so nothing published a chain state either: the
    // record is what a batch writes before it mutates, and it is clean.
    auto const check = chain.read_transition_record();
    CHECK(check.status == database::transition_status::clean);

    // And the durable table did not move, which is what the refusal said.
    auto const durable_after = durable_header_height(chain, 20);
    REQUIRE(durable_after);
    CHECK(*durable_after == *durable_before);

    }   // the fault is disarmed here, before the restart

    // The datadir is still openable, which is the whole point: a refused batch
    // leaves a node that can come back, unlike the state this fix is about.
    // Restarted with persistence working, so what is measured is the state the
    // refused batch left and not the injection still being in force.
    REQUIRE(fixture.restart());
}

// -----------------------------------------------------------------------------
// The exact shape D0 died in
// -----------------------------------------------------------------------------

TEST_CASE("header durability: a chain state builds for a height the table does not hold",
          "[node][headers][d0]") {
    chain_fixture fixture("hdr_d0_shape");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 2);

    // Headers the index has and the durable table does not -- deliberately not
    // persisted. This is the state the mainnet run reached: the chain kept
    // growing after header sync declared itself complete, the index took the new
    // headers, and the table stopped where the bulk catch-up had left it.
    auto prev = chain.get_block_hash(2);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(2);
    REQUIRE(prev_time);

    std::vector<domain::chain::block> more;
    auto hash = *prev;
    auto stamp = *prev_time;
    for (uint32_t h = 3; h <= 4; ++h) {
        stamp += block_spacing;
        more.push_back(mine_block(hash, h, stamp, 0, {}, 0));
        hash = more.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(more)).headers_added == 2u);

    // The two halves of the D0 state, asserted rather than assumed: the index has
    // height 4, and internal_db does not.
    REQUIRE(fixture.organizer().index().active_at(4) != blockchain::header_index::null_index);
    REQUIRE_FALSE(chain.get_header(4).has_value());
    REQUIRE_FALSE(chain.get_header_and_abla_state(4).has_value());

    // And the node is running, so the checkpoint is not consulted for the header.
    blockchain::detail::block_chain_internal::end_hydration(chain);
    REQUIRE_FALSE(chain.hydrating());

    // This is the call that ended the run: publish_chain_view -> populate ->
    // a header lookup that found nothing, pool_state_failed, and a node that
    // could not be restarted because a start publishes the same way.
    //
    // It has to succeed. The header comes from the index, and the ABLA state
    // that is still read from the table is READ OPTIONALLY: a missing row leaves
    // it zero, which is what every row in that table holds anyway, and the
    // populate below already treats zero as "use the static maximum".
    CHECK_FALSE(chain.publish_chain_view(4));

    // And it is STILL absent. Without this the case could pass because something
    // on the way wrote the row -- a publish that succeeded by persisting the
    // header first is not a publish that worked from memory, and it is the
    // second that is being claimed.
    CHECK_FALSE(chain.get_header(4).has_value());
    CHECK_FALSE(chain.get_header_and_abla_state(4).has_value());
}

TEST_CASE("header durability: the same height, persisted, survives a restart",
          "[node][headers][d0]") {
    chain_fixture fixture("hdr_d0_restart");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 2);

    // The recovery half, kept apart from the runtime one above on purpose:
    // publishing from memory and coming back from disk are different claims, and
    // a case that did both would not say which of them it was testing.
    auto prev = chain.get_block_hash(2);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(2);
    REQUIRE(prev_time);

    std::vector<domain::chain::block> more;
    auto hash = *prev;
    auto stamp = *prev_time;
    for (uint32_t h = 3; h <= 4; ++h) {
        stamp += block_spacing;
        more.push_back(mine_block(hash, h, stamp, 0, {}, 0));
        hash = more.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(more)).headers_added == 2u);
    REQUIRE_FALSE(chain.get_header(4).has_value());

    REQUIRE(ensure_headers_persisted(chain, fixture.organizer().index(), 4));
    REQUIRE(chain.get_header(4).has_value());

    REQUIRE(fixture.restart());
    CHECK(fixture.chain().get_header(4).has_value());
}

// -----------------------------------------------------------------------------
// The cursor across processes
// -----------------------------------------------------------------------------

TEST_CASE("header durability: a new chain over an existing datadir does not rewrite the table",
          "[node][headers][cursor]") {
    chain_fixture fixture("hdr_cursor_seed");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    connect_trunk(fixture, 4);
    REQUIRE(fixture.chain().get_header(4).has_value());

    // A NEW chain over the same datadir, which is what every restart is. The
    // cursor is per-chain, so a fresh one starts at zero unless the start seeds
    // it -- and then the first request rewrites every header from 1 to the tip.
    // Idempotent, and on a mainnet datadir a million rows of it.
    REQUIRE(fixture.restart());
    auto& chain = fixture.chain();

    // Seeded from the durable marker, not from zero.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    REQUIRE(heights->header == 4u);
    CHECK(blockchain::detail::block_chain_internal::persisted_through(chain) == 4u);

    // So a request for a range already durable writes nothing at all...
    REQUIRE(ensure_headers_persisted(chain, fixture.organizer().index(), 4));
    CHECK(blockchain::detail::block_chain_internal::persisted_through(chain) == 4u);

    // ...and one for the next height writes only that height. The cursor is what
    // says so: it moves from 4 to 5, not from 0 to 5.
    auto prev = chain.get_block_hash(4);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(4);
    REQUIRE(prev_time);
    std::vector<domain::chain::block> const next{
        mine_block(*prev, 5, *prev_time + block_spacing, 0, {}, 0)};
    REQUIRE(fixture.organizer().add_headers(headers_of(next)).headers_added == 1u);

    REQUIRE(ensure_headers_persisted(chain, fixture.organizer().index(), 5));
    CHECK(blockchain::detail::block_chain_internal::persisted_through(chain) == 5u);
    CHECK(chain.get_header(5).has_value());
}

// -----------------------------------------------------------------------------
// An absent ABLA row and a failed ABLA read are not the same thing
// -----------------------------------------------------------------------------

TEST_CASE("header durability: an absent ABLA row does not stop the chain state",
          "[node][headers][abla]") {
    chain_fixture fixture("hdr_abla_absent");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 2);

    auto prev = chain.get_block_hash(2);
    REQUIRE(prev);
    auto const prev_time = chain.get_timestamp(2);
    REQUIRE(prev_time);
    std::vector<domain::chain::block> const ahead{
        mine_block(*prev, 3, *prev_time + block_spacing, 0, {}, 0)};
    REQUIRE(fixture.organizer().add_headers(headers_of(ahead)).headers_added == 1u);

    // The row is genuinely missing, and the lookup says so with key_not_found --
    // the durable table lagging behind the index, which is the whole of #697.
    auto const missing = chain.get_header_and_abla_state(3);
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error() == database::result_code::key_not_found);

    blockchain::detail::block_chain_internal::end_hydration(chain);

    // It must not stop the publish. Zero is what every row in that table holds
    // anyway, and the populate reads zero as "use the static maximum".
    CHECK_FALSE(chain.publish_chain_view(3));
}

TEST_CASE("header durability: a failed ABLA read stops the chain state",
          "[node][headers][abla]") {
    chain_fixture fixture("hdr_abla_failed");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    connect_trunk(fixture, 2);
    blockchain::detail::block_chain_internal::end_hydration(chain);

    // The same height that publishes cleanly a line earlier...
    REQUIRE_FALSE(chain.publish_chain_view(2));

    // ...stops once the lookup FAILS rather than reporting an absent row. A
    // store fault or a malformed record is not a table lagging behind: reading
    // it as absence would answer with the static maximum, which is a consensus
    // input invented out of an error.
    blockchain::detail::fail_abla_lookup(true);
    CHECK(chain.publish_chain_view(2));
    blockchain::detail::fail_abla_lookup(false);

    // And it recovers once the store answers again, so what was measured is the
    // read failing and not something the case left behind.
    CHECK_FALSE(chain.publish_chain_view(2));
}
