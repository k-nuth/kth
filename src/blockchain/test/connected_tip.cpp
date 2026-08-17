// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <optional>

#include <kth/infrastructure/utility/timer.hpp>

#include <kth/blockchain/utxo_builder.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::blockchain;
using namespace kth::test;

// =============================================================================
// Two markers, two meanings, and the livelock that came from confusing them (#653)
// =============================================================================
//
// `last_block_height` is the CONNECTED tip: publish_chain_view builds state at
// it and disconnect_block refuses to rewind past it. `utxo_built_height`
// describes the UTXO set. They must agree, and only the batch that connected the
// blocks may move either.
//
// A released version let the storage task write the DOWNLOADED height into the
// connected marker — and only on the way out. Two consequences, both reproduced
// below:
//
//   * during a run the connected marker never moved, so is_stale() — which read
//     it — answered "behind the network" forever. The builder drains a remainder
//     shorter than one batch only when NOT stale, so a mainnet node parked 941
//     blocks short of the tip for over an hour, with no error;
//   * a CLEAN STOP wrote a recent-looking height, so the next start drained the
//     remainder immediately. The defect masked itself: every restart repaired it
//     and left a database whose two markers disagreed by 952 blocks.
//
// Recency is answered by the validated header tip instead, which reaches the tip
// in a minute while the build takes an hour, and which does not depend on the
// progress it gates.


namespace {

// A real chain the markers can name: mined, added to the index and written to
// the by-height table, so get_header() resolves every height used below. Two
// numbers agreeing with each other prove nothing about whether either names a
// block, which is what the "no header behind it" case exists to catch.
uint32_t build_chain(chain_fixture& fixture, uint32_t len) {
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (len + 30) * 600;

    std::vector<domain::chain::block> blocks;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= len; ++h) {
        blocks.push_back(mine_block(prev, h, base_time + h * 600, 0, {}, 0));
        prev = blocks.back().hash();
    }

    domain::message::header::list msg_headers;
    domain::chain::header::list headers;
    for (auto const& blk : blocks) {
        msg_headers.push_back(blk.header());
        headers.push_back(blk.header());
    }
    REQUIRE(fixture.organizer().add_headers(msg_headers).headers_added == len);
    REQUIRE( ! fixture.chain().organize_headers_batch(headers, 1));
    return len;
}

} // namespace

// -----------------------------------------------------------------------------
// Recency: the header tip, never the connected tip
// -----------------------------------------------------------------------------

TEST_CASE("the connected-tip marker does not make a genesis-only chain fresh",
          "[connected_tip][stale]") {
    // THE DISCRIMINATING CONTROL, in the shape the mainnet run produced: headers
    // at the tip, the connected tip 952 blocks behind, and a remainder shorter
    // than one build batch.
    //
    // Answering from the connected tip is what closes the loop: stale keeps the
    // remainder unbuilt, unbuilt keeps the connected tip where it is, and the tip
    // keeps the answer stale. The header tip breaks it because nothing the
    // builder does can move it.
    chain_fixture fixture("tip_recent_headers");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // The genesis-only chain: its one header is from 2009, so the node is behind
    // by every measure. This is the baseline the next assertion is measured
    // against — without it, "not stale" below could just mean "always false".
    CHECK(chain.is_stale());

    // A connected marker far behind, exactly as the run left it. It must not make
    // the node fresh, and — the point — it must not make it stale either.
    REQUIRE(chain.set_last_block_height(0) == database::result_code::success);

    // Nothing else in this fixture advances the header chain, so the strongest
    // statement available here is the negative one: the answer does not come from
    // the connected marker. The positive half — headers current, marker behind,
    // not stale — is asserted in the node suite, where a real chain is built.
    CHECK(chain.is_stale());
}

TEST_CASE("a chain holding only genesis answers stale", "[connected_tip][stale]") {
    // Never call a node fresh on a failed read. Being wrongly considered behind
    // costs a poll; being wrongly considered current relaxes batching and lets
    // the node act as though it had caught up.
    chain_fixture fixture("tip_no_headers");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // A chain holding only genesis: whatever the marker says, the answer is
    // stale, and it is reached without consulting the connected tip at all.
    CHECK(fixture.chain().is_stale());
}

// -----------------------------------------------------------------------------
// Reconciliation of the two markers at startup
// -----------------------------------------------------------------------------

TEST_CASE("the UTXO height wins in both directions", "[connected_tip][reconcile]") {
    // min() would be wrong, and this is why. Before the fix the storage marker
    // was written only on a clean stop, so a node that crashed mid-sync has a
    // connected marker of 0 and a UTXO set describing hundreds of thousands of
    // blocks. Taking the lower would discard everything that IS connected.
    //
    // The set is the evidence: wherever they disagree, the UTXO height wins.
    chain_fixture fixture("tip_reconcile");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // Heights that name real blocks: 10 exists, and so does every height below it.
    build_chain(fixture, 10);

    SECTION("marker ahead of the UTXO — the legacy clean-stop state") {
        REQUIRE(chain.set_utxo_built_height(10) == database::result_code::success);
        REQUIRE(chain.set_last_block_height(8) == database::result_code::success);

        auto const tip = chain.reconcile_connected_tip(8);
        REQUIRE(tip);
        CHECK(*tip == 10u);   // not 8: the set reaches 10
    }

    SECTION("marker behind the UTXO — the crashed-mid-sync state") {
        REQUIRE(chain.set_utxo_built_height(10) == database::result_code::success);
        REQUIRE(chain.set_last_block_height(0) == database::result_code::success);

        auto const tip = chain.reconcile_connected_tip(0);
        REQUIRE(tip);
        CHECK(*tip == 10u);   // not 0: 10 blocks really are connected
    }

    SECTION("agreement is left alone") {
        REQUIRE(chain.set_utxo_built_height(10) == database::result_code::success);
        REQUIRE(chain.set_last_block_height(10) == database::result_code::success);

        auto const tip = chain.reconcile_connected_tip(10);
        REQUIRE(tip);
        CHECK(*tip == 10u);
    }
}

TEST_CASE("an absent UTXO marker is not a failure", "[connected_tip][reconcile]") {
    // A database that has never built one is the ordinary state of a fresh
    // datadir, and must be told apart from a marker that could not be read. The
    // first stands on the connected marker; the second refuses to start.
    chain_fixture fixture("tip_absent_marker");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // Nothing has built, so the marker is genuinely absent.
    auto const built = chain.get_utxo_built_height();
    REQUIRE_FALSE(built);
    REQUIRE(built.error() == database::result_code::key_not_found);

    auto const tip = chain.reconcile_connected_tip(0);
    REQUIRE(tip);           // absence is answered, not refused
    CHECK(*tip == 0u);

    // And with a marker that claims blocks: a legacy database can hold a large
    // downloaded height with no built marker, because create_height_properties()
    // initialises one and not the other. Nothing is connected there, whatever the
    // marker says, so standing on it would publish a tip with no UTXO behind it.
    REQUIRE(chain.set_last_block_height(963898) == database::result_code::success);
    auto const claimed = chain.reconcile_connected_tip(963898);
    REQUIRE(claimed);
    CHECK(*claimed == 0u);   // not 963898

    // Persisted, so the next start does not face the same claim again.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 0u);
}

TEST_CASE("the reconciled tip is corrected durably, before anything is published",
          "[connected_tip][reconcile]") {
    // The correction has to reach the disk, and reach it BEFORE chain state is
    // published from it. A restart must read the reconciled value directly rather
    // than repeat the reconciliation forever — and a reader arriving in between
    // would otherwise still see the stale claim.
    //
    // Read back through a second, short-lived database opened on the same
    // directory after the chain is down, which is the layer the markers live in
    // and a genuine reopen. A full restart cannot be used here: it publishes
    // chain state at the reconciled height, and this fixture holds only genesis,
    // so any height with no block behind it would fail for an unrelated reason.
    chain_fixture fixture("tip_durable");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    build_chain(fixture, 10);

    {
        auto& chain = fixture.chain();
        REQUIRE(chain.set_utxo_built_height(10) == database::result_code::success);
        REQUIRE(chain.set_last_block_height(8) == database::result_code::success);
        auto const tip = chain.reconcile_connected_tip(8);
        REQUIRE(tip);
        CHECK(*tip == 10u);
    }

    fixture.close();

    database::settings settings;
    settings.directory = fixture.dir();
    database::data_base db(settings);
    REQUIRE(db.open());
    auto const heights = db.internal_db().get_last_heights();
    auto const built = db.internal_db().get_utxo_built_height();
    REQUIRE(db.close());

    REQUIRE(heights);
    REQUIRE(built);
    // On disk as 10, so the next start finds the two markers in agreement and
    // has nothing to reconcile — the correction happened once, not on every boot.
    CHECK(heights->block == 10u);
    CHECK(*built == 10u);
    CHECK(heights->block == *built);
}

// -----------------------------------------------------------------------------
// The invariant the connect path must maintain
// -----------------------------------------------------------------------------

TEST_CASE("a published transition leaves both heights equal", "[connected_tip][publish]") {
    // After a successful publish the two markers describe the same block. The
    // connect path publishes them in ONE transaction together with clearing the
    // record, so a failure cannot expose one updated and the other not.
    chain_fixture fixture("tip_publish");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    REQUIRE(chain.publish_transition(database::transition_heights{
        .last_block_height = 42, .utxo_built_height = 42}) ==
        database::result_code::success);

    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(heights->block == 42u);
    CHECK(*built == 42u);
    CHECK(heights->block == *built);   // the invariant, stated as one claim

    // And the record is clean, which is the third thing that transaction does.
    CHECK(chain.read_transition_record().status == database::transition_status::clean);
}

TEST_CASE("nothing is published when there is nothing to publish", "[connected_tip][publish]") {
    // A publish with neither height is refused rather than committed: it would
    // clear the record on its own, declaring a transition finished without
    // saying where it finished. The markers must be left exactly as they were.
    chain_fixture fixture("tip_publish_empty");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    REQUIRE(chain.set_last_block_height(7) == database::result_code::success);
    REQUIRE(chain.set_utxo_built_height(7) == database::result_code::success);

    CHECK(chain.publish_transition(database::transition_heights{
        .last_block_height = std::nullopt, .utxo_built_height = std::nullopt}) !=
        database::result_code::success);

    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 7u);       // neither marker moved
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == 7u);
}

// -----------------------------------------------------------------------------
// The paths a fixture cannot reach, reached where the decision lives
// -----------------------------------------------------------------------------

TEST_CASE("an unestablished header tip answers stale", "[connected_tip][stale]") {
    // The three ways active_tip_timestamp() gives up — no active chain, a null
    // index from a chain truncated between the two reads, and a zero timestamp —
    // all arrive here as nullopt, and all must answer stale. Two of them are
    // races against a reorg and cannot be staged from a fixture, so the decision
    // is made where every input is reachable.
    constexpr time_t day = 24 * 60 * 60;

    CHECK(recency_is_stale(std::nullopt, day));

    // And the answer is not simply "always stale": a current timestamp is fresh
    // and an old one is not.
    auto const now = static_cast<uint32_t>(zulu_time());
    CHECK_FALSE(recency_is_stale(now, day));
    CHECK(recency_is_stale(now - uint32_t(6 * day), day));

    // A zero limit disables the question entirely, and must not be turned into
    // "stale" by the unknown case above.
    CHECK_FALSE(recency_is_stale(std::nullopt, 0));
    CHECK_FALSE(recency_is_stale(now - uint32_t(6 * day), 0));
}

TEST_CASE("an operational read failure is not an absent marker",
          "[connected_tip][reconcile]") {
    // The distinction start() fails closed on. LMDB cannot be made to fail a read
    // on demand, so the decision takes the result rather than fetching it, which
    // is what makes this input reachable at all.
    using result = std::expected<uint32_t, database::result_code>;

    constexpr bool empty_set = true;
    constexpr bool populated_set = false;

    // A value reconciles: the UTXO height wins over the marker, whichever side
    // is ahead, and the store's contents do not enter into it.
    CHECK(reconcile_tip(150, result{100}, empty_set) == std::optional<uint32_t>{100});
    CHECK(reconcile_tip(0, result{100}, populated_set) == std::optional<uint32_t>{100});

    // Absent WITH AN EMPTY SET is a database that has never built: nothing is
    // connected, whatever the marker claims.
    CHECK(reconcile_tip(150, std::unexpected(database::result_code::key_not_found), empty_set)
          == std::optional<uint32_t>{0});

    // Absent with a POPULATED set is a materialised UTXO whose height nothing
    // records. Answering zero would rebuild from genesis over it.
    CHECK_FALSE(reconcile_tip(150, std::unexpected(database::result_code::key_not_found),
                              populated_set));
    CHECK_FALSE(reconcile_tip(0, std::unexpected(database::result_code::key_not_found),
                              populated_set));

    // Anything else is a read that FAILED, and answers nothing either way.
    CHECK_FALSE(reconcile_tip(150, std::unexpected(database::result_code::other), empty_set));
    CHECK_FALSE(reconcile_tip(150, std::unexpected(database::result_code::db_corrupt), empty_set));
}

TEST_CASE("a UTXO height with no header behind it is refused", "[connected_tip][reconcile]") {
    // Reconciling is not enough: the height has to be one the node can stand on,
    // because publish_chain_view reads the header and the block at it. Two
    // numbers agreeing with each other is not evidence that either names a block.
    chain_fixture fixture("tip_no_header");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // A UTXO set claiming a height this database has no header for.
    REQUIRE(chain.set_utxo_built_height(500) == database::result_code::success);
    REQUIRE(chain.set_last_block_height(0) == database::result_code::success);

    CHECK_FALSE(chain.reconcile_connected_tip(0));   // refused, not repaired

    // And the marker was NOT moved to the unusable height on the way out.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 0u);
}

// -----------------------------------------------------------------------------
// Atomicity, against a transaction made to fail
// -----------------------------------------------------------------------------

TEST_CASE("a publish that cannot commit moves neither height and leaves the record",
          "[connected_tip][publish][atomicity]") {
    // The claim is that the two heights and the cleared record commit together or
    // not at all. Nothing outside the database can force a commit to fail — a map
    // small enough to exhaust is refused at create, and the property setters
    // overwrite one key rather than growing it — so the failure is injected at
    // the one instant that matters: everything staged, nothing committed.
    chain_fixture fixture("tip_atomic");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    build_chain(fixture, 10);

    // A state that is clearly BEFORE the batch being published.
    REQUIRE(chain.set_last_block_height(4) == database::result_code::success);
    REQUIRE(chain.set_utxo_built_height(4) == database::result_code::success);
    REQUIRE(chain.begin_transition_record(database::utxo_transition_record{
        .format_version = database::utxo_transition_record::current_format_version,
        .type = database::transition_type::connect_batch,
        .operation_id = database::make_operation_id(),
        .first_height = 5,
        .intended_last_height = 10,
        .state = database::transition_state::in_progress}) ==
        database::result_code::success);
    REQUIRE(chain.read_transition_record().status ==
            database::transition_status::recovery_required);

    {
        database::testing::fail_publish_transition_before_commit.store(true);
        // Cleared on every path, an exception included: a flag left set would
        // make every later publish in this process fail for no reason.
        struct restore {
            ~restore() {
                database::testing::fail_publish_transition_before_commit.store(false);
            }
        } const guard;

        CHECK(chain.publish_transition(database::transition_heights{
            .last_block_height = 10, .utxo_built_height = 10}) !=
            database::result_code::success);

        // Neither height moved...
        auto const heights = chain.get_last_heights();
        REQUIRE(heights);
        CHECK(heights->block == 4u);
        auto const built = chain.get_utxo_built_height();
        REQUIRE(built);
        CHECK(*built == 4u);

        // ...and the record is still pending, so the next start still refuses.
        CHECK(chain.read_transition_record().status ==
              database::transition_status::recovery_required);
    }

    // The retry publishes both and clears the record, which is what makes the
    // refusal above a rollback rather than a database left broken.
    REQUIRE(chain.publish_transition(database::transition_heights{
        .last_block_height = 10, .utxo_built_height = 10}) ==
        database::result_code::success);

    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 10u);
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == 10u);
    CHECK(chain.read_transition_record().status == database::transition_status::clean);
}

// -----------------------------------------------------------------------------
// An absent marker means different things on different databases
// -----------------------------------------------------------------------------

TEST_CASE("an absent marker over an empty set is zero", "[connected_tip][reconcile]") {
    // A fresh datadir, or one that only ever downloaded: nothing is connected,
    // whatever the connected marker was left claiming.
    chain_fixture fixture("tip_absent_empty");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    REQUIRE_FALSE(chain.get_utxo_built_height());
    REQUIRE(chain.utxo_count().value() == 0u);

    REQUIRE(chain.set_last_block_height(963898) == database::result_code::success);
    auto const tip = chain.reconcile_connected_tip(963898);
    REQUIRE(tip);
    CHECK(*tip == 0u);

    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 0u);   // corrected durably
}

TEST_CASE("an absent marker over a populated set is refused, not assumed to be genesis",
          "[connected_tip][reconcile]") {
    // The case that must NOT publish genesis silently. A materialised UTXO set
    // with no height recording how far it was built cannot be placed: the set
    // carries creation heights, but the highest surviving one is a lower bound —
    // spent outputs are erased — so it is not the built height. Rebuilding from
    // genesis over it would re-send inserts that are already there.
    chain_fixture fixture("tip_absent_populated");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    build_chain(fixture, 10);

    // Entries in the store, and deliberately no marker published for them.
    {
        auto const window = chain.begin_utxo_write().value();
        blockchain::utxo_raw_delta delta;
        hash_digest h{};
        h.fill(0xAB);
        delta.inserts.emplace(
            utxoz::make_outpoint(std::span<uint8_t const, 32>{h.data(), 32}, 0),
            // Eight bytes: reference mode stores a {file_number, tx_offset}
            // reference and rejects anything else, while full mode takes the
            // payload verbatim. The content is irrelevant here — what this needs
            // is a store that is not empty — but the SHAPE is not.
            blockchain::utxo_raw_value{std::vector<uint8_t>(8, 0x11), 7});
        REQUIRE(chain.apply_utxo_inserts_raw(window, delta.inserts) ==
                database::result_code::success);
    }
    REQUIRE(chain.utxo_count().value() > 0u);
    REQUIRE_FALSE(chain.get_utxo_built_height());

    REQUIRE(chain.set_last_block_height(4) == database::result_code::success);

    // Refused rather than answered with zero.
    CHECK_FALSE(chain.reconcile_connected_tip(4));

    // And nothing was written on the way out: the marker still says what it said,
    // so the operator's database is exactly as they left it.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 4u);
}

// -----------------------------------------------------------------------------
// A chain whose block files cannot be read back does not come up (#668)
// -----------------------------------------------------------------------------

TEST_CASE("a chain whose block files cannot be read back refuses to start",
          "[connected_tip][block_scan]") {
    // The refusal an operator actually meets. `block_store` has its own controls
    // for the walk; this is the one that decides whether the CHAIN comes up. The
    // walk governs the append cursor, so a file it stopped understanding has to
    // stop the start rather than yield a shorter answer and a cursor over
    // whatever was left — which would put the next block on top of data nobody
    // could parse.
    //
    // The file is written by hand, which is what a reader test is for: no correct
    // writer produces this, and that is the point.
    chain_fixture fixture("block_scan_refuses");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const dir = fixture.dir();
    fixture.close();

    auto const blocks_dir = dir / "blocks";
    std::error_code ec;
    std::filesystem::create_directories(blocks_dir, ec);

    // Four bytes that are neither this network's magic nor the zeroes of
    // reserved space. The walk cannot say what follows them, so it must not say
    // the file ended here.
    {
        FILE* file = kth::database::open_native(blocks_dir / "blk00000.dat", "wb");
        REQUIRE(file != nullptr);
        std::array<uint8_t, 8> const junk{{0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4}};
        REQUIRE(std::fwrite(junk.data(), 1, junk.size(), file) == junk.size());
        std::fclose(file);
    }

    // And it does not come up. Not "comes up with fewer blocks": does not come up.
    CHECK_FALSE(fixture.start());
}
