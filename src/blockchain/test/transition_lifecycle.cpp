// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// The record's lifecycle against a real database, and what a start does with it
// =============================================================================
//
// The decoding tests next door prove the bytes cannot be misread. These prove
// the two things the bytes are for: that a record written before a transition
// survives to the next start, and that the next start refuses on it instead of
// resuming.
//
// Every one of them goes through a genuine close and reopen — the same sequence
// a process restart performs — because a record that only exists in a live
// object's memory answers nothing about a node that died.

namespace {

database::utxo_transition_record in_flight(
    database::transition_type kind, uint32_t first, uint32_t intended) {
    return database::utxo_transition_record{
        .format_version = database::utxo_transition_record::current_format_version,
        .type = kind,
        .operation_id = database::make_operation_id(),
        .first_height = first,
        .intended_last_height = intended,
        .state = database::transition_state::in_progress};
}

// Read the record through a second, short-lived database opened on the same
// directory after the chain has been closed.
//
// It cannot be read through a restarted chain: a start that finds a record
// refuses, so `restart()` returns false and there is no chain left to ask. This
// opens the layer underneath instead, which is the layer the record lives in —
// and it is a genuine reopen, so what comes back is what reached the disk.
database::transition_check read_record_from_disk(std::filesystem::path const& dir) {
    database::settings settings;
    settings.directory = dir;

    database::data_base db(settings);
    REQUIRE(db.open());
    auto const check = db.internal_db().read_transition_record();
    REQUIRE(db.close());
    return check;
}

} // namespace

TEST_CASE("a fresh database has no transition record", "[transition][lifecycle]") {
    // The baseline the rest of the file is measured against. If this ever said
    // anything but `clean`, every refusal below would pass for the wrong
    // reason — and the node would refuse to start on a database that is fine.
    chain_fixture fixture("transition_fresh");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const check = fixture.chain().read_transition_record();
    CHECK(check.status == database::transition_status::clean);
    CHECK_FALSE(check.record.has_value());
    CHECK_FALSE(check.decode_error.has_value());
}

TEST_CASE("a record written before a transition survives a restart",
          "[transition][lifecycle]") {
    // Step 2 of the durable order is worth nothing if the record does not
    // outlive the process that wrote it. Written, then the environment closed
    // and reopened, then read back field for field.
    chain_fixture fixture("transition_survives");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const record = in_flight(database::transition_type::connect_batch, 101u, 200u);
    REQUIRE(fixture.chain().begin_transition_record(record) == database::result_code::success);
    REQUIRE(fixture.chain().env_sync() == database::result_code::success);

    // The chain goes down for real: the object is destroyed and the environment
    // closed. Nothing in memory carries over to what is read below.
    fixture.close();

    // Field for field, which is what this test is for. That the next start
    // refuses is the NEXT test's claim; asserting it here as well would leave
    // the persistence of the fields themselves unchecked, since a record whose
    // every field came back as zero would refuse a start just as loudly.
    auto const check = read_record_from_disk(fixture.dir());
    REQUIRE(check.status == database::transition_status::recovery_required);
    REQUIRE(check.record.has_value());
    CHECK(check.record->format_version == record.format_version);
    CHECK(check.record->type == record.type);
    CHECK(check.record->operation_id == record.operation_id);
    CHECK(check.record->first_height == record.first_height);
    CHECK(check.record->intended_last_height == record.intended_last_height);
    CHECK(check.record->state == record.state);
}

TEST_CASE("a start refuses to open a database whose last batch did not finish",
          "[transition][lifecycle]") {
    chain_fixture fixture("transition_refuses_batch");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    REQUIRE(fixture.chain().begin_transition_record(
        in_flight(database::transition_type::connect_batch, 1u, 3u))
            == database::result_code::success);
    REQUIRE(fixture.chain().env_sync() == database::result_code::success);

    // Refusing is the whole point. A start that reported the condition and came
    // up anyway would be worse than one that never checked, because the log
    // line would suggest the node had handled it.
    CHECK_FALSE(fixture.restart());
}

TEST_CASE("a start refuses to open a database whose last reorganization did not finish",
          "[transition][lifecycle]") {
    // The other kind. The two have different rebuild answers, so both have to
    // reach the refusal rather than one of them being the only path tested.
    chain_fixture fixture("transition_refuses_reorg");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    REQUIRE(fixture.chain().begin_transition_record(
        in_flight(database::transition_type::reorg, 5u, 9u))
            == database::result_code::success);
    REQUIRE(fixture.chain().env_sync() == database::result_code::success);

    CHECK_FALSE(fixture.restart());
}

TEST_CASE("a record this build cannot read is refused, not passed over",
          "[transition][lifecycle]") {
    // "Could not read" is never reported as "clean". A record written by a
    // newer build is the reachable case: it decodes far enough to have a valid
    // checksum and no further, which is exactly the state where a reader that
    // fell back to `nullopt` would call the database clean.
    chain_fixture fixture("transition_future_version");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto future = in_flight(database::transition_type::connect_batch, 7u, 11u);
    future.format_version = database::utxo_transition_record::current_format_version + 1u;

    REQUIRE(fixture.chain().begin_transition_record(future) == database::result_code::success);
    REQUIRE(fixture.chain().env_sync() == database::result_code::success);

    // Read back through the live chain first, so the failure is attributable to
    // the decoder rather than to the start.
    auto const check = fixture.chain().read_transition_record();
    CHECK(check.status == database::transition_status::corrupt);
    REQUIRE(check.decode_error.has_value());
    CHECK(*check.decode_error == database::transition_decode_error::unknown_version);

    CHECK_FALSE(fixture.restart());
}

TEST_CASE("publishing a transition clears its record and moves the heights together",
          "[transition][lifecycle]") {
    // Step 10. Both halves in one call because they are one transaction: a
    // record cleared without the heights, or heights moved without the record
    // cleared, are the two states the whole design exists to make unreachable.
    chain_fixture fixture("transition_publishes");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    REQUIRE(chain.begin_transition_record(
        in_flight(database::transition_type::connect_batch, 1u, 1u))
            == database::result_code::success);
    REQUIRE(chain.env_sync() == database::result_code::success);
    REQUIRE(chain.read_transition_record().status
            == database::transition_status::recovery_required);

    REQUIRE(chain.publish_transition(database::transition_heights{
        .last_block_height = std::nullopt,
        .utxo_built_height = 0u}) == database::result_code::success);
    REQUIRE(chain.env_sync() == database::result_code::success);

    CHECK(chain.read_transition_record().status == database::transition_status::clean);

    // And a restart comes back up, which is the half a test that only checked
    // the record would miss: a marker that was never cleared refuses every
    // later start, which is a different failure and just as bad.
    CHECK(fixture.restart());

    auto const built = fixture.chain().get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == 0u);
}

TEST_CASE("publishing no heights at all does not clear the record",
          "[transition][lifecycle]") {
    // The heights are optional individually — a connect batch moves only the
    // built height, a reorganization moves both — so nothing in the type stops a
    // caller passing neither. Writing them is then a no-op and the clear runs
    // alone, which is a transition declared finished without publishing where it
    // finished: the exact state one transaction was chosen to make unreachable.
    chain_fixture fixture("transition_publishes_nothing");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const record = in_flight(database::transition_type::connect_batch, 1u, 1u);
    REQUIRE(chain.begin_transition_record(record) == database::result_code::success);
    REQUIRE(chain.env_sync() == database::result_code::success);

    CHECK(chain.publish_transition(database::transition_heights{
        .last_block_height = std::nullopt,
        .utxo_built_height = std::nullopt}) != database::result_code::success);

    // Still in flight, and still on disk: the refusal happens before the
    // transaction opens, so there is nothing partially written to come back to.
    CHECK(chain.read_transition_record().status
          == database::transition_status::recovery_required);

    fixture.close();
    auto const check = read_record_from_disk(fixture.dir());
    REQUIRE(check.record.has_value());
    CHECK(check.record->operation_id == record.operation_id);
}

TEST_CASE("the refusal names the transition that caused it", "[transition][lifecycle]") {
    // The id, the kind and the range are what a diagnosis starts from: they are
    // how the record found at this start is matched to the log line the run
    // that failed wrote. A refusal that could not name them would send an
    // operator to the wrong database.
    chain_fixture fixture("transition_names");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const record = in_flight(database::transition_type::reorg, 640'000u, 640'012u);
    REQUIRE(fixture.chain().begin_transition_record(record) == database::result_code::success);
    REQUIRE(fixture.chain().env_sync() == database::result_code::success);

    auto const check = fixture.chain().read_transition_record();
    REQUIRE(check.status == database::transition_status::recovery_required);
    REQUIRE(check.record.has_value());
    CHECK(check.record->type == database::transition_type::reorg);
    CHECK(check.record->operation_id == record.operation_id);
    CHECK(check.record->first_height == 640'000u);
    CHECK(check.record->intended_last_height == 640'012u);
}

TEST_CASE("moving the heights leaves the record alone", "[transition][lifecycle]") {
    // set_heights is a step INSIDE a transition — a reorganization rolls both
    // heights back one block at a time — so it must not look like the end of
    // one. If it cleared the record, every reorganization would be reported as
    // finished the moment its first block came off.
    chain_fixture fixture("transition_set_heights");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    REQUIRE(chain.begin_transition_record(
        in_flight(database::transition_type::reorg, 1u, 4u))
            == database::result_code::success);

    REQUIRE(chain.set_heights(database::transition_heights{
        .last_block_height = 0u,
        .utxo_built_height = 0u}) == database::result_code::success);

    CHECK(chain.read_transition_record().status
          == database::transition_status::recovery_required);
}

TEST_CASE("the LMDB barrier reports rather than assumes", "[transition][lifecycle]") {
    // Steps 3 and 11. The environment is opened MDB_NOSYNC, so this is what
    // separates a record that was written from one that reached the disk. It
    // has to answer, and a caller has to be able to act on the answer — which
    // is why it returns a result_code and is [[nodiscard]].
    chain_fixture fixture("transition_env_sync");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    CHECK(fixture.chain().env_sync() == database::result_code::success);
}
