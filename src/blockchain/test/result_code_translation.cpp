// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// What a storage error is allowed to become.
//
// Two conversions meet here and both have a way of going quietly wrong:
//
//   * `storage_failure_to_result_code` decides which UTXO-Z categories survive.
//     The failure mode is silent widening — a code that should have kept its
//     identity arrives as `other`, and the operator is told nothing about what
//     to do. The opposite is worse: an error read as absence makes the validator
//     call an unspent output spent;
//   * `result_code_name` decides what any of it is called. The failure mode is a
//     table that is the right size and the wrong order, which no size assertion
//     can see.
//
// These are exercised as plain values. Making a real database latch to observe
// the mapping would test the library's ability to fail, not this file's choice
// about what the failure means.

#include <array>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include <kth/database/databases/result_code.hpp>
#include <kth/database/databases/utxoz_database.hpp>
#include <kth/database/detail/storage_failure.hpp>

using namespace kth;
using namespace kth::database;

namespace {

// Every code UTXO-Z can report, listed here rather than iterated over a range.
// A range would need the enum's extent, and the only honest way to get that is
// to write the values down — at which point the list is the thing, and casting
// integers back to an enum to walk it is undefined for any value that is not
// one of these.
constexpr std::array<utxoz::error_code, 22> every_utxoz_code{
    utxoz::error_code::not_found,
    utxoz::error_code::not_resolved,
    utxoz::error_code::closed,
    utxoz::error_code::storage_mode_mismatch,
    utxoz::error_code::config_file_corrupt,
    utxoz::error_code::value_too_large,
    utxoz::error_code::duplicate_key,
    utxoz::error_code::catalog_unreadable,
    utxoz::error_code::version_unreadable,
    utxoz::error_code::removal_failed,
    utxoz::error_code::sync_unsupported,
    utxoz::error_code::sync_failed,
    utxoz::error_code::rename_failed,
    utxoz::error_code::database_in_use,
    utxoz::error_code::database_lock_unavailable,
    utxoz::error_code::entropy_unavailable,
    utxoz::error_code::file_open_failed,
    utxoz::error_code::identity_collision,
    utxoz::error_code::insufficient_space,
    utxoz::error_code::recovery_required,
    utxoz::error_code::recovery_failed,
    utxoz::error_code::metadata_write_failed,
};

constexpr std::array<result_code, 11> every_result_code{
    result_code::success,
    result_code::success_duplicate_coinbase,
    result_code::duplicated_key,
    result_code::key_not_found,
    result_code::db_empty,
    result_code::no_data_to_prune,
    result_code::db_corrupt,
    result_code::prune_error,
    result_code::other,
    result_code::not_resolved,
    result_code::recovery_required,
};

static_assert(every_result_code.size() == static_cast<size_t>(result_code::_count),
    "this list has to name every code, or the sweep below silently skips one");

} // namespace

// =============================================================================
// What survives the conversion
// =============================================================================

TEST_CASE("storage translation - not_resolved stays an ordinary miss", "[result_code]") {
    // The one non-error answer a probe of the active versions has. If this
    // widened to `other`, every prevout lookup would look like a storage fault
    // and the batch resolution that exists to answer it would never be reached.
    CHECK(kth::database::detail::storage_failure_to_result_code(utxoz::error_code::not_resolved)
        == result_code::not_resolved);

    // And it is NOT the latch: a miss must never ask for a restart.
    CHECK( ! needs_recovery(kth::database::detail::storage_failure_to_result_code(utxoz::error_code::not_resolved)));
}

TEST_CASE("storage translation - recovery_required keeps its category", "[result_code]") {
    // The regression this exists for. Before, every code other than
    // not_resolved became `other`, so a latched store — which refuses
    // everything until it is closed and reopened — was reported as an
    // unclassified failure, and the one action attached to it was lost.
    CHECK(kth::database::detail::storage_failure_to_result_code(utxoz::error_code::recovery_required)
        == result_code::recovery_required);
    CHECK(needs_recovery(kth::database::detail::storage_failure_to_result_code(utxoz::error_code::recovery_required)));
}

TEST_CASE("storage translation - a closed store is not a latched one", "[result_code]") {
    // Adjacent and different. `closed` is a database this node let go of, or
    // never opened; it is fixed by opening one. `recovery_required` is a
    // database that is open and refusing. Reporting the first as the second
    // would send an operator to rebuild a set that is intact.
    auto const closed = kth::database::detail::storage_failure_to_result_code(utxoz::error_code::closed);
    CHECK(closed == result_code::other);
    CHECK( ! needs_recovery(closed));
}

TEST_CASE("storage translation - read failures do not ask for recovery", "[result_code]") {
    // These say a file could not be read, which is a fact about this run. None
    // of them means the stored state is half-written, and answering
    // recovery_required to any of them would throw away a good database.
    for (auto const code : {utxoz::error_code::catalog_unreadable,
                            utxoz::error_code::version_unreadable,
                            utxoz::error_code::file_open_failed,
                            utxoz::error_code::config_file_corrupt}) {
        CAPTURE(utxoz_error_name(code));
        auto const translated = kth::database::detail::storage_failure_to_result_code(code);
        CHECK(translated == result_code::other);
        CHECK( ! needs_recovery(translated));
    }
}

TEST_CASE("storage translation - recovery_failed is not recovery_required", "[result_code]") {
    // Recovery ran and could not act: the database did not open at all. Telling
    // an operator to close and reopen would send them round a loop that has
    // already been tried.
    auto const failed = kth::database::detail::storage_failure_to_result_code(utxoz::error_code::recovery_failed);
    CHECK(failed == result_code::other);
    CHECK( ! needs_recovery(failed));
}

TEST_CASE("storage translation - no error ever becomes absence", "[result_code]") {
    // The whole point, swept over every code the library has rather than over
    // the ones that came to mind. `key_not_found` is what upstream reads as "not
    // in the set", so an error arriving as that code makes an unspent output
    // look spent — a consensus verdict produced by a disk that would not read.
    for (auto const code : every_utxoz_code) {
        CAPTURE(utxoz_error_name(code));
        auto const translated = kth::database::detail::storage_failure_to_result_code(code);
        CHECK(translated != result_code::key_not_found);
        CHECK( ! succeed(translated));
    }
}

TEST_CASE("storage translation - exactly two codes keep an identity", "[result_code]") {
    // A counted assertion, so widening the mapping later is a decision rather
    // than a drift: anything beyond these two has to change this number and say
    // why in the same commit.
    size_t distinct = 0;
    for (auto const code : every_utxoz_code) {
        if (kth::database::detail::storage_failure_to_result_code(code) != result_code::other) {
            ++distinct;
        }
    }
    CHECK(distinct == 2);
}

// =============================================================================
// What any of it is called
// =============================================================================

TEST_CASE("result_code_name - every code answers with its OWN name", "[result_code]") {
    // Each code paired with the text it must produce. Distinctness alone would
    // not do: a table that is the right size, holds no duplicates and has two
    // entries swapped passes every structural check there is, and then reports
    // one fault under another fault's name. The pairs are what makes a swap
    // visible, and they are written out rather than derived — deriving them from
    // the same table under test would agree with any order it happened to have.
    constexpr std::array<std::pair<result_code, std::string_view>,
                         every_result_code.size()> expected{{
        {result_code::success,                    "success"},
        {result_code::success_duplicate_coinbase, "success_duplicate_coinbase"},
        {result_code::duplicated_key,             "duplicated_key"},
        {result_code::key_not_found,              "key_not_found"},
        {result_code::db_empty,                   "db_empty"},
        {result_code::no_data_to_prune,           "no_data_to_prune"},
        {result_code::db_corrupt,                 "db_corrupt"},
        {result_code::prune_error,                "prune_error"},
        {result_code::other,                      "other"},
        {result_code::not_resolved,               "not_resolved"},
        {result_code::recovery_required,          "recovery_required"},
    }};

    static_assert(expected.size() == static_cast<size_t>(result_code::_count),
        "a code without a pair here is a code this sweep does not check");

    for (auto const& [code, name] : expected) {
        CAPTURE(static_cast<int>(code), name);
        CHECK(std::string_view(result_code_name(code)) == name);
    }
}

TEST_CASE("result_code_name - no two codes share a name", "[result_code]") {
    // The other half. The pairs above catch a wrong name; this catches a table
    // that names two codes the same, which would read as correct in every
    // individual comparison.
    std::array<std::string_view, every_result_code.size()> seen{};
    size_t count = 0;

    for (auto const code : every_result_code) {
        std::string_view const name = result_code_name(code);
        CAPTURE(static_cast<int>(code), name);

        CHECK( ! name.empty());
        CHECK(name != "invalid result_code");

        for (size_t i = 0; i < count; ++i) {
            CHECK(seen[i] != name);
        }
        seen[count++] = name;
    }

    CHECK(count == static_cast<size_t>(result_code::_count));
}

TEST_CASE("result_code_name - an out-of-range value is reported, not named", "[result_code]") {
    // `_count` is the smallest value that is not a code. Reaching it through the
    // enum is defined — it IS an enumerator — whereas casting an arbitrary
    // integer back would not be, which is why this is the value used.
    CHECK(std::string_view(result_code_name(result_code::_count))
        == "invalid result_code");
}

TEST_CASE("result_code - the latch is not a success and not a miss", "[result_code]") {
    // Both predicates already in the header, asked of the new value. A latch
    // that passed `succeed()` would let a caller carry on writing to a store
    // that has stopped accepting writes.
    CHECK( ! succeed(result_code::recovery_required));
    CHECK( ! succeed_prune(result_code::recovery_required));
    CHECK(needs_recovery(result_code::recovery_required));

    // And no OTHER code is the latch.
    for (auto const code : every_result_code) {
        if (code == result_code::recovery_required) {
            continue;
        }
        CAPTURE(result_code_name(code));
        CHECK( ! needs_recovery(code));
    }
}

TEST_CASE("result_code - adding a value did not renumber the old ones", "[result_code]") {
    // The enum is not serialised anywhere today, but it is compared across
    // translation units and stored in `expected` returns that cross library
    // boundaries. Pinning the values makes a reorder a failed test rather than a
    // pair of libraries that disagree about what 9 means.
    CHECK(static_cast<int>(result_code::success) == 0);
    CHECK(static_cast<int>(result_code::key_not_found) == 3);
    CHECK(static_cast<int>(result_code::other) == 8);
    CHECK(static_cast<int>(result_code::not_resolved) == 9);
    CHECK(static_cast<int>(result_code::recovery_required) == 10);
    CHECK(static_cast<int>(result_code::_count) == 11);
}
