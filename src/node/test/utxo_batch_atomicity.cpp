// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <string>
#include <vector>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// A batch that did not finish must not be continued past (#600)
// =============================================================================
//
// Applying a UTXO delta mutates the maps in place: there is no staging, no
// transaction and nothing to roll back. So a batch interrupted anywhere between
// its first mutation and its published height leaves the set part-applied, and
// the built height still names the batch BEFORE — resuming would reapply
// mutations that are already in.
//
// The transition record is what makes that detectable. It is written before the
// first mutation, made durable, and cleared only in the same transaction that
// publishes the height. Finding one at the next start therefore means exactly
// one thing, and the answer to it is a rebuild, never a resume.
//
// Every restart below is a real one: the block_chain is destroyed, the LMDB
// environment and UTXO-Z are closed, and a new chain is opened on the same
// directory. Whatever the node comes back on is whatever reached the disk.

namespace {

// Drive the build task alone and capture what it reports. Used where the whole
// connect path would be noise — the conditions under test are all inside the
// build.
std::vector<std::string> run_build_and_collect_fatals(chain_fixture& fixture,
                                                      uint32_t start_height) {
    std::vector<std::string> fatals;

    ::asio::io_context ctx;
    std::atomic<uint32_t> contiguous{start_height};

    ::asio::co_spawn(ctx,
        utxo_build_task(fixture.chain(), contiguous, start_height,
            domain::config::network::regtest,
            [] { return false; },
            [&fatals](std::string const& reason) { fatals.push_back(reason); }),
        ::asio::detached);

    ctx.run_for(std::chrono::seconds(30));
    return fatals;
}

// A trunk of `len` empty blocks connected through the node's own path, so the
// state the boundaries below are simulated against is one the node built.
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

// A key no block produces, for standing in for a batch's mutations.
utxoz::raw_outpoint synthetic_key(uint8_t seed) {
    hash_digest txid{};
    txid.fill(seed);
    return utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, 0);
}

// Put one entry in the set the way a delta would. The payload is eight bytes
// because reference mode stores exactly that; full mode takes any length and
// nothing here reads it back, so one shape serves both builds.
void insert_raw(blockchain::block_chain& chain, utxoz::raw_outpoint const& key, uint32_t height) {
    blockchain::utxo_raw_delta delta;
    delta.inserts.emplace(key, blockchain::utxo_raw_value{
        std::vector<uint8_t>(8, 0x11), height});
    REQUIRE(chain.apply_utxo_inserts_raw(delta.inserts)
            == database::result_code::success);
}

database::utxo_transition_record batch_record(uint32_t first, uint32_t intended) {
    return database::utxo_transition_record{
        .format_version = database::utxo_transition_record::current_format_version,
        .type = database::transition_type::connect_batch,
        .operation_id = database::make_operation_id(),
        .first_height = first,
        .intended_last_height = intended,
        .state = database::transition_state::in_progress};
}

// Steps 2 and 3: the record, and the barrier that makes it durable. Every
// boundary below starts here, because every real batch does.
void open_transition(blockchain::block_chain& chain, uint32_t first, uint32_t intended) {
    REQUIRE(chain.begin_transition_record(batch_record(first, intended))
            == database::result_code::success);
    REQUIRE(chain.env_sync() == database::result_code::success);
}

} // namespace

// -----------------------------------------------------------------------------
// The connect path writes the record before it mutates anything
// -----------------------------------------------------------------------------

TEST_CASE("a batch that fails while mutating leaves its record behind",
          "[node][utxo][atomicity]") {
    // The control for step 2. Everything else in this file writes the record by
    // hand, so without this test the whole suite would still pass against a
    // build that never wrote one.
    //
    // The delta is made to fail by putting one of the outputs block 1 creates
    // into the set first: UTXO-Z reports the second insert as a duplicate key,
    // apply_utxo_delta_raw refuses, and the batch stops at step 4 — after the
    // record was written and before the height moved. That is the interrupted
    // state the record exists to describe, reached without any storage fault.
    chain_fixture fixture("batch_records_before_mutating");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 40 * block_spacing;
    auto const blk = mine_block(genesis.hash(), 1, base_time + block_spacing, 0, {}, 0);
    std::vector<domain::chain::block> const blocks{blk};

    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == 1);
    persist_headers(fixture, blocks, 1);

    // The coinbase output the batch is about to insert, inserted first.
    auto const& coinbase = blk.transactions().front();
    auto const txid = coinbase.hash();
    auto const clash = utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, 0);
    insert_raw(chain, clash, 1);

    run_connect_tasks_expect_fatal(fixture, blocks, 1,
        "a UTXO delta could not be applied");

    // The batch stopped, and it said so durably. Both halves matter: a build
    // that mutated first and recorded afterwards would leave nothing here.
    auto const check = chain.read_transition_record();
    REQUIRE(check.status == database::transition_status::recovery_required);
    REQUIRE(check.record.has_value());
    CHECK(check.record->type == database::transition_type::connect_batch);
    CHECK(check.record->first_height == 1u);

    // And the height never moved, so a start reading it alone would believe
    // nothing had happened — which is why the record is what it reads instead.
    auto const built = chain.get_utxo_built_height();
    CHECK(( ! built || *built == 0u));

    // The next start refuses.
    CHECK_FALSE(fixture.restart());
}

TEST_CASE("a batch that completed leaves no record behind", "[node][utxo][atomicity]") {
    // The other direction, and not a formality: a record that was never cleared
    // refuses every later start. That is a different failure and just as bad,
    // and it is the one a change to step 10 would introduce.
    chain_fixture fixture("batch_completes_clean");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    connect_trunk(fixture, 3);

    auto const check = fixture.chain().read_transition_record();
    CHECK(check.status == database::transition_status::clean);

    // A real restart, and it comes back up where it left off.
    REQUIRE(fixture.restart());
    auto const built = fixture.chain().get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == 3u);
}

// -----------------------------------------------------------------------------
// One test per crash boundary
// -----------------------------------------------------------------------------
//
// The stores are put into the state each boundary leaves and the node is then
// restarted for real. The record does not distinguish between them — that is
// deliberate, and stated in #600: it says WHETHER the transition finished,
// never how far it got — so what each of these proves is that the answer is the
// same refusal from every one of them, including the ones where the set looks
// untouched and the ones where it looks complete.

TEST_CASE("a crash right after the batch was recorded refuses the next start",
          "[node][utxo][atomicity]") {
    // Boundary 1: the record is on disk and not one byte of the set has moved.
    // The tempting reading is "nothing happened, carry on" — and it is wrong,
    // because nothing here can tell this state from the next one.
    chain_fixture fixture("boundary_started");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    connect_trunk(fixture, 3);
    open_transition(fixture.chain(), 4u, 6u);

    CHECK_FALSE(fixture.restart());
}

TEST_CASE("a crash after the delta was applied refuses the next start",
          "[node][utxo][atomicity]") {
    // Boundary 2: the set holds this batch's mutations and the height does not
    // name them. Resuming would apply them again.
    chain_fixture fixture("boundary_delta");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    connect_trunk(fixture, 3);
    open_transition(fixture.chain(), 4u, 6u);
    insert_raw(fixture.chain(), synthetic_key(0xA1), 4);

    CHECK_FALSE(fixture.restart());
}

TEST_CASE("a crash after the deletions are applied refuses the next start",
          "[node][utxo][atomicity]") {
    // Boundary 3: the delta is in and the deletions this batch owed have been
    // applied, so the set is internally complete for this batch — and still
    // nothing durable says so.
    chain_fixture fixture("boundary_deletions");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    connect_trunk(fixture, 3);
    auto& chain = fixture.chain();
    open_transition(chain, 4u, 6u);
    insert_raw(chain, synthetic_key(0xA2), 4);

    // Nothing is owed here: the insert above created no deletion obligation.
    auto const progress = chain.utxo_apply_deletes({});
    REQUIRE(progress.erased.empty());
    REQUIRE(progress.unresolved.empty());

    CHECK_FALSE(fixture.restart());
}

TEST_CASE("a crash after every durability barrier refuses the next start",
          "[node][utxo][atomicity]") {
    // Boundary 4: every store has been put on stable storage and the height
    // still has not moved. This is the boundary that looks most like success,
    // and it is the one a build without the record would be least able to
    // detect: the set is complete, durable, and one transaction short of being
    // described.
    chain_fixture fixture("boundary_barriers");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    connect_trunk(fixture, 3);
    auto& chain = fixture.chain();
    open_transition(chain, 4u, 6u);
    insert_raw(chain, synthetic_key(0xA3), 4);

    // No obligation was created by the insert above.
    auto const progress = chain.utxo_apply_deletes({});
    REQUIRE(progress.unresolved.empty());

    std::vector<int32_t> const touched{0};
    REQUIRE(chain.flush_undo(touched).has_value());
    CHECK(chain.utxo_sync() != database::barrier_outcome::failed);
    REQUIRE(chain.env_sync() == database::result_code::success);

    CHECK_FALSE(fixture.restart());
}

TEST_CASE("a crash after the height was published starts cleanly",
          "[node][utxo][atomicity]") {
    // Boundary 5, and the one that must NOT refuse. Steps 10 and 11 ran: the
    // height and the clearing of the record went into one transaction and that
    // transaction reached the disk, so the transition finished and there is
    // nothing to recover.
    //
    // Without this test every refusal above would be satisfied by a node that
    // refuses unconditionally.
    chain_fixture fixture("boundary_published");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Four real blocks, then the built height wound back to 3 — so the batch
    // staged below publishes a height that names a block the store actually
    // holds. A height with no block behind it is a different broken state, and
    // startup refuses on it for a different reason (undo coverage), which would
    // make this test pass without saying anything about the record.
    connect_trunk(fixture, 4);
    auto& chain = fixture.chain();
    REQUIRE(chain.set_heights(database::transition_heights{
        .last_block_height = std::nullopt,
        .utxo_built_height = 3u}) == database::result_code::success);
    auto const before = chain.utxo_size();

    open_transition(chain, 4u, 4u);
    auto const key = synthetic_key(0xA4);
    insert_raw(chain, key, 4);
    REQUIRE(chain.publish_transition(database::transition_heights{
        .last_block_height = std::nullopt,
        .utxo_built_height = 4u}) == database::result_code::success);
    REQUIRE(chain.env_sync() == database::result_code::success);

    REQUIRE(fixture.restart());

    // And it came back describing what was published, not what was attempted.
    auto const built = fixture.chain().get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == 4u);

    // The batch's one entry is in the set, once. Neither lost by the restart
    // nor doubled by anything replaying it.
    CHECK(fixture.chain().utxo_size() == before + 1);
    auto const found = fixture.chain().find_utxo_raw(key, 4u);
    CHECK(found.has_value());
}

// -----------------------------------------------------------------------------
// What survives a restart, counted
// -----------------------------------------------------------------------------

TEST_CASE("a restart neither loses nor duplicates what a batch applied",
          "[node][utxo][atomicity]") {
    // The refusals above are about states that must not be continued. This is
    // the other obligation: a transition that DID finish has to come back
    // whole. An output the chain spent must be gone and stay gone, and the
    // outputs it created must be there exactly once.
    constexpr uint32_t trunk_len = 100;   // coinbase maturity
    chain_fixture fixture("restart_conserves");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const trunk = connect_trunk(fixture, trunk_len);

    // Block 101 spends block 1's coinbase, which matures exactly there.
    auto const& matured = trunk.front().transactions().front();
    constexpr uint64_t fee = 1000;
    auto const spend = spend_p2pkh(matured, 0, matured.outputs()[0].value() - fee,
        chain.chain_settings().enabled_flags());

    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;
    auto const blk = mine_block(trunk.back().hash(), trunk_len + 1,
        base_time + (trunk_len + 1) * block_spacing, 1, {spend}, fee);
    std::vector<domain::chain::block> const blocks{blk};

    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == 1);
    persist_headers(fixture, blocks, trunk_len + 1);
    connect_bodies(fixture, blocks, trunk_len + 1);

    auto const size_before = chain.utxo_size();
    REQUIRE(size_before > 0);

    // Nothing was left in flight, so the restart must succeed.
    REQUIRE(fixture.restart());
    auto& reopened = fixture.chain();

    auto const built = reopened.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == trunk_len + 1);

    // Counted, not sampled: a duplicate insert or a deletion that never ran
    // moves this number, and neither shows up in a spot check of one outpoint.
    CHECK(reopened.utxo_size() == size_before);

    // The spent coinbase is gone — the deletion was applied and its effect
    // reached the disk.
    CHECK_FALSE(utxo_present(reopened, matured.hash(), 0, trunk_len + 1));

    // And what block 101 created is there.
    CHECK(utxo_present(reopened, spend.hash(), 0, trunk_len + 1));
    CHECK(utxo_present(reopened, blk.transactions().front().hash(), 0, trunk_len + 1));
}

TEST_CASE("a connected batch owes no deletions once it is published",
          "[node][utxo][atomicity]") {
    // Step 6 sits before the publication, so by the time a batch is described as
    // connected nothing of it is still owed. This does not observe an internal
    // queue — there is none — it observes what the obligation being met leaves
    // behind: a clean transition record and the deletions' effects on disk. A
    // build that published first and applied afterwards would be caught by a
    // crash between the two, which a test cannot stage; the same defect is
    // visible without one as a batch that closed while still owing deletions.
    constexpr uint32_t trunk_len = 100;
    chain_fixture fixture("batch_owes_nothing");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const trunk = connect_trunk(fixture, trunk_len);

    auto const& matured = trunk.front().transactions().front();
    constexpr uint64_t fee = 1000;
    auto const spend = spend_p2pkh(matured, 0, matured.outputs()[0].value() - fee,
        chain.chain_settings().enabled_flags());

    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;
    auto const blk = mine_block(trunk.back().hash(), trunk_len + 1,
        base_time + (trunk_len + 1) * block_spacing, 1, {spend}, fee);
    std::vector<domain::chain::block> const blocks{blk};

    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == 1);
    persist_headers(fixture, blocks, trunk_len + 1);
    connect_bodies(fixture, blocks, trunk_len + 1);

    CHECK(chain.read_transition_record().status == database::transition_status::clean);
}

// -----------------------------------------------------------------------------
// The reorganization runs the same protocol
// -----------------------------------------------------------------------------

TEST_CASE("a reorganization records itself and clears it once it has published",
          "[node][utxo][atomicity][reorg]") {
    // A chain switch rewrites the UTXO state of every height above the fork. It
    // is a transition in exactly the sense the record means, and it gets the
    // same treatment — otherwise a node interrupted mid-switch comes back with
    // the set rewound part way and nothing saying so.
    //
    // What is observable after the fact is the pair: the switch completed, so
    // the record is gone; and the node restarts, so it was genuinely cleared
    // rather than never written.
    constexpr uint32_t trunk_len = 100;
    chain_fixture fixture("reorg_records");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const trunk = connect_trunk(fixture, trunk_len);
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    // One block on the branch that will be abandoned...
    auto const a101 = mine_block(trunk.back().hash(), trunk_len + 1,
        base_time + (trunk_len + 1) * block_spacing, 1, {}, 0);
    std::vector<domain::chain::block> const branch_a{a101};
    REQUIRE(fixture.organizer().add_headers(headers_of(branch_a)).headers_added == 1);
    persist_headers(fixture, branch_a, trunk_len + 1);
    connect_bodies(fixture, branch_a, trunk_len + 1);

    // ...and three that outweigh it.
    std::vector<domain::chain::block> branch_b;
    auto prev = trunk.back().hash();
    for (uint32_t h = trunk_len + 1; h <= trunk_len + 3; ++h) {
        branch_b.push_back(mine_block(prev, h, base_time + h * block_spacing + 60, 2, {}, 0));
        prev = branch_b.back().hash();
    }

    auto const b_result = fixture.organizer().add_headers(headers_of(branch_b));
    REQUIRE(b_result.reorg_candidate);
    REQUIRE(b_result.reorg_fork_height == int32_t(trunk_len));

    reorg_outcome reorg;
    {
        ::asio::io_context switch_ctx;
        ::asio::co_spawn(switch_ctx,
            execute_reorg(chain, fixture.organizer(), b_result.reorg_branch_head, trunk_len,
                [] { return false; }, real_persister(chain)),
            [&reorg](std::exception_ptr, reorg_outcome result) { reorg = result; });
        switch_ctx.run_for(std::chrono::seconds(30));
    }

    REQUIRE(reorg.result.ok);
    REQUIRE_FALSE(reorg.fatal);
    REQUIRE(reorg.result.validated_tip);
    CHECK(*reorg.result.validated_tip == trunk_len);

    // Published, so nothing is left in flight.
    CHECK(chain.read_transition_record().status == database::transition_status::clean);

    // Both heights came back to the fork, and in one transaction: a start that
    // found them naming different blocks could not tell which described the set.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == trunk_len);
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == trunk_len);

    // And the node comes back up on it.
    REQUIRE(fixture.restart());
    auto const after = fixture.chain().get_utxo_built_height();
    REQUIRE(after);
    CHECK(*after == trunk_len);
}

TEST_CASE("a reorganization left in flight refuses the next start",
          "[node][utxo][atomicity][reorg]") {
    // The reorg's own record reaching the same refusal. Staged rather than
    // interrupted, for the same reason as the batch boundaries above: what a
    // test can produce is the durable evidence, and the answer to it is what is
    // under test.
    chain_fixture fixture("reorg_in_flight");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    connect_trunk(fixture, 3);

    REQUIRE(fixture.chain().begin_transition_record(database::utxo_transition_record{
        .format_version = database::utxo_transition_record::current_format_version,
        .type = database::transition_type::reorg,
        .operation_id = database::make_operation_id(),
        .first_height = 2u,
        .intended_last_height = 3u,
        .state = database::transition_state::in_progress})
            == database::result_code::success);
    REQUIRE(fixture.chain().env_sync() == database::result_code::success);

    CHECK_FALSE(fixture.restart());
}

TEST_CASE("a clean database lets the build start", "[node][utxo][atomicity]") {
    // So the refusals above cannot pass by the task declining to run for some
    // unrelated reason.
    chain_fixture fixture("batch_clean_start");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    REQUIRE(fixture.chain().read_transition_record().status
            == database::transition_status::clean);

    auto const fatals = run_build_and_collect_fatals(fixture, 1);
    CHECK(fatals.empty());
}
