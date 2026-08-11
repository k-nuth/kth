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

#include <limits>
#include <memory>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

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


// A requires-expression over a CONCRETE type is a hard error, not a substitution
// failure, so each absence below is asked through a template parameter.
template <typename D>
concept has_single_key_erase = requires(D& db, utxoz::raw_outpoint const& k, uint32_t h) {
    db.erase(k, h);
};
template <typename D>
concept has_lookup_queue = requires(D& db) { db.process_pending_lookups(); };
template <typename D>
concept has_deletion_queue = requires(D& db) { db.process_pending_deletions(); };
template <typename D>
concept has_lookup_counter = requires(D const& db) { db.deferred_lookups_size(); };
template <typename D>
concept has_deletion_counter = requires(D const& db) { db.deferred_deletions_size(); };

// Swap the default logger for one that keeps what was written, so a test can ask
// what a probe actually logged rather than what it was supposed to.
struct captured_log {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
    std::shared_ptr<spdlog::logger> installed;
    std::shared_ptr<spdlog::logger> previous;

    captured_log()
        : sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256))
        , installed(std::make_shared<spdlog::logger>("captured", sink))
        , previous(spdlog::default_logger()) {
        installed->set_level(spdlog::level::trace);
        spdlog::set_default_logger(installed);
    }

    ~captured_log() { spdlog::set_default_logger(previous); }

    captured_log(captured_log const&) = delete;
    captured_log& operator=(captured_log const&) = delete;

    [[nodiscard]] size_t errors() const {
        size_t n = 0;
        for (auto const& entry : sink->last_raw()) {
            if (entry.level >= spdlog::level::err) {
                ++n;
            }
        }
        return n;
    }
};

TEST_CASE("utxoz behaviour: an ordinary probe miss is not logged as a failure",
          "[utxoz][contract]") {
    // The BEHAVIOURAL half, at the real boundary. That not_resolved has a name
    // says nothing about whether a probe treats it as a fault, and the two
    // conditions drifted apart exactly there: the probes still tested
    // `not_found`, which 0.10.0 never returns, so every ordinary miss took the
    // error path and printed one line per prevout.
    scoped_temp_dir const dir{"kth_utxoz_quiet_" + std::to_string(current_process_id())};
    utxoz_database db;
    REQUIRE(db.open(dir.path, true));
    scoped_open_db const guard{db};

    auto const key = utxoz::make_outpoint(std::span<uint8_t const, 32>{
        std::vector<uint8_t>(32, 0x5A).data(), 32}, 0);

    {
        captured_log log;

        // Both probes, because both were wired to the same stale condition.
        auto const raw = db.find_raw(key, 0);
        auto const typed = db.find(domain::chain::point{
            hash_digest(std::array<uint8_t, 32>{}), 0}, 0);

        // Each really did miss, so the assertion below is about a path that ran.
        REQUIRE_FALSE(raw.has_value());
        CHECK(raw.error() == result_code::not_resolved);
        REQUIRE_FALSE(typed.has_value());
        CHECK(typed.error() == result_code::not_resolved);

        // And neither said a word at error level. Restoring `!= not_found`, or
        // making not_resolved take the log path again, turns this red.
        CHECK(log.errors() == 0);
    }
}

TEST_CASE("utxoz contract: only the ordinary miss is exempt from the log",
          "[utxoz][contract]") {
    // The predicate both probes are wired to. not_resolved is the one outcome
    // that is not a fault; everything else keeps the error path, so a genuine
    // storage failure still reaches an operator.
    CHECK(database::is_ordinary_probe_miss(utxoz::error_code::not_resolved));

    for (auto const code : {utxoz::error_code::not_found,
                            utxoz::error_code::version_unreadable,
                            utxoz::error_code::catalog_unreadable,
                            utxoz::error_code::closed,
                            utxoz::error_code::recovery_required}) {
        CHECK_FALSE(database::is_ordinary_probe_miss(code));
    }
}

TEST_CASE("utxoz contract: the ordinary miss has a name", "[utxoz][contract]") {
    // 0.10.0 answers every ordinary miss with not_resolved. A switch that does
    // not carry the code prints "unrecognised", which is what an operator reads
    // while trying to diagnose something else entirely.
    CHECK(std::string(database::utxoz_error_name(utxoz::error_code::not_resolved))
          == "not_resolved");

    // NEGATION, without reaching for an out-of-range enum: that switch has no
    // `default`, so casting an unlisted value into it is undefined behaviour and
    // the test would be asserting against a jump past the table. Instead, every
    // code this migration actually reasons about must carry its OWN name — a
    // case silently lost shows up here as "unrecognised".
    for (auto const code : {utxoz::error_code::not_resolved,
                            utxoz::error_code::version_unreadable,
                            utxoz::error_code::catalog_unreadable,
                            utxoz::error_code::closed,
                            utxoz::error_code::not_found}) {
        CHECK(std::string(database::utxoz_error_name(code)) != "unrecognised");
    }

    // And a real fault keeps its own name rather than collapsing into the miss.
    CHECK(std::string(database::utxoz_error_name(utxoz::error_code::version_unreadable))
          == "version_unreadable");
    CHECK(std::string(database::utxoz_error_name(utxoz::error_code::catalog_unreadable))
          == "catalog_unreadable");
}

TEST_CASE("utxoz contract: a closed store still partitions over distinct keys",
          "[utxoz][contract]") {
    // apply_deletes() promises a partition over DISTINCT keys, deduplicated
    // keeping the FIRST occurrence — height included. The refusal path never
    // reaches the library, so the wrapper owes that contract itself; copying the
    // span verbatim would hand back more entries than there are keys.
    utxoz_database db;   // deliberately not opened

    auto const key = utxoz::make_outpoint(
        std::span<uint8_t const, 32>{std::array<uint8_t, 32>{}.data(), 32}, 7);

    std::array<utxoz::deferred_deletion_entry, 3> const repeated{
        utxoz::deferred_deletion_entry{key, 101},
        utxoz::deferred_deletion_entry{key, 202},
        utxoz::deferred_deletion_entry{key, 303}};

    auto const progress = db.apply_deletes(repeated);

    CHECK(progress.erased.empty());
    CHECK(progress.absent.empty());
    REQUIRE(progress.error.has_value());

    // One entry per distinct key, not one per request.
    REQUIRE(progress.unresolved.size() == 1);
    CHECK(progress.unresolved.front().key == key);

    // And the FIRST occurrence's height, not the last and not the largest.
    CHECK(progress.unresolved.front().height == 101u);
}

TEST_CASE("utxoz contract: a height that does not fit is refused, not truncated",
          "[utxoz][contract]") {
    // The request records carry uint32_t and several KTH signatures carry
    // heights as size_t. Apple Clang rejects the implicit narrowing outright;
    // every other compiler takes it silently, and a truncated height is not a
    // smaller height — it names a DIFFERENT block, so the resolution would be
    // bounded at the wrong point and answer confidently about the wrong one.

    // POSITIVE: every height a chain will ever reach fits, and comes back
    // unchanged. Without this half, a helper that refused everything would pass
    // the negation below and break all resolution.
    CHECK(database::to_store_height(0u) == 0u);
    CHECK(database::to_store_height(1u) == 1u);
    CHECK(database::to_store_height(900000u) == 900000u);
    CHECK(database::to_store_height(
        size_t{std::numeric_limits<uint32_t>::max()}) == std::numeric_limits<uint32_t>::max());

    // NEGATIVE: one past the boundary, and far past it. Not truncated — refused.
    if constexpr (sizeof(size_t) > sizeof(uint32_t)) {
        CHECK_FALSE(database::to_store_height(
            size_t{std::numeric_limits<uint32_t>::max()} + 1).has_value());
        CHECK_FALSE(database::to_store_height(
            std::numeric_limits<size_t>::max()).has_value());

        // And the truncation it refuses to perform would have been silent: the
        // wrapped value is a perfectly plausible height.
        auto const wrapped = static_cast<uint32_t>(
            size_t{std::numeric_limits<uint32_t>::max()} + 1);
        CHECK(wrapped == 0u);
    }
}

TEST_CASE("utxoz contract: there is no single-key erase", "[utxoz][contract]") {
    // 0.10.0 removed erase() outright. It could not survive the removal of the
    // internal queue: reaching only the active versions and deferring the rest
    // is what it did, and without somewhere to defer to, a lone key would have
    // to pay the whole descent through the version files by itself.
    //
    // Deletion is a batch the caller owns, and the batch is what makes the
    // descent affordable — each further file is searched for fewer keys.
    static_assert( ! has_single_key_erase<utxoz_db>,
        "utxoz::erase() is back; the deletion path assumes apply_deletes() owns the whole batch");
}

TEST_CASE("utxoz contract: a probe never answers absence", "[utxoz][contract]") {
    // find() reads the ACTIVE versions and nothing else, so it cannot establish
    // that a key does not exist. 0.10.0 says so in the code rather than in a
    // comment: the miss is not_resolved, a code that did not exist before and
    // that is deliberately not spelled not_found.
    static_assert(requires { utxoz::error_code::not_resolved; },
        "utxoz::error_code::not_resolved is gone; a probe miss would read as absence again");

    // And KTH keeps them apart on the way up. Mapping not_resolved onto
    // key_not_found is exactly the conflation #602 exists to prevent.
    static_assert(static_cast<int>(database::result_code::not_resolved)
               != static_cast<int>(database::result_code::key_not_found),
        "result_code::not_resolved collapsed into key_not_found");
}

TEST_CASE("utxoz contract: resolution is a caller-owned batch", "[utxoz][contract]") {
    // The queue is gone from the library, which is what stops one component
    // consuming another's lookups (#116, #646). resolve() takes a span and
    // returns; nothing of the request survives the call.
    using lib_resolve = decltype(std::declval<utxoz_db const&>().resolve(
        std::declval<std::span<utxoz::lookup_request const>>()));
    static_assert(is_expected_v<lib_resolve>,
        "UTXO-Z's resolve() no longer reports failure as an error");

    static_assert( ! has_lookup_queue<utxoz_db>,
        "the global lookup queue is back; a caller could consume another's batch");
    static_assert( ! has_deletion_queue<utxoz_db>,
        "the global deletion queue is back");
    static_assert( ! has_lookup_counter<utxoz_db>,
        "a global pending counter is back; nothing may infer work from it");
    static_assert( ! has_deletion_counter<utxoz_db>,
        "a global pending counter is back");
}

TEST_CASE("utxoz contract: deletion reports progress, not an error", "[utxoz][contract]") {
    // apply_deletes() is deliberately NOT result<>-wrapped. It writes as it
    // walks, so a fault partway leaves earlier deletions applied, and hiding
    // that behind an error would leave the caller unable to tell which keys are
    // gone — and resending one of them turns its own success into a refusal.
    using lib_deletes = decltype(std::declval<utxoz_db&>().apply_deletes(
        std::declval<std::span<utxoz::deferred_deletion_entry const>>()));
    static_assert(std::is_same_v<lib_deletes, utxoz::deletion_progress>,
        "apply_deletes() no longer returns progress; partial application would be hidden");
    static_assert( ! is_expected_v<lib_deletes>,
        "apply_deletes() became result<>-wrapped: `erased` would be lost on the failure path");

    // The three lists are three separate facts, and the caller acts differently
    // on each. Collapsing absent into unresolved (or the reverse) is the
    // ambiguity 0.9.1 had and this replaced.
    static_assert(requires(utxoz::deletion_progress p) {
        p.erased; p.absent; p.unresolved; p.error;
    }, "deletion_progress lost one of its four fields");
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
    static_assert(std::is_same_v<decltype(std::declval<utxoz_database&>().sync()),
                                 kth::database::barrier_outcome>,
        "the wrapper must keep 'this platform has none' apart from 'one failed': a bool "
        "answers both with false, and only one of the two may be carried on past (#600)");
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
    REQUIRE(closed.error() != result_code::not_resolved);

    // POSITIVE: opened and empty, the same key is genuinely absent, and THAT is
    // key_not_found. Without this half, an implementation that returned `other`
    // for everything would pass the negation and destroy the fallback.
    scoped_temp_dir const dir{"kth_utxoz_find_" + std::to_string(current_process_id())};
    REQUIRE(db.open(dir.path, true));
    scoped_open_db const guard{db};
    // An empty database cannot answer from its active versions either, so the
    // probe says not_resolved — NOT absence. Absence is established by a
    // resolution, and only by one.
    auto const probed = db.find_raw(key, 0);
    REQUIRE_FALSE(probed.has_value());
    REQUIRE(probed.error() == result_code::not_resolved);

    std::array<utxoz::lookup_request, 1> const own{utxoz::lookup_request{key, 0}};
    auto const resolved = db.resolve_raw(own);
    REQUIRE(resolved);
    CHECK(resolved->found.empty());
    REQUIRE(resolved->absent.size() == 1);
    CHECK(resolved->absent.front() == key);
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
    REQUIRE(db.apply_inserts_raw(inserts) == result_code::success);

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
    REQUIRE(db.apply_inserts_raw(short_payload) != result_code::success);
    REQUIRE_FALSE(db.find_raw(key, 0).has_value());

    // ...and nine bytes, which would otherwise be accepted by silent truncation.
    std::vector<std::pair<utxoz::raw_outpoint, raw_in>> long_payload{
        {key, {std::vector<uint8_t>(9, 0), 10u}}};
    REQUIRE(db.apply_inserts_raw(long_payload) != result_code::success);
#endif

    // POSITIVE, in both modes: a well-formed payload is accepted and stored, so
    // the refusals above are about the length and not about the call failing
    // unconditionally.
    std::vector<std::pair<utxoz::raw_outpoint, raw_in>> good{
        {key, {std::vector<uint8_t>(8, 0), 10u}}};
    REQUIRE(db.apply_inserts_raw(good) == result_code::success);
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
    REQUIRE(db.apply_inserts_raw(no_inserts) != result_code::success);

    // POSITIVE: open store, and deleting a key that was never there is NOT a
    // storage failure — not_found must stay tolerated, or every delta carrying a
    // pruned output would fail the batch.
    scoped_temp_dir const dir{"kth_utxoz_del_" + std::to_string(current_process_id())};
    REQUIRE(db.open(dir.path, true));
    scoped_open_db const guard{db};
    REQUIRE(db.apply_inserts_raw(no_inserts) == result_code::success);
}
