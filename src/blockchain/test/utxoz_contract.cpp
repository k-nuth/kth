// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Negative controls for the UTXO-Z 0.9.0 migration.
//
// Renaming `compact` to `reference` is the visible half of that bump and the
// harmless one: get it wrong and nothing compiles. The dangerous half is the
// set of functions that changed what they RETURN. Four of them started
// returning `result<>`, three of those broke the build and were therefore
// found, and one — `compact_all()`, which used to return void — did not. A
// caller that kept invoking it as a statement compiled exactly as before and
// silently discarded every failure.
//
// That is the failure this file exists to prevent, so the tests below assert
// the SHAPE of the contract rather than the behaviour of a database. They are
// static assertions on purpose: a runtime test can only fail once the wrong
// call has already been written and executed, whereas these fail while the
// wrong call is being written.

#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
#include <type_traits>
#include <utility>

#include <test_helpers.hpp>

#include <expected>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/utxo_builder.hpp>
#include <kth/database/databases/utxoz_database.hpp>

using namespace kth;
using namespace kth::database;
using namespace kth::blockchain;

namespace {

// `std::expected<T, E>` detector. Written out rather than assumed, because the
// whole point of these assertions is that "returns a value" and "returns a
// value or an error" are different contracts.
template <typename T> struct is_expected : std::false_type {};
template <typename T, typename E> struct is_expected<std::expected<T, E>> : std::true_type {};
// A REQUIRE that fails throws, so anything after it never runs. These tests open
// databases and create directories; without this the first failure would leak an
// open handle and leave a temporary tree behind, and the NEXT run would then fail
// for a different reason than the one being investigated.
struct scoped_temp_dir {
    std::filesystem::path path;
    explicit scoped_temp_dir(std::string_view tag)
        : path(std::filesystem::temp_directory_path() / std::string(tag)) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~scoped_temp_dir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    scoped_temp_dir(scoped_temp_dir const&) = delete;
    scoped_temp_dir& operator=(scoped_temp_dir const&) = delete;
};

struct scoped_open_db {
    utxoz_database& db;
    ~scoped_open_db() { db.close(); }
};

inline int current_process_id() {
#if defined(_WIN32)
    return ::_getpid();
#else
    return static_cast<int>(::getpid());
#endif
}

template <typename T> inline constexpr bool is_expected_v = is_expected<std::remove_cvref_t<T>>::value;


#ifdef KTH_UTXOZ_REFERENCE_MODE
using utxoz_db = utxoz::reference_db;
#else
using utxoz_db = utxoz::full_db;
#endif

// The mode this build selected must be the mode the wrapper reports. These are
// set in two CMake lists and read in a third place, so they can disagree.
static_assert(
#ifdef KTH_UTXOZ_REFERENCE_MODE
    std::is_same_v<utxoz_db, utxoz::reference_db>,
#else
    std::is_same_v<utxoz_db, utxoz::full_db>,
#endif
    "the storage mode selected by the build is not the one this translation unit sees");

} // namespace

// ---------------------------------------------------------------------------
// The contract changes that do NOT break a naive migration.
// ---------------------------------------------------------------------------

TEST_CASE("utxoz contract: compact_all reports failure instead of returning void", "[utxoz][contract]") {
    // THE CONTROL. In 0.8.0 this was `void compact_all()`. Had the migration
    // kept calling it as a bare statement, everything would have compiled and
    // every compaction failure would have been discarded. If a future version
    // makes it void again — or if someone "simplifies" the wrapper back — this
    // assertion is what notices.
    using compact_result = decltype(std::declval<utxoz_db&>().compact_all());
    static_assert( ! std::is_void_v<compact_result>,
        "compact_all() returns void again: a failure would now be discarded silently");
    static_assert(std::is_same_v<compact_result, utxoz::result<>>,
        "compact_all() no longer returns result<>; the wrapper's error handling is dead code");

    // And the wrapper must pass that answer on rather than swallowing it.
    static_assert(std::is_same_v<decltype(std::declval<utxoz_database&>().compact()), bool>,
        "utxoz_database::compact() must report whether compaction happened");

    // ...and the report has to survive the trip up. A bool at the wrapper with a
    // void above it is the same defect one layer higher: every caller of
    // block_chain::utxo_compact() would be back to the 0.8.0 contract, in which
    // compaction could not fail.
    static_assert(std::is_same_v<decltype(std::declval<blockchain::block_chain&>().utxo_compact()), bool>,
        "block_chain::utxo_compact() returns void: the failure dies between the "
        "wrapper and every caller");
}

TEST_CASE("utxoz contract: erase distinguishes failure from absence", "[utxoz][contract]") {
    // `erase()` went from size_t to result<size_t>. The tempting migration is
    // `*erased > 0 ? success : key_not_found`, which quietly turns "the erase
    // failed" into "the key was not there" — and the entry may well still exist
    // and still be spendable.
    using erase_result = decltype(std::declval<utxoz_db&>().erase(
        std::declval<utxoz::raw_outpoint const&>(), uint32_t{}));
    static_assert(std::is_same_v<erase_result, utxoz::result<size_t>>,
        "erase() no longer returns result<size_t>; revisit the failure-vs-absence split");
    static_assert( ! std::is_integral_v<erase_result>,
        "erase() returns a bare count again: a failure would read as a missing key");
}

TEST_CASE("utxoz contract: draining a queue can fail as a whole", "[utxoz][contract]") {
    // Both drains are result<>-wrapped since 0.9.0. An empty result has to keep
    // meaning "nothing was pending"; a drain that could not run must not borrow
    // that meaning, because every caller upstream reads absence as "spent".
    //
    // The library's own types are the easy half. What matters is that KNUTH
    // does not unwrap them back into a sentinel on the way up, so each layer is
    // pinned below — utxoz_database, block_chain, and the shape the builder
    // template requires of whatever it is given.
    using lib_lookups = decltype(std::declval<utxoz_db&>().process_pending_lookups());
    using lib_deletions = decltype(std::declval<utxoz_db&>().process_pending_deletions());
    static_assert(is_expected_v<lib_lookups>,
        "UTXO-Z's process_pending_lookups() no longer reports failure");
    static_assert(is_expected_v<lib_deletions>,
        "UTXO-Z's process_pending_deletions() no longer reports failure");
}

TEST_CASE("utxoz contract: knuth does not unwrap the drains into sentinels", "[utxoz][contract]") {
    // THE BLOCKER THIS FILE MISSED THE FIRST TIME. Every one of these compiled
    // happily while the wrapper logged the error and returned an empty pair,
    // which is precisely "could not drain" reported as "there was nothing".

    // Layer 1: the database wrapper.
    using db_lookups = decltype(std::declval<utxoz_database&>().process_pending_lookups());
    using db_lookups_raw = decltype(std::declval<utxoz_database&>().process_pending_lookups_raw());
    using db_deletions = decltype(std::declval<utxoz_database&>().process_pending_deletions());
    static_assert(is_expected_v<db_lookups>,
        "utxoz_database::process_pending_lookups() returns a bare pair: a failed drain "
        "is indistinguishable from an empty queue");
    static_assert(is_expected_v<db_lookups_raw>,
        "utxoz_database::process_pending_lookups_raw() returns a bare pair");
    static_assert(is_expected_v<db_deletions>,
        "utxoz_database::process_pending_deletions() returns a bare pair");

    // Layer 2: the chain interface every consumer actually calls.
    using chain_lookups = decltype(std::declval<blockchain::block_chain&>().utxo_process_pending_lookups());
    using chain_lookups_raw = decltype(std::declval<blockchain::block_chain&>().utxo_process_pending_lookups_raw());
    using chain_deletions = decltype(std::declval<blockchain::block_chain&>().utxo_process_pending_deletions());
    static_assert(is_expected_v<chain_lookups>,
        "block_chain::utxo_process_pending_lookups() drops the failure on the way up");
    static_assert(is_expected_v<chain_lookups_raw>,
        "block_chain::utxo_process_pending_lookups_raw() drops the failure on the way up");
    static_assert(is_expected_v<chain_deletions>,
        "block_chain::utxo_process_pending_deletions() drops the failure on the way up");

    // ...and the payload each one carries is mode-dependent, so a check written
    // for full mode cannot pass vacuously in reference mode or the reverse.
    using resolved_map = boost::unordered_flat_map<utxoz::raw_outpoint, database::utxo_entry>;
    static_assert(std::is_same_v<typename chain_lookups::value_type,
                                 std::pair<resolved_map, std::vector<utxoz::raw_outpoint>>>,
        "the resolved-lookup payload changed shape");
}

TEST_CASE("utxoz contract: this target and the database agree on the mode", "[utxoz][contract]") {
    // The first version of this test asked the macro what the macro said. The
    // alias below and any static_assert about it both come from
    // KTH_UTXOZ_REFERENCE_MODE as seen HERE, so they agree by construction and
    // could never catch the thing worth catching: the option is declared in two
    // CMake lists, and a build can end up with the database library compiled one
    // way and blockchain the other. That produces a wrapper whose db_ member is
    // one type on one side of the ABI and another on the other.
    //
    // utxoz_reference_mode() is compiled INTO the database library, so asking it
    // at runtime compares two independently compiled answers.
    bool const database_says = utxoz_reference_mode();
#ifdef KTH_UTXOZ_REFERENCE_MODE
    bool const this_target_says = true;
#else
    bool const this_target_says = false;
#endif
    INFO("database library reports " << (database_says ? "reference" : "full")
         << ", this target was compiled as " << (this_target_says ? "reference" : "full"));
    REQUIRE(database_says == this_target_says);

    // Given they agree, the selected mode must also be the one whose find()
    // shape is compiled in — this part is a type check, and it is only
    // meaningful because the runtime comparison above passed.
    using found_type = typename decltype(std::declval<utxoz_db&>().find(
        std::declval<utxoz::raw_outpoint const&>(), uint32_t{}))::value_type;
    if (this_target_says) {
        REQUIRE(std::is_same_v<found_type, utxoz::reference_find_result>);
    } else {
        REQUIRE(std::is_same_v<found_type, utxoz::full_find_result>);
    }
}

// ---------------------------------------------------------------------------
// The durability contract, which is new rather than changed.
// ---------------------------------------------------------------------------

TEST_CASE("utxoz contract: sync is a separate act from close", "[utxoz][contract]") {
    // close() does not sync. If a future version made close() durable, the
    // wrappers that call sync() first would become redundant — and, more to the
    // point, code that forgot to would start working by accident, which is the
    // kind of thing that should be noticed deliberately.
    static_assert(std::is_same_v<decltype(std::declval<utxoz_db&>().sync()), utxoz::result<>>,
        "sync() must report what it achieved; a void sync promises what it cannot");
    static_assert(std::is_same_v<decltype(std::declval<utxoz_database&>().sync()), bool>,
        "the wrapper must report whether a barrier was actually reached");
}

TEST_CASE("utxoz contract: platform durability is askable, not assumed", "[utxoz][contract]") {
    static_assert(std::is_same_v<decltype(utxoz::platform_durability()), utxoz::durability_level>,
        "platform_durability() no longer answers with a durability_level");

    // Three distinct answers, and the middle one is the trap: under
    // contents_only a successful sync() means the entries reached the disk and
    // the directory entries naming them did not.
    auto const level = utxoz_platform_durability();
    bool const known = level == utxoz::durability_level::full
                    || level == utxoz::durability_level::contents_only
                    || level == utxoz::durability_level::none;
    REQUIRE(known);

    // Not a tautology: on the platforms KTH ships, a barrier exists. If this
    // ever reports `none` on a real build, every durability claim above it is
    // worth nothing and we want to hear about it here rather than after a crash.
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    REQUIRE(level != utxoz::durability_level::none);
#endif
}

// ---------------------------------------------------------------------------
// The exclusive claim, which is new behaviour at open().
// ---------------------------------------------------------------------------

TEST_CASE("utxoz contract: a second opener is refused, and says which refusal it is", "[utxoz][contract]") {
    // 0.9.0 claims the database exclusively. Two error codes cover it and they
    // are NOT interchangeable: `database_in_use` means someone else holds it,
    // `database_lock_unavailable` means the claim could not be attempted at
    // all. Collapsing them sends an operator to the wrong place.
    static_assert(static_cast<int>(utxoz::error_code::database_in_use)
               != static_cast<int>(utxoz::error_code::database_lock_unavailable),
        "the two claim failures must stay distinct");

    scoped_temp_dir const dir{"kth_utxoz_claim_" + std::to_string(current_process_id())};

    auto first = utxoz_db::open_for_testing(dir.path.string(), true);
    REQUIRE(first.has_value());

    // The claim is held. A second open of the same path must fail rather than
    // hand out a second writer — the whole reason KTH can stop policing this
    // itself is that the library now does.
    auto second = utxoz_db::open_for_testing(dir.path.string(), false);
    REQUIRE_FALSE(second.has_value());
    REQUIRE(second.error() == utxoz::error_code::database_in_use);

    first->close();

    // ...and once released, the path opens again. Without this half, a library
    // that refused every open would pass the assertion above.
    auto third = utxoz_db::open_for_testing(dir.path.string(), false);
    REQUIRE(third.has_value());
    third->close();
}

// ---------------------------------------------------------------------------
// Behavioural controls. The assertions above pin the SHAPE of the contracts;
// these exercise the branch and check what actually happens, which is the only
// way to catch a signature that is right and a body that is wrong.
// ---------------------------------------------------------------------------

TEST_CASE("utxoz behaviour: a negative file number fails in reference mode only", "[utxoz][contract]") {
    // The field exists to build an 8-byte reference, and only reference mode
    // builds one. A check that fired in both modes would refuse perfectly good
    // full-mode work over a parameter that mode never reads; a check that fired
    // in neither would write UINT32_MAX into every reference of the block.
    utxo_compact_block block;
    block.outputs.push_back({});

    auto const negative = process_compact_block_utxos(
        block, /*height*/ 500u, /*mtp*/ 10u, /*file*/ int16_t{-1}, /*data_pos*/ 0u, nullptr);

#ifdef KTH_UTXOZ_REFERENCE_MODE
    REQUIRE_FALSE(negative.has_value());
#else
    // Full mode stores the whole output; the file number is never read, so the
    // block is buildable and refusing it would be inventing a failure.
    REQUIRE(negative.has_value());
#endif

    // The control that keeps the above from passing by always failing: a valid
    // file number builds in both modes.
    auto const valid = process_compact_block_utxos(
        block, /*height*/ 500u, /*mtp*/ 10u, /*file*/ int16_t{0}, /*data_pos*/ 0u, nullptr);
    REQUIRE(valid.has_value());
}

TEST_CASE("utxoz behaviour: find() failing for a reason other than absence is not absence",
          "[utxoz][contract]") {
    // The distinction that matters upstream: key_not_found lets populate_prevout
    // fall back to the mempool, and every other code must not. A closed database
    // is the cheapest real producer of "not key_not_found".
    utxoz_database db;
    auto const key = utxoz::make_outpoint(std::span<uint8_t const, 32>{
        std::vector<uint8_t>(32, 7).data(), 32}, 0);

    // NEGATION: closed, so the read never happened.
    auto const closed = db.find_raw(key, 0);
    REQUIRE_FALSE(closed.has_value());
    REQUIRE(closed.error() != result_code::key_not_found);

    // POSITIVE: opened and empty, the same key is genuinely absent, and THAT is
    // key_not_found. Without this half, an implementation that returned `other`
    // for everything would pass the negation and destroy the fallback.
    scoped_temp_dir const dir{"kth_utxoz_find_" + std::to_string(current_process_id())};
    REQUIRE(db.open(dir.path, true));
    scoped_open_db const guard{db};
    auto const absent = db.find_raw(key, 0);
    REQUIRE_FALSE(absent.has_value());
    REQUIRE(absent.error() == result_code::key_not_found);
}

TEST_CASE("utxoz behaviour: an entry that cannot be materialised is not absent",
          "[utxoz][contract]") {
    // Reference mode resolves a stored reference by reading the block file. If
    // that read fails, the entry EXISTS and could not be read — reporting it as
    // missing would tell the validator a live UTXO was spent.
    //
    // Full mode reaches the same fork through decoding: bytes that will not
    // parse are corruption, not absence, and must not be silently skipped.
    scoped_temp_dir const dir{"kth_utxoz_mat_" + std::to_string(current_process_id())};

    utxoz_database db;
    REQUIRE(db.open(dir.path, true));
    scoped_open_db const guard{db};

    auto const key = utxoz::make_outpoint(std::span<uint8_t const, 32>{
        std::vector<uint8_t>(32, 9).data(), 32}, 0);

#ifdef KTH_UTXOZ_REFERENCE_MODE
    // A reference pointing at a block file that no store can serve: the resolver
    // has no block_store wired, so materialising must fail.
    std::vector<uint8_t> ref(8, 0);
    struct raw_in { std::vector<uint8_t> data; uint32_t height; };
    std::vector<std::pair<utxoz::raw_outpoint, raw_in>> inserts{{key, {ref, 100u}}};
#else
    // Bytes that are not a serialised utxo_entry.
    std::vector<uint8_t> junk{0xff, 0xff, 0xff};
    struct raw_in { std::vector<uint8_t> data; uint32_t height; };
    std::vector<std::pair<utxoz::raw_outpoint, raw_in>> inserts{{key, {junk, 100u}}};
#endif
    std::vector<std::pair<utxoz::raw_outpoint, uint32_t>> const no_deletes;
    REQUIRE(db.apply_delta_raw(inserts, no_deletes) == result_code::success);

    // NEGATION. This MUST fail in both modes, for the reason each mode has:
    // in reference the stored reference points at a block file no store can
    // serve and no block_store is wired, so materialising it fails; in full the
    // stored bytes are not a serialised entry, so decoding fails. Either way the
    // failure must not be reported as the key being absent — the entry is there.
    //
    // An earlier version wrote this as `if ( ! found)`, which let a fabricated
    // success pass: the one outcome meaning the resolver invented an entry.
    auto const point = utxoz_database::key_to_point(key);
    auto const found = db.find(point, 0);
    REQUIRE_FALSE(found.has_value());
    REQUIRE(found.error() != result_code::key_not_found);

    // POSITIVE: the raw payload is there, so the entry is genuinely stored —
    // this is what makes the negation above about materialisation and not about
    // an empty database.
    auto const raw = db.find_raw(key, 0);
    REQUIRE(raw.has_value());
}

TEST_CASE("utxoz behaviour: a reference payload that is not eight bytes is refused",
          "[utxoz][contract]") {
    // apply_delta_raw reads the reference with two memcpys off a caller-supplied
    // buffer. A short payload would read past its end, and a long one would be
    // silently truncated into a reference pointing somewhere else — both write a
    // UTXO that resolves to the wrong bytes, or to none.
    scoped_temp_dir const dir{"kth_utxoz_len_" + std::to_string(current_process_id())};
    utxoz_database db;
    REQUIRE(db.open(dir.path, true));
    scoped_open_db const guard{db};

    struct raw_in { std::vector<uint8_t> data; uint32_t height; };
    std::vector<std::pair<utxoz::raw_outpoint, uint32_t>> const no_deletes;

    auto const key = utxoz::make_outpoint(std::span<uint8_t const, 32>{
        std::vector<uint8_t>(32, 3).data(), 32}, 0);

#ifdef KTH_UTXOZ_REFERENCE_MODE
    // NEGATION: seven bytes is not a reference, and must be refused BEFORE the
    // reads rather than producing a stored entry.
    std::vector<std::pair<utxoz::raw_outpoint, raw_in>> short_payload{
        {key, {std::vector<uint8_t>(7, 0), 10u}}};
    REQUIRE(db.apply_delta_raw(short_payload, no_deletes) != result_code::success);
    REQUIRE_FALSE(db.find_raw(key, 0).has_value());

    // ...and nine bytes, which would otherwise be accepted by silent truncation.
    std::vector<std::pair<utxoz::raw_outpoint, raw_in>> long_payload{
        {key, {std::vector<uint8_t>(9, 0), 10u}}};
    REQUIRE(db.apply_delta_raw(long_payload, no_deletes) != result_code::success);
#endif

    // POSITIVE, in both modes: a well-formed payload is accepted and stored, so
    // the refusals above are about the length and not about the call failing
    // unconditionally.
    std::vector<std::pair<utxoz::raw_outpoint, raw_in>> good{
        {key, {std::vector<uint8_t>(8, 0), 10u}}};
    REQUIRE(db.apply_delta_raw(good, no_deletes) == result_code::success);
    REQUIRE(db.find_raw(key, 0).has_value());
}

TEST_CASE("utxoz behaviour: a delete on a closed store is not success",
          "[utxoz][contract]") {
    // apply_delta/apply_delta_raw used to discard db_->erase(), so a storage
    // failure ended in result_code::success and the caller recorded the batch as
    // applied while the outputs it spent were still in the set. That discard is
    // fixed in the code.
    //
    // HONEST LIMIT OF THIS TEST. It does NOT exercise that branch. A closed store
    // returns from the is_open() guard before the delete loop is reached, so
    // reverting the erase propagation still passes here — checked by reverting it,
    // and this test did not notice. Making db_->erase() fail on an OPEN store
    // needs a fault-injection seam the wrapper does not have.
    //
    // What it does pin, and both are worth pinning: the closed-store guard, and
    // that not_found stays TOLERATED — without the second half, propagating every
    // erase error would fail any batch carrying an already-pruned output.
    utxoz_database db;
    auto const key = utxoz::make_outpoint(std::span<uint8_t const, 32>{
        std::vector<uint8_t>(32, 4).data(), 32}, 0);

    struct raw_in { std::vector<uint8_t> data; uint32_t height; };
    std::vector<std::pair<utxoz::raw_outpoint, raw_in>> const no_inserts;
    std::vector<std::pair<utxoz::raw_outpoint, uint32_t>> deletes{{key, 10u}};

    // NEGATION: closed store, the erase cannot have happened.
    REQUIRE(db.apply_delta_raw(no_inserts, deletes) != result_code::success);

    // POSITIVE: open store, and deleting a key that was never there is NOT a
    // storage failure — not_found must stay tolerated, or every delta carrying a
    // pruned output would fail the batch.
    scoped_temp_dir const dir{"kth_utxoz_del_" + std::to_string(current_process_id())};
    REQUIRE(db.open(dir.path, true));
    scoped_open_db const guard{db};
    REQUIRE(db.apply_delta_raw(no_inserts, deletes) == result_code::success);
}
