// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// A fatal condition is reported with the UTXO window already released (#657)
// =============================================================================
//
// The connect path used to log and call on_fatal from inside the exclusive
// window. Both are external execution: spdlog's production sinks are synchronous
// with flush_on(info), so the write reaches the disk before the call returns, and
// on_fatal stops the node and joins its pools. Every reader stayed shut out for
// the duration of both, and a handler that reached for the UTXO store at all
// would have been waiting for the window its own caller was holding.
//
// What this file asserts is the ordering, on the real connect path with a real
// failure — not on a mock of it. The failure is staged the way the atomicity
// suite already stages one: an output block 1 creates is put into the set first,
// so UTXO-Z reports the second insert as a duplicate key and the batch stops at
// step 4, after the record was written and after the mutation began.
//
// A watchdog runs throughout. The defect this replaces would appear as a hang
// rather than a wrong value, so a test that could hang would be a test that never
// reports.

namespace {

// Fails the test from a background thread if the body does not finish in time.
//
// The condition under test is "nothing waits for the window", and its failure
// mode is a wait that never ends. A deadline is therefore part of the assertion
// rather than a convenience — and it is enforced by RAII so it is lifted on
// every exit, an assertion failure and an exception included.
class watchdog {
public:
    watchdog(std::chrono::seconds budget, std::string what)
        : what_(std::move(what)),
          thread_([this, budget] {
              std::unique_lock<std::mutex> lock(mutex_);
              if ( ! done_.wait_for(lock, budget, [this] { return finished_; })) {
                  // Not FAIL(): Catch2's assertion macros are not safe from a
                  // thread it does not know about. Aborting is the honest
                  // outcome for a deadlock — the process is stuck, and a report
                  // that never arrives is worse than a loud stop.
                  std::fprintf(stderr, "watchdog: %s did not finish in time\\n", what_.c_str());
                  std::fflush(stderr);
                  std::abort();
              }
          }) {}

    watchdog(watchdog const&) = delete;
    watchdog& operator=(watchdog const&) = delete;

    ~watchdog() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
        done_.notify_all();
        thread_.join();
    }

private:
    std::string what_;
    std::mutex mutex_;
    std::condition_variable done_;
    bool finished_{false};
    std::thread thread_;
};

// What the handler observed about the gate at the moment it ran.
struct handler_view {
    int calls{0};
    bool read_refused{false};
    database::result_code read_code{database::result_code::success};
    std::chrono::milliseconds read_elapsed{0};
    std::optional<uint32_t> built_height_at_fatal;
};

// Stage one UTXO directly, following the same window protocol production does.
//
// Declared before the insert and completed after it. The insert here is a real
// mutation: a window left unmarked would apply it and then release without
// latching, so a failure in this helper would leave the gate open over a set it
// had already touched — the exact shape the rest of this file is about. The test
// would still pass, which is why it is worth getting right in a helper rather
// than only in the code under test.
//
// No barrier is taken, and none is owed: nothing here has a second store to
// agree with. Completion is the successful insert.
void insert_raw(blockchain::block_chain& chain, utxoz::raw_outpoint const& key, uint32_t height) {
    blockchain::utxo_raw_delta delta;
    delta.inserts.emplace(key, blockchain::utxo_raw_value{
        std::vector<uint8_t>(8, 0x11), height});

    auto window = chain.begin_utxo_write();
    REQUIRE(window);

    window->mark_mutating();
    REQUIRE(chain.apply_utxo_inserts_raw(*window, delta.inserts)
            == database::result_code::success);
    window->mark_mutated();
    window->complete();
}

} // namespace

TEST_CASE("a fatal connect exit runs with the UTXO window already released",
          "[node][utxo][fatal_window]") {
    // 150s, and the number is derived rather than picked. The run itself takes
    // under a second (834 ms in release, 1.1 s under ASAN), so almost all of this
    // is headroom for a different reason: run_connect_tasks gives the io_context
    // its own 120 s budget, and a run that stalls without deadlocking must be
    // allowed to hit THAT deadline and fail through Catch2 with a readable
    // assertion. Aborting first would replace a diagnosis with a stack trace.
    //
    // So this fires only when the process cannot make progress at all — which is
    // the deadlock it exists for, and nothing else.
    watchdog guard(std::chrono::seconds(150), "the connect run and its fatal handler");

    chain_fixture fixture("fatal_outside_window");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 40 * block_spacing;
    auto const blk = mine_block(genesis.hash(), 1, base_time + block_spacing, 0, {}, 0);
    std::vector<domain::chain::block> const blocks{blk};

    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == 1);
    persist_headers(fixture, blocks, 1);

    // The clash. From here the batch will record its transition, begin mutating,
    // and stop on the duplicate — which is a failure AFTER the mutation started,
    // which is the only kind this test is about.
    auto const& coinbase = blk.transactions().front();
    auto const txid = coinbase.hash();
    auto const clash = utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, 0);
    insert_raw(chain, clash, 1);

    handler_view observed;
    std::vector<std::string> reasons;

    run_connect_tasks(fixture, blocks, 1, [&](std::string const& reason) {
        ++observed.calls;
        reasons.push_back(reason);

        // THE ASSERTION THIS FILE EXISTS FOR. If the window were still held by
        // the thread calling us, this read would block until it was released —
        // which is never, because that thread is inside this call. It returns,
        // and it returns a refusal rather than a lease: the gate latched as the
        // window died, and it latched BEFORE anyone was let back in.
        auto const started = std::chrono::steady_clock::now();
        auto const count = chain.utxo_count();
        observed.read_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        observed.read_refused = ! count.has_value();
        if ( ! count) {
            observed.read_code = count.error();
        }

        // Read from the handler too: a marker that moved would mean the batch
        // published something after the failure.
        if (auto const h = chain.get_utxo_built_height(); h) {
            observed.built_height_at_fatal = *h;
        }
    });

    REQUIRE(observed.calls == 1);
    REQUIRE(reasons.size() == 1);
    CHECK(reasons.front() == "a UTXO delta could not be applied");

    // Refused, not served, and not waited for.
    CHECK(observed.read_refused);
    CHECK(observed.read_code == database::result_code::recovery_required);

    // "Without touching the store" is what the code shape gives — the gate
    // answers before any callback reaches the database — and the time is the
    // observable side of it. A generous bound: this is here to catch a wait on
    // the window, which would never end, not to measure anything.
    CHECK(observed.read_elapsed < std::chrono::seconds(5));

    // Nothing was published after the failure. The height is where it was, and
    // it reads the same from inside the handler and after the run.
    CHECK(( ! observed.built_height_at_fatal || *observed.built_height_at_fatal == 0u));
    auto const built = chain.get_utxo_built_height();
    CHECK(( ! built || *built == 0u));

    // The record is intact and still says what it said: this batch was in
    // flight. Deferring the report changed nothing about what reaches the disk.
    auto const check = chain.read_transition_record();
    REQUIRE(check.status == database::transition_status::recovery_required);
    REQUIRE(check.record.has_value());
    CHECK(check.record->type == database::transition_type::connect_batch);
    CHECK(check.record->first_height == 1u);

    // No further mutation is even possible: every subsequent operation is
    // refused, so a caller that tried would be told rather than served.
    CHECK_FALSE(chain.utxo_count().has_value());
    CHECK_FALSE(chain.compact_utxo().has_value());
    CHECK(chain.compact_utxo().error() == database::result_code::recovery_required);

    // And the administrative close still completes on a latched gate. Under the
    // watchdog: a close that waited for a window nobody holds would hang here,
    // which is the failure this whole change is about, one layer down.
    CHECK(chain.close());

    // The next start refuses, exactly as before — the record decides that, and
    // this PR does not touch it.
    CHECK_FALSE(fixture.restart());
}
