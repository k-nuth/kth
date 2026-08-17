// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <expected>
#include <limits>

#include <kth/node/sync/orchestrator.hpp>

using namespace kth;
using namespace kth::node::sync;
using kth::database::result_code;

// =============================================================================
// The post-checkpoint range waits for the UTXO set, not for the checkpoint (#663)
// =============================================================================
//
// To validate the block at height H the set must already describe the chain
// through H - 1: full validation resolves that block's prevouts against it, and a
// set that is short answers a PROVEN ABSENCE — indistinguishable from an output
// that was never there. The block is then rejected with a consensus verdict for a
// condition that is purely transient.
//
// The decision is a pure function of two values, so every case here is a value
// rather than a scenario to stage. What the coordinator does with the answer —
// hold, log once, and re-ask when the builder advances — is exercised by the
// orchestrator's own suite; what is tested here is that the answer is right, and
// in particular that it is not the answer the obvious condition would give.

namespace {

std::expected<uint32_t, result_code> built_at(uint32_t height) {
    return height;
}

std::expected<uint32_t, result_code> unreadable(result_code code) {
    return std::unexpected(code);
}

} // namespace

TEST_CASE("slow sync - the set exactly one block below start is enough",
          "[node][sync][slow_sync]") {
    // The boundary, and it is the whole condition. Block H needs the set at
    // H - 1, no more: demanding H would wait for the block the range has not
    // downloaded yet, and the range would never start at all.
    CHECK(may_start_slow_sync(963888, built_at(963887)) == slow_sync_admission::start);
}

TEST_CASE("slow sync - one block short is not enough", "[node][sync][slow_sync]") {
    CHECK(may_start_slow_sync(963888, built_at(963886)) == slow_sync_admission::builder_behind);
}

TEST_CASE("slow sync - ahead is enough", "[node][sync][slow_sync]") {
    // The builder can be past the start of the range; a range whose blocks are
    // already described is fine to validate.
    CHECK(may_start_slow_sync(963888, built_at(970000)) == slow_sync_admission::start);
}

TEST_CASE("slow sync - the run that exposed this would still be held",
          "[node][sync][slow_sync]") {
    // THE CASE THAT MATTERS, with the numbers from the observed IBD: checkpoint
    // 951146, range about to start at 963888, UTXO set at 962946.
    constexpr uint32_t checkpoint = 951146;
    constexpr uint32_t start_height = 963888;
    constexpr uint32_t utxo = 962946;

    // The obvious condition — "the set is past the checkpoint" — is TRUE here,
    // by more than twelve thousand blocks. Asserted so that a future change to
    // that shape fails rather than looks equivalent.
    REQUIRE(utxo >= checkpoint);
    CHECK(utxo - checkpoint == 11800u);

    // And the range still must not start: the first block of it needs the set at
    // 963887, and the set is 941 short.
    CHECK(start_height - 1 - utxo == 941u);
    CHECK(may_start_slow_sync(start_height, built_at(utxo))
        == slow_sync_admission::builder_behind);

    // It is released when the builder arrives, and not one block earlier.
    CHECK(may_start_slow_sync(start_height, built_at(start_height - 2))
        == slow_sync_admission::builder_behind);
    CHECK(may_start_slow_sync(start_height, built_at(start_height - 1))
        == slow_sync_admission::start);
}

TEST_CASE("slow sync - an unreadable height never admits", "[node][sync][slow_sync]") {
    // Fail-closed, over every way the read can fail. "No answer" must never be
    // read as "far enough": that is a validator judging blocks against a set it
    // cannot describe, and the verdict it produces is a consensus one.
    for (auto const code : {result_code::key_not_found,
                            result_code::db_empty,
                            result_code::other,
                            result_code::db_corrupt,
                            result_code::not_resolved,
                            result_code::recovery_required}) {
        CAPTURE(database::result_code_name(code));
        CHECK(may_start_slow_sync(963888, unreadable(code))
            == slow_sync_admission::height_unavailable);
    }
}

TEST_CASE("slow sync - a latched store is held, not admitted", "[node][sync][slow_sync]") {
    // Named on its own because it is the one that would be tempting to treat as
    // "the store will tell us later": it will not, it has stopped answering, and
    // the range must not go out.
    auto const admission = may_start_slow_sync(963888, unreadable(result_code::recovery_required));
    CHECK(admission == slow_sync_admission::height_unavailable);
    CHECK(admission != slow_sync_admission::start);
}

TEST_CASE("slow sync - the bottom of the range does not underflow",
          "[node][sync][slow_sync]") {
    // start_height is blocks_synced_to + 1 at every call site, so 0 cannot reach
    // here today. It is answered anyway, and answered FIRST, so the subtraction
    // that follows is never reached with a zero: a range starting at genesis needs
    // nothing built ahead of it.
    CHECK(may_start_slow_sync(0, built_at(0)) == slow_sync_admission::start);
    CHECK(may_start_slow_sync(0, built_at(std::numeric_limits<uint32_t>::max()))
        == slow_sync_admission::start);

    // Zero is answered before the height is even consulted, so an unreadable one
    // does not change it. Stated because the alternative — checking the height
    // first — would make a genesis range depend on a store that has built
    // nothing, which is every fresh database.
    CHECK(may_start_slow_sync(0, unreadable(result_code::key_not_found))
        == slow_sync_admission::height_unavailable);

    CHECK(may_start_slow_sync(1, built_at(0)) == slow_sync_admission::start);
    CHECK(may_start_slow_sync(2, built_at(0)) == slow_sync_admission::builder_behind);
}

TEST_CASE("slow sync - the top of the range does not overflow", "[node][sync][slow_sync]") {
    // The other end, and it is the reason the arithmetic is a subtraction on
    // start_height rather than an addition on the measured height: `built + 1`
    // wraps to 0 at UINT32_MAX, which would report the highest set representable
    // as behind every range there is.
    constexpr auto top = std::numeric_limits<uint32_t>::max();

    CHECK(may_start_slow_sync(top, built_at(top)) == slow_sync_admission::start);
    CHECK(may_start_slow_sync(top, built_at(top - 1)) == slow_sync_admission::start);
    CHECK(may_start_slow_sync(top, built_at(top - 2))
        == slow_sync_admission::builder_behind);

    // A builder at the top admits every range, which is the shape that breaks
    // under the wrapping form.
    CHECK(may_start_slow_sync(1, built_at(top)) == slow_sync_admission::start);
    CHECK(may_start_slow_sync(963888, built_at(top)) == slow_sync_admission::start);
    CHECK(may_start_slow_sync(top - 1, built_at(top)) == slow_sync_admission::start);
}

TEST_CASE("slow sync - every admission has a name of its own", "[node][sync][slow_sync]") {
    // The log line that reports a hold is the only place an operator sees this,
    // so a value without a name of its own would be a hold nobody can diagnose.
    CHECK(std::string_view(to_string(slow_sync_admission::start)) == "start");
    CHECK(std::string_view(to_string(slow_sync_admission::builder_behind))
        == "builder_behind");
    CHECK(std::string_view(to_string(slow_sync_admission::height_unavailable))
        == "height_unavailable");
}

// =============================================================================
// Waking the coordinator up without waiting for anything else
// =============================================================================
//
// The hold above is only correct if something makes the coordinator ask again.
// In the observed run nothing did: the range went out when an unrelated peer
// event happened to arrive, more than ten minutes late. The bridge's inputs are a
// timer and the store, and these are its decisions.

TEST_CASE("slow sync - a height that moved is announced", "[node][sync][slow_sync]") {
    // The mechanism that replaces the accident. No peer, no header batch, no
    // other task: the height changed, so the coordinator is told.
    auto const step = utxo_progress_for_tick(962946u, built_at(962947u));
    REQUIRE(step.announce);
    CHECK(*step.announce == 962947u);
    CHECK(step.next_last_seen == std::optional<uint32_t>(962947u));
}

TEST_CASE("slow sync - the first reading is announced", "[node][sync][slow_sync]") {
    auto const step = utxo_progress_for_tick(std::nullopt, built_at(951146u));
    REQUIRE(step.announce);
    CHECK(*step.announce == 951146u);
}

TEST_CASE("slow sync - a height that did not move is silent", "[node][sync][slow_sync]") {
    // A builder between batches reads the same height for a second at a time. One
    // event per tick would bury the coordinator's own log and buy nothing: the
    // answer cannot have changed.
    auto const step = utxo_progress_for_tick(962946u, built_at(962946u));
    CHECK_FALSE(step.announce.has_value());
    CHECK(step.next_last_seen == std::optional<uint32_t>(962946u));
}

TEST_CASE("slow sync - a failed read announces nothing and forgets",
          "[node][sync][slow_sync]") {
    // Two halves, and the second is the one that closes a stall.
    //
    // Nothing is announced, because the coordinator reads the store itself when it
    // evaluates. But the remembered height is CLEARED — otherwise a store that
    // hiccups while the coordinator is holding, and then answers the same height
    // again, would produce no announcement at all, and the coordinator would be
    // back to waiting for an unrelated event.
    for (auto const code : {result_code::key_not_found,
                            result_code::other,
                            result_code::recovery_required}) {
        CAPTURE(database::result_code_name(code));

        auto const step = utxo_progress_for_tick(962946u, unreadable(code));
        CHECK_FALSE(step.announce.has_value());
        CHECK_FALSE(step.next_last_seen.has_value());

        auto const from_nothing = utxo_progress_for_tick(std::nullopt, unreadable(code));
        CHECK_FALSE(from_nothing.announce.has_value());
        CHECK_FALSE(from_nothing.next_last_seen.has_value());
    }
}

TEST_CASE("slow sync - a hiccup at a stable height still wakes the coordinator",
          "[node][sync][slow_sync]") {
    // The sequence, tick by tick, because the defect only exists across ticks.
    //
    // The coordinator is holding — on `builder_behind` or on `height_unavailable`,
    // it does not matter which — and the builder has not moved. Without the
    // forgetting above, the third tick here is silent and nothing ever asks again.
    std::optional<uint32_t> last_seen;

    auto first = utxo_progress_for_tick(last_seen, built_at(963887u));
    last_seen = first.next_last_seen;
    REQUIRE(first.announce);                       // tick 1: announced

    auto hiccup = utxo_progress_for_tick(last_seen, unreadable(result_code::other));
    last_seen = hiccup.next_last_seen;
    CHECK_FALSE(hiccup.announce.has_value());      // tick 2: silent, and forgets

    auto recovered = utxo_progress_for_tick(last_seen, built_at(963887u));
    last_seen = recovered.next_last_seen;
    REQUIRE(recovered.announce);                   // tick 3: SAME height, announced
    CHECK(*recovered.announce == 963887u);

    // And it settles again straight away: the fourth tick is silent.
    auto steady = utxo_progress_for_tick(last_seen, built_at(963887u));
    CHECK_FALSE(steady.announce.has_value());
}

TEST_CASE("slow sync - a height that went backwards is still announced",
          "[node][sync][slow_sync]") {
    // A rebuild or a rewind lowers it. Announcing only increases would leave the
    // coordinator holding against a height that no longer exists, so the test is
    // "changed", not "grew".
    auto const step = utxo_progress_for_tick(963887u, built_at(951146u));
    REQUIRE(step.announce);
    CHECK(*step.announce == 951146u);
}

TEST_CASE("slow sync - a stale wake-up cannot admit a range", "[node][sync][slow_sync]") {
    // `utxo_build_advanced` carries a height, and it is a WAKE-UP rather than an
    // authority: the message can sit in the channel while a reorg, a startup
    // reconciliation or a rebuild lowers the real height under it.
    //
    // So the decision is taken over the height read at the moment of deciding,
    // and never over the one a message reported. Here the message would have said
    // 963887 — exactly enough — and the store now says 951146, which is not.
    constexpr uint32_t start_height = 963888;
    constexpr uint32_t stale_report = 963887;   // what a queued message claimed
    constexpr uint32_t current = 951146;        // what the store says now

    // The stale value on its own WOULD have admitted, which is what makes this
    // worth pinning rather than assuming.
    REQUIRE(may_start_slow_sync(start_height, built_at(stale_report))
        == slow_sync_admission::start);

    // And the current one holds. The coordinator passes this one: its
    // `try_start_slow_sync` takes no arguments, so the message's height cannot
    // reach the decision even by accident.
    CHECK(may_start_slow_sync(start_height, built_at(current))
        == slow_sync_admission::builder_behind);

    // Nor can a stale wake-up survive the store becoming unreadable afterwards.
    CHECK(may_start_slow_sync(start_height, unreadable(result_code::recovery_required))
        == slow_sync_admission::height_unavailable);
}
