// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <array>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/blockchain/utxo_deletion_sweep.hpp>
#include <kth/node/sync/reorg.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::blockchain;
using namespace kth::node::sync;
using namespace kth::test;

// =============================================================================
// A rewind that owes deletions must not be published (#602, UTXO-Z 0.10.0)
// =============================================================================
//
// disconnect_block() applies the inverse of a block's delta: it restores what
// the block spent and erases what the block created. Those erases are not
// applied one at a time — 0.10.0 has no erase() — they accumulate into a batch
// this rewind OWNS and are handed to apply_deletes() once, which descends
// through the version files searching each for fewer keys than the last.
//
// apply_deletes() answers with three lists, and the correctness argument is
// entirely about telling them apart:
//
//   erased      applied. A fact even when `error` is set, because the walk
//               writes as it goes. Never resent: the key is gone, so asking
//               again returns `absent` and turns our own success into a refusal.
//   absent      PROVEN not stored. Never an operational fault — those are
//               `unresolved`. Whether this rewind is ENTITLED to one is decided
//               from the delta it applied, never from the store.
//   unresolved  still owed. The only category that may be resent.
//
// The policy is exercised against injected outcomes, because a storage fault is
// not something a test can conjure on demand and a policy only ever run against
// the happy path is not one that has been tested. The end-to-end cases then pin
// the same policy against a real store.

namespace {

// Aborts rather than letting a hang wedge the suite. Only ever fires on a
// genuine exclusion defect: nothing here is expected to reach the deadline.
struct watchdog_scope {
    std::atomic<bool> done{false};
    std::thread barker;

    watchdog_scope(std::chrono::seconds budget, char const* what)
        : barker([this, budget, what] {
              auto const deadline = std::chrono::steady_clock::now() + budget;
              while ( ! done.load()) {
                  if (std::chrono::steady_clock::now() > deadline) {
                      std::fprintf(stderr, "\n[watchdog] %s did not finish: "
                          "treating it as an exclusion defect\n", what);
                      std::abort();
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(20));
              }
          }) {}

    ~watchdog_scope() {
        done.store(true);
        barker.join();
    }
};

constexpr uint32_t trunk_len = 100;
constexpr uint64_t spend_fee = 1000;

utxoz::raw_outpoint key_from(uint8_t seed, uint32_t index) {
    hash_digest h{};
    h.fill(seed);
    return utxoz::make_outpoint(std::span<uint8_t const, 32>{h.data(), 32}, index);
}

utxoz::deferred_deletion_entry entry_of(utxoz::raw_outpoint const& key, uint32_t height) {
    return utxoz::deferred_deletion_entry{key, height};
}

// Records every batch it is handed and answers with whatever the test scripted.
// The record is what proves `erased` is never resent — an assertion about the
// requests actually made, not merely about the verdict reached.
struct scripted_applier {
    std::vector<utxoz::deletion_progress> script;
    std::vector<std::vector<utxoz::deferred_deletion_entry>> seen;
    size_t next = 0;

    utxoz::deletion_progress operator()(std::span<utxoz::deferred_deletion_entry const> batch) {
        seen.emplace_back(batch.begin(), batch.end());
        if (next < script.size()) {
            return script[next++];
        }
        return utxoz::deletion_progress{};
    }

    [[nodiscard]] size_t times_sent(utxoz::raw_outpoint const& key) const {
        size_t n = 0;
        for (auto const& batch : seen) {
            n += size_t(std::count_if(batch.begin(), batch.end(),
                [&](auto const& e) { return e.key == key; }));
        }
        return n;
    }
};

deletion_sweep_outcome sweep(std::vector<utxoz::deferred_deletion_entry> owed,
                             absence_tolerance const& tolerated,
                             scripted_applier& applier,
                             int attempts = 3) {
    return run_deletion_sweep(
        std::move(owed), tolerated,
        [&applier](std::span<utxoz::deferred_deletion_entry const> b) { return applier(b); },
        attempts, nullptr, nullptr);
}

} // namespace

// -----------------------------------------------------------------------------
// The policy, against outcomes a real store cannot be made to produce on demand
// -----------------------------------------------------------------------------

TEST_CASE("an applied deletion is retired and never sent again",
          "[node][reorg][deferred]") {
    // Resending a key that was already erased asks about something now genuinely
    // gone, gets `absent` back, and converts this operation's own success into a
    // refusal. So the request is rebuilt from `unresolved` and nothing else.
    auto const a = key_from(0xA1, 0);
    auto const b = key_from(0xB2, 0);

    scripted_applier applier;
    utxoz::deletion_progress first;
    first.erased.push_back(entry_of(a, 101));
    first.unresolved.push_back(entry_of(b, 101));
    utxoz::deletion_progress second;
    second.erased.push_back(entry_of(b, 101));
    applier.script = {first, second};

    CHECK(sweep({entry_of(a, 101), entry_of(b, 101)}, {}, applier)
          == deletion_sweep_outcome::applied);

    REQUIRE(applier.seen.size() == 2);
    CHECK(applier.times_sent(a) == 1);      // erased on attempt 1, never again
    CHECK(applier.times_sent(b) == 2);      // owed after attempt 1, so resent
    REQUIRE(applier.seen[1].size() == 1);
    CHECK(applier.seen[1].front().key == b);
}

TEST_CASE("progress survives a fault: what was applied is not sent again",
          "[node][reorg][deferred]") {
    // A deletion writes as it walks, so a fault partway leaves earlier keys
    // gone. Losing that — by rebuilding the next request from the original batch
    // — is the mutation this catches.
    auto const applied = key_from(0xC3, 0);
    auto const owed = key_from(0xD4, 0);

    scripted_applier applier;
    utxoz::deletion_progress faulted;
    faulted.erased.push_back(entry_of(applied, 101));
    faulted.unresolved.push_back(entry_of(owed, 101));
    faulted.error = utxoz::error_code::version_unreadable;
    utxoz::deletion_progress recovered;
    recovered.erased.push_back(entry_of(owed, 101));
    applier.script = {faulted, recovered};

    CHECK(sweep({entry_of(applied, 101), entry_of(owed, 101)}, {}, applier)
          == deletion_sweep_outcome::applied);

    // Retired even though the attempt that applied it also reported an error.
    CHECK(applier.times_sent(applied) == 1);
    CHECK(applier.times_sent(owed) == 2);
}

TEST_CASE("a fault with nothing left owed still refuses",
          "[node][reorg][deferred]") {
    // Everything is classified, so this is not a gap in coverage — but a
    // transition publishes only over a clean run.
    auto const key = key_from(0xE5, 0);
    scripted_applier applier;
    utxoz::deletion_progress p;
    p.erased.push_back(entry_of(key, 101));
    p.error = utxoz::error_code::catalog_unreadable;
    applier.script = {p};

    CHECK(sweep({entry_of(key, 101)}, {}, applier)
          == deletion_sweep_outcome::fault_reported);
}

TEST_CASE("an obligation that never clears exhausts its attempts and fails",
          "[node][reorg][deferred]") {
    auto const stuck = key_from(0xF6, 0);
    scripted_applier applier;
    utxoz::deletion_progress owed;
    owed.unresolved.push_back(entry_of(stuck, 101));
    applier.script = {owed, owed, owed, owed, owed};

    CHECK(sweep({entry_of(stuck, 101)}, {}, applier, 3)
          == deletion_sweep_outcome::attempts_exhausted);

    // Exactly the bound, not one more: an unbounded loop would hold the switch
    // open forever with the transition record in place.
    CHECK(applier.seen.size() == 3);
}

TEST_CASE("a proven absence is tolerated only where the delta entitles it",
          "[node][reorg][deferred]") {
    auto const tolerable = key_from(0x11, 0);
    auto const demanded = key_from(0x22, 0);
    auto const unclaimed = key_from(0x33, 0);

    absence_tolerance tolerated;
    tolerated.emplace(tolerable, true);
    tolerated.emplace(demanded, false);

    SECTION("entitled") {
        scripted_applier applier;
        utxoz::deletion_progress p;
        p.absent.push_back(entry_of(tolerable, 101));
        applier.script = {p};
        CHECK(sweep({entry_of(tolerable, 101)}, tolerated, applier)
              == deletion_sweep_outcome::applied);
    }

    SECTION("not entitled - a block required it to exist") {
        scripted_applier applier;
        utxoz::deletion_progress p;
        p.absent.push_back(entry_of(demanded, 101));
        applier.script = {p};
        CHECK(sweep({entry_of(demanded, 101)}, tolerated, applier)
              == deletion_sweep_outcome::absent_unaccounted);
    }

    SECTION("not entitled - no block claimed it at all") {
        scripted_applier applier;
        utxoz::deletion_progress p;
        p.absent.push_back(entry_of(unclaimed, 101));
        applier.script = {p};
        CHECK(sweep({entry_of(unclaimed, 101)}, tolerated, applier)
              == deletion_sweep_outcome::absent_unaccounted);
    }
}

TEST_CASE("folding a key's verdicts is conservative, never a union",
          "[node][reorg][deferred]") {
    // The fold itself, in every order, because the end-to-end scenarios cannot
    // reach it: outpoints are unique, so today each key is contributed by
    // exactly ONE block and AND is indistinguishable from OR there. The rule
    // still has to be right — the batch is applied once and UTXO-Z answers once
    // per distinct key, so the day anything makes a key reachable from two
    // blocks, a union would let one block's legitimate no-op excuse another
    // block's real deletion.
    auto const key = key_from(0x55, 0);

    SECTION("tolerable then strict") {
        absence_tolerance t;
        fold_absence_tolerance(t, key, true);
        fold_absence_tolerance(t, key, false);
        CHECK_FALSE(t.at(key));
    }

    SECTION("strict then tolerable") {
        absence_tolerance t;
        fold_absence_tolerance(t, key, false);
        fold_absence_tolerance(t, key, true);
        CHECK_FALSE(t.at(key));
    }

    SECTION("many tolerable and one strict, in any position") {
        absence_tolerance t;
        fold_absence_tolerance(t, key, true);
        fold_absence_tolerance(t, key, true);
        fold_absence_tolerance(t, key, false);
        fold_absence_tolerance(t, key, true);
        CHECK_FALSE(t.at(key));
    }

    SECTION("all tolerable stays tolerable") {
        absence_tolerance t;
        fold_absence_tolerance(t, key, true);
        fold_absence_tolerance(t, key, true);
        CHECK(t.at(key));
    }

    SECTION("a first verdict is taken as it stands") {
        absence_tolerance t;
        fold_absence_tolerance(t, key, true);
        CHECK(t.at(key));
        absence_tolerance u;
        fold_absence_tolerance(u, key, false);
        CHECK_FALSE(u.at(key));
    }
}

TEST_CASE("one strict obligation dominates a tolerable one for the same key",
          "[node][reorg][deferred]") {
    // UTXO-Z deduplicates by key and answers ONCE, so a key contributed by
    // several blocks at several heights comes back as one verdict. Folding the
    // per-block classifications with anything other than AND — a union of
    // "tolerable", say — lets one block's legitimate no-op excuse another
    // block's real deletion.
    auto const shared = key_from(0x44, 0);

    absence_tolerance tolerated;
    tolerated.emplace(shared, true);                    // block 101: created and spent itself
    auto const it = tolerated.find(shared);
    it->second = it->second && false;                   // block 102: required it to exist

    scripted_applier applier;
    utxoz::deletion_progress p;
    p.absent.push_back(entry_of(shared, 102));
    applier.script = {p};

    // Sent at two heights, answered once, and the strict obligation wins.
    CHECK(sweep({entry_of(shared, 101), entry_of(shared, 102)}, tolerated, applier)
          == deletion_sweep_outcome::absent_unaccounted);
}

TEST_CASE("the two callers differ in tolerance and in nothing else",
          "[node][reorg][deferred]") {
    // connect and reorg run the SAME policy from the same function, and the only
    // thing that separates them is what a proven absence means. Reimplementing
    // the retry and classification per caller is how they drifted apart before,
    // so both modalities are pinned here against one obligation.
    auto const key = key_from(0x66, 0);
    std::vector<utxoz::deferred_deletion_entry> const owed{entry_of(key, 101)};

    auto absent_once = [&] {
        scripted_applier a;
        utxoz::deletion_progress p;
        p.absent.push_back(entry_of(key, 101));
        a.script = {p};
        return a;
    };

    SECTION("connect is strict: no absence is legitimate") {
        // A connect batch nets out anything created and spent inside itself, so
        // every key it asks to delete was in the set.
        auto applier = absent_once();
        CHECK(sweep(owed, strict_absence(), applier)
              == deletion_sweep_outcome::absent_unaccounted);
    }

    SECTION("reorg tolerates exactly what its delta entitles") {
        absence_tolerance reorg_tolerance;
        reorg_tolerance.emplace(key, true);
        auto applier = absent_once();
        CHECK(sweep(owed, reorg_tolerance, applier) == deletion_sweep_outcome::applied);
    }

    SECTION("and the strict tolerance really is empty") {
        // If strict_absence() ever gained an entry, connect would start
        // tolerating an absence and the section above would still pass.
        CHECK(strict_absence().empty());
    }

    SECTION("everything else is shared: both stop on a fault with nothing owed") {
        for (auto const& tolerance : {strict_absence(), absence_tolerance{}}) {
            scripted_applier applier;
            utxoz::deletion_progress p;
            p.erased.push_back(entry_of(key, 101));
            p.error = utxoz::error_code::version_unreadable;
            applier.script = {p};
            CHECK(sweep(owed, tolerance, applier) == deletion_sweep_outcome::fault_reported);
        }
    }

    SECTION("and both retry only what is still owed, to the same bound") {
        scripted_applier applier;
        utxoz::deletion_progress stuck;
        stuck.unresolved.push_back(entry_of(key, 101));
        applier.script = {stuck, stuck, stuck, stuck};
        CHECK(sweep(owed, strict_absence(), applier)
              == deletion_sweep_outcome::attempts_exhausted);
        CHECK(applier.seen.size() == 3);
    }
}

// -----------------------------------------------------------------------------
// End to end, against a real store
// -----------------------------------------------------------------------------

namespace {

// A trunk of 100 blocks; a branch A of TWO; and a heavier branch B of four.
//
//   101: tx_parent spends the coinbase of block 1 (matures exactly at 101), and
//        tx_child spends tx_parent — both in ONE block, so tx_parent's output is
//        created and spent inside a single block and never enters the set.
//   102: tx_grand spends tx_child's output, created in 101. ACROSS two blocks:
//        that one DID enter the set and its erase must succeed, because
//        disconnecting 102 restores it before 101 erases it.
struct reorg_scenario {
    std::vector<domain::chain::block> trunk;
    std::vector<domain::chain::block> branch_a;
    std::vector<domain::chain::block> branch_b;
    domain::chain::transaction tx_parent;   // created and spent inside 101
    domain::chain::transaction tx_child;    // created in 101, spent in 102
    domain::chain::transaction tx_grand;    // created in 102, never spent

    [[nodiscard]] domain::chain::block const& a101() const { return branch_a.front(); }
    [[nodiscard]] domain::chain::block const& a102() const { return branch_a.back(); }
};

utxoz::raw_outpoint key_of(domain::chain::transaction const& tx, uint32_t index) {
    auto const txid = tx.hash();
    return utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, index);
}

reorg_scenario build_and_connect(chain_fixture& fixture) {
    auto& chain = fixture.chain();
    reorg_scenario s;

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 40) * block_spacing;

    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        s.trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = s.trunk.back().hash();
    }

    REQUIRE(fixture.organizer().add_headers(headers_of(s.trunk)).headers_added == trunk_len);
    persist_headers(fixture, s.trunk, 1);
    connect_bodies(fixture, s.trunk, 1);

    auto const& matured = s.trunk.front().transactions().front();
    auto const flags = chain.chain_settings().enabled_flags();

    s.tx_parent = spend_p2pkh(matured, 0, matured.outputs()[0].value() - spend_fee, flags);
    s.tx_child = spend_p2pkh(s.tx_parent, 0,
        s.tx_parent.outputs()[0].value() - spend_fee, flags);

    s.branch_a.push_back(mine_block(s.trunk.back().hash(), 101,
        base_time + 101 * block_spacing, 1, {s.tx_parent, s.tx_child}, 2 * spend_fee));

    s.tx_grand = spend_p2pkh(s.tx_child, 0,
        s.tx_child.outputs()[0].value() - spend_fee, flags);

    s.branch_a.push_back(mine_block(s.branch_a.front().hash(), 102,
        base_time + 102 * block_spacing, 1, {s.tx_grand}, spend_fee));

    REQUIRE(fixture.organizer().add_headers(headers_of(s.branch_a)).headers_added == 2);
    persist_headers(fixture, s.branch_a, 101);
    connect_bodies(fixture, s.branch_a, 101);

    prev = s.trunk.back().hash();
    for (uint32_t h = 101; h <= 104; ++h) {
        s.branch_b.push_back(mine_block(prev, h, base_time + h * block_spacing + 60, 2, {}, 0));
        prev = s.branch_b.back().hash();
    }

    return s;
}

// Probe the ACTIVE versions, then resolve this call's OWN batch. Any failure
// throws rather than answering, because "could not look" is not "not there".
bool resolves(blockchain::block_chain& chain, utxoz::raw_outpoint const& key, uint32_t at_height) {
    auto const direct = chain.find_utxo_raw(key, at_height);
    if (direct) {
        return true;
    }
    if (direct.error() != database::result_code::not_resolved) {
        throw std::runtime_error("resolves: the UTXO store failed; presence is unknown");
    }

    std::array<utxoz::lookup_request, 1> const own{utxoz::lookup_request{key, at_height}};
    auto const resolved = chain.utxo_resolve_raw(own);
    if ( ! resolved) {
        throw std::runtime_error("resolves: the resolution could not run; presence is unknown");
    }
    return resolved->found.contains(key);
}

template <typename T>
T run_to_completion(::asio::awaitable<T> work, std::chrono::steady_clock::duration budget,
                    std::string_view what) {
    ::asio::io_context ctx;
    std::optional<T> result;
    std::exception_ptr thrown;

    ::asio::co_spawn(ctx, std::move(work),
        [&result, &thrown](std::exception_ptr error, T value) {
            if (error) {
                thrown = error;
                return;
            }
            result = std::move(value);
        });

    ctx.run_for(budget);

    if (thrown) {
        std::rethrow_exception(thrown);
    }
    if ( ! result) {
        throw std::runtime_error(std::string(what) + ": did not complete within its budget");
    }
    return std::move(*result);
}

reorg_outcome run_switch(chain_fixture& fixture, database::header_index::index_t branch_head,
                        uint32_t fork_height, std::function<void()> on_abort) {
    return run_to_completion<reorg_outcome>(
        execute_reorg(fixture.chain(), fixture.organizer(), branch_head, fork_height,
            [on_abort = std::move(on_abort)] {
                if (on_abort) {
                    on_abort();
                }
                return false;
            },
            real_persister(fixture.chain())),
        std::chrono::seconds(30), "the reorganization");
}

database::header_index::index_t propose_switch(chain_fixture& fixture, reorg_scenario const& s) {
    auto const result = fixture.organizer().add_headers(headers_of(s.branch_b));
    REQUIRE(result.reorg_candidate);
    REQUIRE(result.reorg_fork_height == int32_t(trunk_len));
    return result.reorg_branch_head;
}

} // namespace

TEST_CASE("the classification is per block, and strict dominates tolerable",
          "[node][reorg][deferred]") {
    // The discriminating test for the rule itself. disconnect_block accumulates
    // the tolerance map, and is called directly here so the map can be read
    // rather than inferred from effects.
    //
    // A branch-wide rule — "created and spent anywhere among the abandoned
    // blocks" — passes every other test in this file and fails this one, because
    // tx_child's output is created in 101 and spent in 102.
    chain_fixture fixture("sweep_classification");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const s = build_and_connect(fixture);

    absence_tolerance tolerated;
    std::vector<utxoz::deferred_deletion_entry> owed;

    // The same capability the rewind holds: one window for the whole operation.
    auto const window = chain.begin_utxo_write();
    REQUIRE(chain.disconnect_block(102, window, tolerated, owed)
            == database::disconnect_result::ok);
    REQUIRE(chain.disconnect_block(101, window, tolerated, owed)
            == database::disconnect_result::ok);

    // Created and spent inside 101: never in the set, so absence is tolerable.
    auto const parent = tolerated.find(key_of(s.tx_parent, 0));
    REQUIRE(parent != tolerated.end());
    CHECK(parent->second);

    // Created in 101, spent in 102: block 102's disconnect restored it, so 101's
    // erase must succeed. Present in the map, and NOT tolerable.
    auto const child = tolerated.find(key_of(s.tx_child, 0));
    REQUIRE(child != tolerated.end());
    CHECK_FALSE(child->second);

    // Ordinary creations, never spent: strictly demanded.
    for (auto const& key : {key_of(s.tx_grand, 0),
                            key_of(s.a101().transactions().front(), 0),
                            key_of(s.a102().transactions().front(), 0)}) {
        auto const it = tolerated.find(key);
        REQUIRE(it != tolerated.end());
        CHECK_FALSE(it->second);
    }

    // Exactly one key is entitled to be absent.
    CHECK(std::count_if(tolerated.begin(), tolerated.end(),
        [](auto const& kv) { return kv.second; }) == 1);

    // And the obligation carries every output of both blocks.
    CHECK(owed.size() == tolerated.size());
}

TEST_CASE("a completed rewind applies what it owed before it publishes",
          "[node][reorg][deferred]") {
    chain_fixture fixture("sweep_drains");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const s = build_and_connect(fixture);
    auto const branch_head = propose_switch(fixture, s);
    auto const outcome = run_switch(fixture, branch_head, trunk_len, {});

    CHECK_FALSE(outcome.fatal);
    REQUIRE(outcome.result.ok);
    REQUIRE(outcome.result.validated_tip);
    CHECK(*outcome.result.validated_tip == trunk_len);
    CHECK(chain.read_transition_record().status == database::transition_status::clean);

    // Every output the abandoned branch created stops resolving: the same-block
    // one, the cross-block one, the unspent one, and both coinbases.
    CHECK_FALSE(resolves(chain, key_of(s.tx_parent, 0), trunk_len));
    CHECK_FALSE(resolves(chain, key_of(s.tx_child, 0), trunk_len));
    CHECK_FALSE(resolves(chain, key_of(s.tx_grand, 0), trunk_len));
    CHECK_FALSE(resolves(chain, key_of(s.a101().transactions().front(), 0), trunk_len));
    CHECK_FALSE(resolves(chain, key_of(s.a102().transactions().front(), 0), trunk_len));

    // And what A had spent is back, at its ORIGINAL height rather than at 101,
    // which would misdate its maturity.
    CHECK(resolves(chain, key_of(s.trunk.front().transactions().front(), 0), trunk_len));

    // The positive control: every refusal in this file would also be satisfied
    // by a node that refused unconditionally.
    REQUIRE(fixture.restart());
    auto const built = fixture.chain().get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == trunk_len);
    CHECK(fixture.chain().read_transition_record().status == database::transition_status::clean);
}

TEST_CASE("the connected branch is spendable again after the switch",
          "[node][reorg][deferred]") {
    chain_fixture fixture("sweep_continues");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const s = build_and_connect(fixture);
    auto const branch_head = propose_switch(fixture, s);
    REQUIRE(run_switch(fixture, branch_head, trunk_len, {}).result.ok);

    connect_bodies(fixture, s.branch_b, 101);

    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 104u);
    CHECK(chain.read_transition_record().status == database::transition_status::clean);

    CHECK_FALSE(resolves(chain, key_of(s.tx_child, 0), 104));
    CHECK_FALSE(resolves(chain, key_of(s.tx_parent, 0), 104));

    REQUIRE(fixture.restart());
    CHECK_FALSE(resolves(fixture.chain(), key_of(s.tx_child, 0), 104));
}

TEST_CASE("a deletion the rewind did not account for is fatal and publishes nothing",
          "[node][reorg][deferred]") {
    // Block 101's coinbase output is an ordinary creation: nothing spends it, so
    // its deletion must succeed and it is strictly demanded. Removing it from
    // the set beforehand makes the rewind's own delete come back PROVEN ABSENT
    // for a key that is not entitled to it.
    chain_fixture fixture("sweep_unaccounted");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const s = build_and_connect(fixture);
    auto const coinbase_101 = key_of(s.a101().transactions().front(), 0);
    REQUIRE(resolves(chain, coinbase_101, 102));

    std::array<utxoz::deferred_deletion_entry, 1> const strip{
        utxoz::deferred_deletion_entry{coinbase_101, 101}};
    {
        // Scoped, and it MUST be: resolves() below takes a read lease, and the
        // gate is deliberately not recursive, so reading while still holding the
        // window is the self-deadlock the header calls reachable-but-unsupported.
        // This test formed it and hung — the pattern being real, not a reason to
        // soften the gate.
        auto const strip_window = chain.begin_utxo_write();
        auto const stripped = chain.utxo_apply_deletes(strip_window, strip);
        REQUIRE(stripped.erased.size() == 1);
    }
    REQUIRE_FALSE(resolves(chain, coinbase_101, 102));

    auto const branch_head = propose_switch(fixture, s);
    auto const outcome = run_switch(fixture, branch_head, trunk_len, {});

    CHECK(outcome.fatal);
    CHECK_FALSE(outcome.result.ok);
    CHECK_FALSE(outcome.result.validated_tip.has_value());

    // The record stands, naming the reorganization, and nothing was published.
    auto const check = chain.read_transition_record();
    REQUIRE(check.status == database::transition_status::recovery_required);
    REQUIRE(check.record.has_value());
    CHECK(check.record->type == database::transition_type::reorg);

    // The markers followed the rewind to the fork — set_heights runs per
    // disconnected block and never touches the record — which is correct and is
    // NOT publication.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == trunk_len);

    // And the next start refuses outright. #602 retries within the live
    // operation only; an interrupted transition is never resumed across a
    // restart, it is refused and rebuilt.
    CHECK_FALSE(fixture.restart());
}

TEST_CASE("a chain starts and makes progress under either storage mode",
          "[node][reorg][deferred][gate]") {
    // REGRESSION for a production deadlock full mode cannot reach. start() held
    // one window across its whole body, and the reference-mode wiring inside it
    // takes another; the gate is not recursive, so a reference build hung at
    // startup while every full-mode run stayed green.
    //
    // Compiled in both modes, so the reference build actually exercises the
    // #ifdef branch that deadlocked. Under a watchdog, because the failure it
    // guards against is a hang rather than a wrong answer.
    watchdog_scope guard(std::chrono::seconds(60), "starting a chain");

    chain_fixture fixture("gate_start_regression");
    REQUIRE(fixture.created());

    // The call that hung. Reaching the next line is the assertion.
    REQUIRE(fixture.start());

    auto& chain = fixture.chain();

    // Progress, not merely "it returned": the store answers, a window can be
    // taken and released, and a read completes afterwards.
    CHECK(chain.utxo_size() == 0u);
    {
        auto const window = chain.begin_utxo_write();
        CHECK(window.held());
    }
    CHECK(chain.utxo_size() == 0u);

    // And a restart, which closes and reopens — the other lifecycle path that
    // takes windows.
    REQUIRE(fixture.restart());
    CHECK(fixture.chain().utxo_size() == 0u);
}

TEST_CASE("a deletion under a scoped window is readable once the window ends",
          "[node][reorg][deferred][gate]") {
    // REGRESSION. An earlier version held the write window across a resolves()
    // call, which takes a read lease; the gate is not recursive, so the suite
    // hung until it was killed. That is the reachable-but-unsupported pattern
    // the header describes, and it must not come back as a hang someone has to
    // notice.
    //
    // The blocking capability is owned BY THE TEST, in an optional, so the
    // failure path can release it and let the reader finish. Nothing is ever
    // detached: a thread parked on the gate that outlived this fixture would be
    // reading freed memory, which would poison the rest of the suite and every
    // sanitizer report in it.
    chain_fixture fixture("gate_scoped_window_regression");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const s = build_and_connect(fixture);
    auto const target = key_of(s.tx_grand, 0);
    REQUIRE(resolves(chain, target, 102));

    std::optional<utxo_write_window> blocker = chain.begin_utxo_write();
    CHECK(blocker->held());
    {
        std::array<utxoz::deferred_deletion_entry, 1> const batch{
            utxoz::deferred_deletion_entry{target, 102}};
        auto const progress = chain.utxo_apply_deletes(*blocker, batch);
        CHECK(progress.erased.size() == 1);
    }

    // THE PROPERTY: released before anything reads.
    blocker.reset();

    // Re-acquisition after release, which is what this can actually show:
    // `blocker` was already reset above, so taking a window here proves the
    // first one gave the gate back rather than proving anything about exclusion
    // while it was alive. Exclusion during a live window is covered where it can
    // be forced — see the gate's own suite.
    {
        auto const proof = chain.begin_utxo_write();
        CHECK(proof.held());
    }

    std::atomic<bool> done{false};
    std::atomic<bool> still_there{true};
    std::thread reader;

    // The failure here is a HANG, not a wrong answer: `blocker` was released
    // above before the reader starts, so the reaper's reset below is a no-op on
    // every path — it cannot rescue a reader stuck on a window this test no
    // longer holds. If the read blocks, the FAIL below throws, unwinding reaches
    // the join, and the join has no deadline of its own. This is what bounds it.
    watchdog_scope hang_guard(std::chrono::seconds(60), "a read after a scoped window");

    // Joins on EVERY path, including a failed REQUIRE or an exception. The reset
    // is kept for the shape rather than the effect: if a later edit moves the
    // release below the thread, the reaper is already correct.
    struct reaper {
        std::thread& worker;
        std::optional<utxo_write_window>& blocking;
        ~reaper() {
            if (worker.joinable()) {
                blocking.reset();
                worker.join();
            }
        }
    } const guard{reader, blocker};

    // resolves() throws when the store fails or the resolution cannot run, and an
    // exception leaving a thread function calls std::terminate — which would kill
    // the whole suite before the FAIL below could say anything. Captured here and
    // reported by the main thread.
    std::atomic<bool> threw{false};
    std::string reason;
    reader = std::thread([&] {
        try {
            still_there.store(resolves(chain, target, 102));
        } catch (std::exception const& e) {
            reason = e.what();
            threw.store(true);
        } catch (...) {
            reason = "an unknown exception";
            threw.store(true);
        }
        done.store(true);
    });

    // Short: the isolated case finishes immediately, so a long budget would only
    // delay the diagnosis.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ( ! done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // FAIL, not FAIL_CHECK: the reaper above still runs and joins cleanly.
    if ( ! done.load()) {
        FAIL("the read did not complete: a write window is being held across it");
    }

    if (threw.load()) {
        FAIL("the read failed rather than answering: " << reason);
    }
    CHECK(still_there.load() == false);   // deleted, and observable afterwards
}

TEST_CASE("connect runs its whole mutation under one window and reaches sync",
          "[node][reorg][deferred][gate]") {
    // A gate regression here is a HANG, not a wrong answer: build_and_connect
    // drives the window-taking sync path, and reaching the end is the assertion.
    // Without this the suite wedges instead of reporting.
    watchdog_scope guard(std::chrono::seconds(60), "connect under one window");
    // End to end for the connect path (#649): the batch's inserts, the deletions
    // it owes and utxo_sync(window) all run under ONE capability, and the effect
    // is observable afterwards rather than merely "it did not die".
    //
    // The timeout is the point as much as the assertions: utxo_sync() used to
    // take its own window while the batch still held one, which this gate — not
    // being recursive — turns into a deterministic hang. Reaching the end at all
    // is what proves that is gone.
    chain_fixture fixture("gate_connect_e2e");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const s = build_and_connect(fixture);

    // connect_bodies drove utxo_build_task to completion, which is the path that
    // opens the window, applies the delta, applies the deletions and syncs.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 102u);

    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == 102u);

    // The final effect: what the branch created resolves, what it spent does not.
    CHECK(resolves(chain, key_of(s.tx_grand, 0), 102));
    CHECK_FALSE(resolves(chain, key_of(s.tx_child, 0), 102));
    CHECK_FALSE(resolves(chain, key_of(s.tx_parent, 0), 102));

    // Published cleanly, so the sync at the end of the window did happen.
    CHECK(chain.read_transition_record().status == database::transition_status::clean);

    // And the gate is left open: a window can be taken now, which it could not
    // be if the batch had abandoned one.
    auto const after = chain.begin_utxo_write();
    CHECK(after.held());
}

TEST_CASE("reorg runs the whole rewind under one window and publishes",
          "[node][reorg][deferred][gate]") {
    // Same reason, and run_switch bounds only itself: build_and_connect and
    // connect_bodies below it are unbounded.
    watchdog_scope guard(std::chrono::seconds(60), "the rewind under one window");
    // The same for the switch: restorations, every disconnect, the deletion
    // sweep and the barrier under ONE capability, ending in a published branch
    // and a verifiable UTXO set.
    chain_fixture fixture("gate_reorg_e2e");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const s = build_and_connect(fixture);
    auto const branch_head = propose_switch(fixture, s);
    auto const outcome = run_switch(fixture, branch_head, trunk_len, {});

    // It finished — the switch reaches publish_reorg_transition, which syncs
    // while still holding the rewind's window.
    CHECK_FALSE(outcome.fatal);
    REQUIRE(outcome.result.ok);
    REQUIRE(outcome.result.validated_tip);
    CHECK(*outcome.result.validated_tip == trunk_len);
    CHECK(chain.read_transition_record().status == database::transition_status::clean);

    // The branch is published and the UTXO set matches it.
    connect_bodies(fixture, s.branch_b, 101);
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == 104u);

    CHECK_FALSE(resolves(chain, key_of(s.tx_parent, 0), 104));
    CHECK_FALSE(resolves(chain, key_of(s.tx_child, 0), 104));
    CHECK_FALSE(resolves(chain, key_of(s.tx_grand, 0), 104));
    CHECK(resolves(chain, key_of(s.trunk.front().transactions().front(), 0), 104));

    auto const after = chain.begin_utxo_write();
    CHECK(after.held());
}

TEST_CASE("a concurrent resolution keeps its own batch through the switch",
          "[node][reorg][deferred]") {
    // The race this design removed rather than locked. UTXO-Z 0.10.0 keeps no
    // queue: resolve() borrows a span and returns, so a batch belonging to a
    // concurrent transaction validation cannot be consumed by the rewind, and
    // none of the rewind's can arrive there.
    //
    // The timing is forced rather than hoped for: the foreign resolution runs
    // from inside execute_reorg, through the abort predicate it calls.
    chain_fixture fixture("sweep_foreign_batch");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const s = build_and_connect(fixture);
    auto const branch_head = propose_switch(fixture, s);

    // A key no version holds: proven absent, and that must stay the foreign
    // caller's answer rather than becoming the switch's verdict.
    auto const foreign_key = key_from(0xC7, 0);

    bool ran = false;
    bool foreign_answer_intact = false;
    auto const outcome = run_switch(fixture, branch_head, trunk_len, [&] {
        if (ran) {
            return;
        }
        ran = true;
        std::array<utxoz::lookup_request, 1> const own{utxoz::lookup_request{foreign_key, 101}};
        auto const resolved = chain.utxo_resolve_raw(own);
        foreign_answer_intact = resolved.has_value()
                             && resolved->found.empty()
                             && resolved->absent.size() == 1
                             && resolved->absent.front() == foreign_key;
    });

    REQUIRE(ran);
    // The foreign resolution got its own answer, about its own key, and only it.
    CHECK(foreign_answer_intact);

    // And the switch completed on its own terms: a foreign key is not its
    // business and must not become its verdict.
    CHECK_FALSE(outcome.fatal);
    REQUIRE(outcome.result.ok);
    CHECK(chain.read_transition_record().status == database::transition_status::clean);
}

// -----------------------------------------------------------------------------
// The harness itself: neither failure may leave through the return value
// -----------------------------------------------------------------------------

TEST_CASE("a switch that never finishes is a test failure, not an outcome",
          "[node][reorg][deferred]") {
    auto never_finishes = []() -> ::asio::awaitable<reorg_outcome> {
        ::asio::steady_timer timer(co_await ::asio::this_coro::executor);
        timer.expires_after(std::chrono::hours(1));
        co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
        co_return reorg_outcome{};
    };

    try {
        auto const outcome = run_to_completion<reorg_outcome>(
            never_finishes(), std::chrono::milliseconds(50), "the reorganization");
        (void)outcome;
        FAIL("run_to_completion returned instead of reporting the timeout");
    } catch (std::runtime_error const& e) {
        CHECK(std::string(e.what()).find("did not complete") != std::string::npos);
    }
}

TEST_CASE("an exception from the switch propagates rather than becoming an outcome",
          "[node][reorg][deferred]") {
    auto throws = []() -> ::asio::awaitable<reorg_outcome> {
        throw std::runtime_error("the switch threw");
        co_return reorg_outcome{};
    };

    try {
        auto const outcome = run_to_completion<reorg_outcome>(
            throws(), std::chrono::seconds(5), "the reorganization");
        (void)outcome;
        FAIL("run_to_completion returned instead of propagating the exception");
    } catch (std::runtime_error const& e) {
        auto const what = std::string(e.what());
        CHECK(what.find("the switch threw") != std::string::npos);
        CHECK(what.find("did not complete") == std::string::npos);
    }
}
