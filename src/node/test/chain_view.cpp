// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <expected>
#include <memory>
#include <tuple>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

#include <kth/blockchain/pools/block_template.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/pools/header_organizer.hpp>
#include <kth/blockchain/pools/mempool.hpp>
#include <kth/database/native_file.hpp>
#include <kth/database/settings.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// What the chain publishes about itself
// =============================================================================
//
// The state a transaction is validated against, the tip it describes and the
// number that labels the pair used to be three separate things: the state was
// computed once at startup and never advanced, and the readers that noticed
// combined it with a fresh read of the height or the hash — a stale value beside
// a current one, describing a chain that never existed (#605).
//
// These drive the node's own connect and reorganization paths, because when the
// state advances is a property of those paths and not of the chain object.

namespace {

constexpr uint32_t trunk_len = 40;

// Enough pooled transactions that assembling a template takes long enough to be
// a window a second request can arrive inside. Sized against the build, not the
// pool: see the measurement in the recapture test.
constexpr uint64_t pool_entries = 200000;

// The coinbase-budget test needs contention, not a transition landing inside
// it, so its window is the arrival of one request while another builds — a
// shorter thing to hit, and a smaller pool.
constexpr uint64_t reserve_pool_entries = 60000;

// A pooled transaction, synthetic. The template builder is a pure function of
// the entry set — it resolves parents within that set and nothing against the
// chain — so these need to be well-formed and distinct, not spendable.
inline domain::chain::transaction pooled_tx(uint64_t tag) {
    hash_digest prevout{};
    for (int i = 0; i < 8; ++i) {
        prevout[i] = uint8_t(tag >> (8 * i));
    }

    domain::chain::input::list ins;
    ins.emplace_back(domain::chain::output_point{prevout, 0},
                     domain::chain::script{}, 0xffffffffu);

    domain::chain::output out;
    out.set_value(1000u + tag);
    out.set_script(domain::chain::script{});

    domain::chain::output::list outs;
    outs.push_back(std::move(out));

    return domain::chain::transaction{1u, 0u, std::move(ins), std::move(outs)};
}

// A chain of `len` blocks, connected the way the node connects them.
struct built_chain {
    test::chain_fixture fixture;
    std::vector<domain::chain::block> trunk;
    uint32_t base_time;

    explicit built_chain(char const* tag, uint32_t len)
        : fixture(tag)
        , base_time(uint32_t(zulu_time()) - (len + 30) * block_spacing)
    {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());

        auto prev = domain::chain::block::genesis_regtest().hash();
        for (uint32_t h = 1; h <= len; ++h) {
            trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
    }

    blockchain::block_chain& chain() { return fixture.chain(); }

    void add_headers(std::vector<domain::chain::block> const& blocks, uint32_t start) {
        auto const added = fixture.organizer().add_headers(headers_of(blocks));
        REQUIRE(added.headers_added == blocks.size());
        persist_headers(fixture, blocks, start);
    }
};

// Make the undo record at `height` disown its block, so disconnecting that block
// fails while the ones above it succeed.
//
// An undo record carries the hash of the block that owns it, and `read_undo`
// refuses one that disagrees (#604) — before the inverse delta is applied, which
// is what makes this a clean failure rather than a half-applied one. Nothing is
// restarted: the position comes from the in-memory index, which still holds it.
inline void corrupt_undo_owner(test::chain_fixture& fixture,
                               blockchain::block_chain& chain, uint32_t height) {
    // marker(4) + owning block hash(32) + payload size(4). `undo_pos` points just
    // past that, so the hash begins four bytes into the header.
    constexpr size_t header_size = 4 + std::tuple_size<hash_digest>::value + sizeof(uint32_t);

    auto const idx = chain.headers().active_at(static_cast<int32_t>(height));
    REQUIRE(idx != database::header_index::null_index);
    REQUIRE(chain.headers().has_undo_data(idx));

    auto const file = chain.headers().get_file_number(idx);
    auto const undo_pos = chain.headers().get_undo_pos(idx);
    REQUIRE(file >= 0);
    REQUIRE(undo_pos >= header_size);

    auto const path = fixture.dir() / fmt::format("blocks/rev{:05}.dat", file);
    FILE* rev = database::open_native(path, "r+b");
    REQUIRE(rev != nullptr);
    REQUIRE(std::fseek(rev, static_cast<long>(undo_pos - header_size + 4), SEEK_SET) == 0);

    uint8_t first = 0;
    REQUIRE(std::fread(&first, 1, 1, rev) == 1);
    first ^= 0xFFu;
    REQUIRE(std::fseek(rev, static_cast<long>(undo_pos - header_size + 4), SEEK_SET) == 0);
    REQUIRE(std::fwrite(&first, 1, 1, rev) == 1);
    std::fclose(rev);
}

// A competing branch from `fork_height`, with its headers in. Returns its head.
//
// Long enough to survive deep-reorg parking, which is not the same as merely
// heavier: a branch that would rewind validated blocks has to have accumulated
// twice the work the active chain gained since the fork (BCHN's -parkdeepreorg).
// Against four blocks above the fork, five would be heavier and still parked.
//
// A switch needs somewhere to switch *to*: `switch_to_branch` derives the fork
// rather than taking the caller's word for it, so handing it the active chain's
// own head is rejected before anything is disconnected.
inline database::header_index::index_t competing_branch(
    built_chain& built, uint32_t fork_height, uint32_t length) {

    std::vector<domain::chain::block> branch;
    auto prev = built.trunk[fork_height - 1].hash();
    for (uint32_t i = 0; i < length; ++i) {
        auto const h = fork_height + 1 + i;
        // A different salt, so these are not the trunk's blocks again.
        branch.push_back(test::mine_block(prev, h, built.base_time + h * block_spacing + 60,
                                          /*salt*/ 7, {}, 0));
        prev = branch.back().hash();
    }

    // Deep-reorg parking measures the rewind against the height the organizer
    // believes is validated, and nothing in this harness tells it — the node's
    // sync tasks do that in production. Without it a fork a few blocks down
    // looks like a rewind of the whole chain and is parked, so the switch never
    // becomes a candidate.
    built.fixture.organizer().note_block_validated(
        static_cast<int32_t>(built.chain().chain_view()->connected_tip_height));

    auto const result = built.fixture.organizer().add_headers(headers_of(branch));
    REQUIRE(result.reorg_candidate);
    REQUIRE(result.reorg_fork_height == int32_t(fork_height));
    return result.reorg_branch_head;
}

} // namespace

TEST_CASE("a started chain publishes the tip it has connected", "[chain_view]") {
    built_chain built("view_initial", trunk_len);

    auto const view = built.chain().chain_view();
    REQUIRE(view);
    // Nothing connected yet: the genesis block is the tip, and the state is the
    // context for the block after it.
    CHECK(view->state->height() == 1u);
    CHECK(view->tip_hash == domain::chain::block::genesis_regtest().hash());
}

TEST_CASE("headers alone move neither the view nor the generation", "[chain_view]") {
    // Headers arrive thousands of blocks ahead of what is connected during a
    // sync. Building the state from that tip would give a height, a median time
    // past and a set of activation flags for a block whose UTXO delta has not
    // been applied — internally consistent and describing nothing.
    built_chain built("view_headers_only", trunk_len);

    auto const before = built.chain().chain_view();
    REQUIRE(before);

    built.add_headers(built.trunk, 1);

    auto const after = built.chain().chain_view();
    REQUIRE(after);
    CHECK(after->generation == before->generation);
    CHECK(after->state->height() == before->state->height());
    CHECK(after->tip_hash == before->tip_hash);

    // And specifically not the header tip, which is now far above.
    CHECK(built.chain().headers().active_tip_height() == int32_t(trunk_len));
    CHECK(after->state->height() == 1u);
}

TEST_CASE("connecting a batch publishes the height after its last block", "[chain_view]") {
    built_chain built("view_batch", trunk_len);
    built.add_headers(built.trunk, 1);

    auto const before = built.chain().chain_view();
    REQUIRE(before);

    connect_bodies(built.fixture, built.trunk, 1);

    auto const after = built.chain().chain_view();
    REQUIRE(after);
    CHECK(after->state->height() == trunk_len + 1u);
    CHECK(after->tip_hash == built.trunk.back().hash());
    CHECK(after->generation > before->generation);
}

TEST_CASE("a second batch advances the view again", "[chain_view]") {
    built_chain built("view_two_batches", trunk_len);

    std::vector<domain::chain::block> first(built.trunk.begin(), built.trunk.begin() + 20);
    std::vector<domain::chain::block> second(built.trunk.begin() + 20, built.trunk.end());

    built.add_headers(built.trunk, 1);

    connect_bodies(built.fixture, first, 1);
    auto const after_first = built.chain().chain_view();
    REQUIRE(after_first);
    CHECK(after_first->state->height() == 21u);
    CHECK(after_first->tip_hash == first.back().hash());

    connect_bodies(built.fixture, second, 21);
    auto const after_second = built.chain().chain_view();
    REQUIRE(after_second);
    CHECK(after_second->state->height() == trunk_len + 1u);
    CHECK(after_second->tip_hash == built.trunk.back().hash());
    CHECK(after_second->generation > after_first->generation);
}

TEST_CASE("the triple is read whole under concurrent publication", "[chain_view]") {
    // The point of publishing one object rather than three fields: a reader
    // holds the previous triple or the next one, never a state from one and a
    // hash from another. Readers run while batches are being connected, and
    // every view they take must agree with itself.
    built_chain built("view_atomic", trunk_len);
    built.add_headers(built.trunk, 1);

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> reads{0};
    uint64_t mismatches = 0;

    std::thread reader([&] {
        while ( ! stop) {
            // Spinning as hard as this thread can would starve the connect path
            // it is supposed to be racing, which is the opposite of the point.
            std::this_thread::yield();

            auto const view = built.chain().chain_view();
            if ( ! view) continue;
            ++reads;

            // The hash the view carries must be the hash of the block one below
            // the height it carries. A torn read would pair them across
            // publications and this would not hold.
            auto const tip_height = view->state->height() - 1u;
            auto const expected = tip_height == 0
                ? domain::chain::block::genesis_regtest().hash()
                : built.trunk[tip_height - 1].hash();
            if (view->tip_hash != expected) ++mismatches;
        }
    });

    for (uint32_t start = 1; start <= trunk_len; start += 10) {
        std::vector<domain::chain::block> batch(
            built.trunk.begin() + (start - 1), built.trunk.begin() + (start - 1) + 10);
        connect_bodies(built.fixture, batch, start);
    }

    stop = true;
    reader.join();

    CHECK(mismatches == 0);
    CHECK(reads > 0);
    CHECK(built.chain().chain_view()->state->height() == trunk_len + 1u);
}

TEST_CASE("mining info reports one publication, not a height beside an old difficulty", "[chain_view]") {
    // It used to take the work required from the frozen state and the height
    // from a fresh read, so it described a chain that never existed.
    built_chain built("view_mining_info", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto const view = built.chain().chain_view();
    REQUIRE(view);

    ::asio::io_context ctx;
    std::optional<blockchain::mining_info> info;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        if (auto const r = co_await built.chain().fetch_mining_info(); r) info = *r;
    }, ::asio::detached);
    ctx.run_for(std::chrono::seconds(30));

    REQUIRE(info);
    CHECK(info->blocks == view->state->height() - 1u);
    CHECK(info->blocks == trunk_len);
}

TEST_CASE("a failed publication leaves the view and the generation alone", "[chain_view]") {
    // Failing rather than keeping the old view quietly is the point: at the
    // close of a batch or a reorganization, being unable to describe the state
    // just reached is not something to carry on from. What must not happen is a
    // half-published triple, or a generation that moved without a state to go
    // with it — a reader would then see a number saying something changed and a
    // view saying it did not.
    built_chain built("view_publish_failure", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto const before = built.chain().chain_view();
    REQUIRE(before);

    // A height the chain cannot resolve: past everything it holds.
    auto const ec = built.chain().publish_chain_view(trunk_len + 5000u);
    CHECK(ec);

    auto const after = built.chain().chain_view();
    REQUIRE(after);
    CHECK(after->generation == before->generation);
    CHECK(after->state->height() == before->state->height());
    CHECK(after->tip_hash == before->tip_hash);

    // The previous view is still there and still coherent — which is why the
    // caller has to treat the error as fatal rather than read on: nothing in the
    // view itself says it is out of date.
    CHECK(after->state->height() == trunk_len + 1u);

    // And the generation has no hole in it. A failure that moved the counter
    // without publishing would leave a number no view ever carries, so a reader
    // comparing generations would see one it can never be given and conclude
    // something was published that was not. Several failures, then a success
    // that lands exactly one above where it started.
    for (int i = 0; i < 5; ++i) {
        CHECK(built.chain().publish_chain_view(trunk_len + 5000u + uint32_t(i)));
    }
    CHECK(built.chain().chain_view()->generation == before->generation);

    REQUIRE_FALSE(built.chain().publish_chain_view(trunk_len));
    CHECK(built.chain().chain_view()->generation == before->generation + 1u);
}

TEST_CASE("a template built after a batch names the new tip", "[chain_view]") {
    // The template cache refuses to serve a snapshot from a previous tip, and it
    // decides that by comparing the parent hash. That guard could never fire
    // while the parent came from a height frozen at startup: the key never
    // changed, so a template built before a batch would keep being served after
    // it — on a parent the chain had left behind.
    built_chain built("view_template_cache", trunk_len);

    std::vector<domain::chain::block> first(built.trunk.begin(), built.trunk.begin() + 20);
    std::vector<domain::chain::block> second(built.trunk.begin() + 20, built.trunk.end());

    // Headers arrive with the blocks they describe. Adding all forty first would
    // leave the node holding headers it has not connected — a node that is
    // behind, and one that is behind does not serve mining work (#621).
    built.add_headers(first, 1);
    connect_bodies(built.fixture, first, 1);

    auto const fetch = [&]() -> std::optional<blockchain::mining_template> {
        ::asio::io_context ctx;
        std::optional<blockchain::mining_template> out;
        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            if (auto const r = co_await built.chain().fetch_mining_template(1000u); r) out = *r;
        }, ::asio::detached);
        ctx.run_for(std::chrono::seconds(30));
        return out;
    };

    auto const early = fetch();
    REQUIRE(early);
    CHECK(early->previous_block_hash == first.back().hash());
    CHECK(early->height == 21u);

    built.add_headers(second, 21);
    connect_bodies(built.fixture, second, 21);

    auto const later = fetch();
    REQUIRE(later);
    CHECK(later->previous_block_hash == built.trunk.back().hash());
    CHECK(later->height == trunk_len + 1u);

    // And specifically not the earlier one served again.
    CHECK(later->previous_block_hash != early->previous_block_hash);
}

TEST_CASE("the branch path answers null before anything is published", "[chain_view]") {
    // A branch's state is seeded from the published one, and building it reads
    // through that seed. There is exactly one window where none exists — a chain
    // constructed but not started, since start() is what publishes the first —
    // and the branch path has to answer rather than read through nothing.
    test::chain_fixture fixture("view_unstarted");
    REQUIRE(fixture.created());

    // Deliberately not started.
    blockchain::settings chain_settings(domain::config::network::regtest);
    database::settings db_settings;
    db_settings.directory = fixture.dir();

    blockchain::block_chain chain(chain_settings, db_settings,
                                  domain::config::network::regtest,
                                  /*relay_transactions*/ false);

    REQUIRE(chain.chain_view() == nullptr);

    auto const branch_ptr = std::make_shared<blockchain::branch>(0u, chain.block_validations());
    CHECK(chain.chain_state(branch_ptr) == nullptr);
}

// =============================================================================
// When this node should be serving mining work (#621)
// =============================================================================

TEST_CASE("headers ahead of the connected chain mean the node is behind", "[chain_view][sync]") {
    // Recent headers say nothing about having connected them. This is the case
    // the old `is_stale()` got wrong: it read the stored block marker, which
    // during a sync sits with the headers rather than with the UTXO set, so a
    // node in the middle of downloading looked synchronized.
    built_chain built("sync_behind", trunk_len);
    built.add_headers(built.trunk, 1);

    // Half connected, so the tip that is published is recent while the headers
    // run past it. Freshness holding is what makes this test about catching up
    // and not about the clock.
    std::vector<domain::chain::block> first(built.trunk.begin(), built.trunk.begin() + 20);
    connect_bodies(built.fixture, first, 1);

    auto const ready = built.chain().synchronization();
    CHECK_FALSE(ready.caught_up);
    CHECK(ready.fresh);
    CHECK_FALSE(ready.synchronized());
}

TEST_CASE("a chain that has connected nothing is behind and stale at once", "[chain_view][sync]") {
    // The connected tip is the genesis block, whose timestamp is from 2011. Both
    // halves fail, and they fail for their own reasons rather than one standing
    // in for the other.
    built_chain built("sync_nothing", trunk_len);
    built.add_headers(built.trunk, 1);

    auto const ready = built.chain().synchronization();
    CHECK_FALSE(ready.caught_up);
    CHECK_FALSE(ready.fresh);
    CHECK_FALSE(ready.synchronized());
}

TEST_CASE("a connected chain at its header tip is caught up", "[chain_view][sync]") {
    built_chain built("sync_caught_up", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto const ready = built.chain().synchronization();
    CHECK(ready.caught_up);
    CHECK(ready.fresh);
    CHECK(ready.synchronized());
}

TEST_CASE("a caught-up chain whose tip is old is not synchronized", "[chain_view][sync]") {
    // Caught up and stale is a different problem from behind — a clock or a
    // connectivity one — with a different remedy, which is why the two are named
    // apart rather than folded into one answer.
    //
    // The chain cannot simply be built old: `utxo_batch_len` takes `is_stale()`
    // and switches to thousand-block batches when it holds, so a short chain old
    // enough to be stale is a chain the connect path never builds. The two
    // notions of "old" share one setting. So the chain is built by a node with
    // the ordinary limit and then read by one configured tighter — the same
    // blocks on disk, judged against a freshness window they fall outside.
    // Built with its own offset rather than the shared one: the default chain
    // ends exactly one hour back, and `fresh` is `stamp + limit >= now`, so
    // against a one-hour limit it sits precisely on the boundary and the answer
    // turns on how many seconds the rest of the suite took. Four hours back is
    // unambiguously outside it, and still well inside the twenty-four hours the
    // building node judges by, so the connect path does not switch to
    // thousand-block batches.
    test::chain_fixture fixture("sync_stale");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 120) * block_spacing;
    std::vector<domain::chain::block> trunk;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }

    auto const added = fixture.organizer().add_headers(headers_of(trunk));
    REQUIRE(added.headers_added == trunk.size());
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);
    REQUIRE(fixture.chain().synchronization().synchronized());

    // Down before the other comes up. The two never share the directory: one
    // process opening a data directory twice is not a supported state — the LMDB
    // environment, UTXO-Z and the flat-file stores each hold handles and cached
    // file state for that path — and a conflict there would surface as a
    // freshness answer that looks like the thing under test.
    fixture.close();

    blockchain::settings strict(domain::config::network::regtest);
    strict.notify_limit_hours = 1u;
    database::settings db_settings;
    db_settings.directory = fixture.dir();

    blockchain::block_chain tight(strict, db_settings, domain::config::network::regtest,
                                  /*relay_transactions*/ false);
    REQUIRE(tight.start(kth::get_disk_magic(domain::config::network::regtest)));

    // start() loads the headers; the active chain by height is materialized by
    // the organizer, and `caught_up` reads the active tip. A node does this at
    // startup, so a test that skipped it would be measuring against a header
    // index that answers nothing — and would read as behind for the wrong
    // reason.
    blockchain::header_organizer organizer(tight.headers(), strict,
                                           domain::config::network::regtest);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    auto const ready = tight.synchronization();
    CHECK(ready.caught_up);
    CHECK_FALSE(ready.fresh);
    CHECK_FALSE(ready.synchronized());
}

TEST_CASE("a limit of zero disables freshness and nothing else", "[chain_view][sync]") {
    // "Do not judge by the clock" is not "you are at the head". A node that has
    // not connected its headers stays unsynchronized however the limit is set.
    test::chain_fixture unlimited("sync_no_limit_zero", /*notify_limit_hours*/ 0u);
    REQUIRE(unlimited.created());
    REQUIRE(unlimited.start());

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;
    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    auto const added = unlimited.organizer().add_headers(headers_of(trunk));
    REQUIRE(added.headers_added == trunk.size());
    persist_headers(unlimited, trunk, 1);

    // Headers in, nothing connected: freshness is switched off, and the node is
    // still not synchronized.
    auto const ready = unlimited.chain().synchronization();
    CHECK(ready.fresh);
    CHECK_FALSE(ready.caught_up);
    CHECK_FALSE(ready.synchronized());
}

// =============================================================================
// Nothing captures while a transition is mutating (#621)
// =============================================================================

TEST_CASE("a template is refused while a transition holds the gate", "[chain_view][gate]") {
    // The refusal a caller sees, and its reason. A transition is not the same
    // answer as a node that is behind: it clears in milliseconds and the caller
    // retries, where the other two clear when the node catches up.
    built_chain built("gate_refusal", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto& chain = built.chain();
    REQUIRE(chain.synchronization().synchronized());

    auto const fetch = [&]() -> std::expected<blockchain::mining_template, code> {
        ::asio::io_context ctx;
        std::optional<std::expected<blockchain::mining_template, code>> out;
        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            out = co_await chain.fetch_mining_template(1000u);
        }, ::asio::detached);
        ctx.run_for(std::chrono::seconds(30));
        REQUIRE(out);
        return *out;
    };

    // Served while nothing is transitioning.
    REQUIRE(fetch().has_value());

    REQUIRE(chain.begin_transition());
    CHECK(chain.transition_in_progress());

    auto const refused = fetch();
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error() == error::transition_in_progress);

    // And served again once the transition publishes and reopens.
    chain.end_transition();
    CHECK_FALSE(chain.transition_in_progress());
    CHECK(fetch().has_value());
}

TEST_CASE("mining info answers while a transition holds the gate", "[chain_view][gate]") {
    // It observes rather than constructs, so it stays available exactly when
    // something is wrong — and it says which of the three reasons applies.
    built_chain built("gate_mining_info", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto& chain = built.chain();
    REQUIRE(chain.begin_transition());

    ::asio::io_context ctx;
    std::optional<blockchain::mining_info> info;
    ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        if (auto const r = co_await chain.fetch_mining_info(); r) info = *r;
    }, ::asio::detached);
    ctx.run_for(std::chrono::seconds(30));

    REQUIRE(info);
    CHECK(info->transition_in_progress);
    CHECK(info->caught_up);
    CHECK(info->fresh);
    CHECK(info->blocks == trunk_len);

    chain.end_transition();
}

TEST_CASE("the gate is not held across the template build", "[chain_view][gate]") {
    // Everything after the capture runs on private copies, so holding the gate
    // there would make a transition wait on work it cannot affect — the
    // exclusion measured in #611 plus the whole build, for nothing.
    //
    // Provoked rather than reasoned: a transition begun while a template is
    // being served must not be blocked by it for anything like the time the
    // build takes.
    built_chain built("gate_released", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto& chain = built.chain();

    std::atomic<bool> serving{true};
    std::thread server([&] {
        for (int i = 0; i < 20; ++i) {
            ::asio::io_context ctx;
            ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
                (void)co_await chain.fetch_mining_template(1000u + uint64_t(i));
            }, ::asio::detached);
            ctx.run_for(std::chrono::seconds(30));
        }
        serving = false;
    });

    // Transitions keep succeeding while templates are being served: the gate is
    // free between captures rather than held for the length of a request.
    //
    // Bounded, because the failure this looks for is a transition that blocks:
    // if the gate were held across a build, `begin_transition` would wait inside
    // the call and an unbounded loop would hang rather than fail. The deadline
    // is generous against twenty template builds and tight against one that
    // never returns.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    int transitions = 0;
    bool timed_out = false;

    // do/while, so at least one transition is attempted even if the server
    // thread finished all twenty builds before this loop was first scheduled.
    // With `while (serving)` the count could be zero for a reason that has
    // nothing to do with the gate, and a test that can pass without exercising
    // what it names is not a test.
    do {
        if (std::chrono::steady_clock::now() > deadline) {
            timed_out = true;
            break;
        }
        if (chain.begin_transition()) {
            chain.end_transition();
            ++transitions;
        }
        std::this_thread::yield();
    } while (serving);
    server.join();

    CHECK_FALSE(timed_out);
    CHECK(transitions > 0);
}

TEST_CASE("a queued rebuild recaptures the chain it builds on", "[chain_view][gate]") {
    // The defect this pins down: a rebuild that reads the chain view under one
    // lease and copies the pool under another. Each half is admitted by the
    // gate, so each half looks correct — but a whole transition can run between
    // them, and the template then names a parent the chain has left behind while
    // selecting from a pool that has moved past it. A capture is the pair, not
    // either half of it.
    //
    // The window is opened rather than waited for. Requests coalesce on the
    // rebuild mutex, so a second request arriving while the first is still
    // building blocks *between* the two reads — exactly where the transition
    // has to land. The transition is driven through the chain's own primitives
    // because here it is the fixture, not the subject: what is under test is
    // what the second request builds on when it wakes.
    built_chain built("gate_recapture", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto& chain = built.chain();
    REQUIRE(chain.synchronization().synchronized());

    // A pool large enough that one build is a window rather than an instant.
    auto& pool = chain.mempool_ref();
    size_t admitted = 0;
    for (uint64_t i = 0; i < pool_entries; ++i) {
        auto const tx = pooled_tx(i);
        admitted += pool.add(blockchain::mempool_entry{
            std::make_shared<domain::message::transaction>(tx),
            /*fee*/ 1000u + i,
            /*size*/ uint32_t(tx.serialized_size(true)),
            /*sigchecks*/ 1u,
            /*time_seen*/ i});
    }
    REQUIRE(admitted == pool_entries);
    REQUIRE(pool.size() == pool_entries);

    // How long a build takes on this machine, measured rather than assumed: the
    // ordering below is expressed in fractions of it, so a slow runner (or a
    // sanitizer build) stretches the window instead of losing it. The builder
    // is a pure function of the entry set, so measuring it costs the chain
    // nothing — in particular it does not warm the template cache, which has to
    // stay cold for the second request to queue rather than be served.
    std::vector<blockchain::mempool_entry> sample_entries;
    sample_entries.reserve(pool_entries);
    pool.for_each([&](blockchain::mempool_entry const& e) { sample_entries.push_back(e); });

    auto const build_time = [&] {
        auto const start = std::chrono::steady_clock::now();
        auto const selection = blockchain::build_block_template(
            sample_entries, blockchain::block_template_context{
                /*max_block_size*/ 32u * 1000u * 1000u,
                /*max_block_sigchecks*/ 1u * 1000u * 1000u,
                /*height*/ trunk_len + 1u,
                /*median_time_past*/ 0u});
        REQUIRE_FALSE(selection.txs.empty());
        return std::chrono::steady_clock::now() - start;
    }();

    // The block the transition connects. Built here so its hash is known to the
    // assertions below.
    auto const extra = test::mine_block(built.trunk.back().hash(), trunk_len + 1u,
        built.base_time + (trunk_len + 1u) * block_spacing, 0, {}, 0);

    using result = std::expected<blockchain::mining_template, code>;
    std::optional<result> first_out;
    std::optional<result> second_out;
    std::atomic<int> completed{0};
    int first_seq = 0;
    int second_seq = 0;

    auto const call = [&](uint64_t reserve, std::optional<result>& out, int& seq) {
        ::asio::io_context ctx;
        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            out = co_await chain.fetch_mining_template(reserve);
        }, ::asio::detached);
        ctx.run_for(std::chrono::seconds(300));
        seq = ++completed;
    };

    // Distinct reserve sizes: the cache is keyed on this too, so the second
    // request cannot be served the first one's snapshot and has to rebuild.
    std::thread first([&] { call(1000u, first_out, first_seq); });
    std::this_thread::sleep_for(build_time / 8);
    std::thread second([&] { call(2000u, second_out, second_seq); });
    std::this_thread::sleep_for(build_time / 8);

    // Recorded here, asserted after the join. A Catch2 macro that fails throws,
    // and throwing with two joinable threads outstanding terminates the process
    // instead of failing the test — the report would be lost at the exact moment
    // it is worth having.
    bool const window_open_before = completed.load() == 0;

    bool const began = chain.begin_transition();
    if (began) {
        built.add_headers(std::vector<domain::chain::block>{extra}, trunk_len + 1u);
    }
    code const published = began ? chain.publish_chain_view(trunk_len + 1u) : code{};

    // Still open when the transition finished, which is what puts the transition
    // strictly between the parked request's two reads.
    bool const window_open_after = completed.load() == 0;

    if (began) {
        chain.end_transition();
    }

    first.join();
    second.join();

    // The fixture held: without these the rest proves nothing.
    REQUIRE(began);
    REQUIRE_FALSE(published);
    REQUIRE(window_open_before);
    REQUIRE(window_open_after);

    // The first request was admitted before the transition and built on what it
    // captured: the old tip. That is correct — a capture admitted before a
    // transition finishes with what it captured.
    REQUIRE(first_out);
    REQUIRE(first_out->has_value());
    CHECK((*first_out)->previous_block_hash == built.trunk.back().hash());
    CHECK((*first_out)->height == trunk_len + 1u);

    // The second was queued behind it, so it finished second — the ordering the
    // whole construction depends on.
    CHECK(first_seq == 1);
    CHECK(second_seq == 2);

    // And it builds on what it recaptured after waking, not on what it read
    // before parking. Reading the chain once at the top and the pool again at
    // the bottom would produce the old tip here, with the new pool.
    REQUIRE(second_out);
    REQUIRE(second_out->has_value());
    CHECK((*second_out)->previous_block_hash == extra.hash());
    CHECK((*second_out)->height == trunk_len + 2u);
    CHECK((*second_out)->previous_block_hash != (*first_out)->previous_block_hash);
}

TEST_CASE("a queued request is not served another request's coinbase budget",
          "[chain_view][gate]") {
    // The other half of the fallback that serves a stale snapshot rather than
    // block: it compared the tip and nothing else. A template is built against a
    // coinbase budget, and the caller passes that number precisely because its
    // coinbase does not fit the default — serving it a snapshot built for a
    // different reserve answers it with someone else's budget, on the right
    // parent, which is the kind of wrong that looks right.
    built_chain built("gate_reserve", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto& chain = built.chain();
    REQUIRE(chain.synchronization().synchronized());

    auto& pool = chain.mempool_ref();
    size_t admitted = 0;
    for (uint64_t i = 0; i < reserve_pool_entries; ++i) {
        auto const tx = pooled_tx(i);
        admitted += pool.add(blockchain::mempool_entry{
            std::make_shared<domain::message::transaction>(tx),
            /*fee*/ 1000u + i,
            /*size*/ uint32_t(tx.serialized_size(true)),
            /*sigchecks*/ 1u,
            /*time_seen*/ i});
    }
    REQUIRE(admitted == reserve_pool_entries);

    auto const fetch = [&](uint64_t reserve) -> std::optional<blockchain::mining_template> {
        ::asio::io_context ctx;
        std::optional<blockchain::mining_template> out;
        ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
            if (auto const r = co_await chain.fetch_mining_template(reserve); r) out = *r;
        }, ::asio::detached);
        ctx.run_for(std::chrono::seconds(300));
        return out;
    };

    // The snapshot the fallback would be tempted to hand out: the current tip,
    // the default budget, and room for the whole pool.
    auto const warm = fetch(1000u);
    REQUIRE(warm);
    REQUIRE(warm->selection.txs.size() > 1000u);

    std::vector<blockchain::mempool_entry> sample_entries;
    sample_entries.reserve(reserve_pool_entries);
    pool.for_each([&](blockchain::mempool_entry const& e) { sample_entries.push_back(e); });

    auto const build_time = [&] {
        auto const start = std::chrono::steady_clock::now();
        auto const selection = blockchain::build_block_template(
            sample_entries, blockchain::block_template_context{
                /*max_block_size*/ warm->size_limit,
                /*max_block_sigchecks*/ warm->sigchecks_limit,
                /*height*/ trunk_len + 1u,
                /*median_time_past*/ 0u});
        REQUIRE_FALSE(selection.txs.empty());
        return std::chrono::steady_clock::now() - start;
    }();

    // A budget that leaves room for almost nothing, so what the queued request
    // was given is legible in the answer: its own budget selects a handful of
    // transactions where the warm snapshot holds the whole pool.
    auto const tight_reserve = warm->size_limit - 1000u;

    using result = std::optional<blockchain::mining_template>;
    result holder_out;
    result queued_out;
    std::atomic<int> completed{0};
    int holder_seq = 0;
    int queued_seq = 0;

    auto const call = [&](uint64_t reserve, result& out, int& seq) {
        out = fetch(reserve);
        seq = ++completed;
    };

    // A third budget, so this one misses the cache too and takes the rebuild
    // mutex for the length of a build.
    std::thread holder([&] { call(2000u, holder_out, holder_seq); });
    std::this_thread::sleep_for(build_time / 8);

    // Recorded, not asserted: a failing macro throws, and throwing with joinable
    // threads outstanding terminates the process rather than reporting.
    bool const holding_when_queued = completed.load() == 0;

    std::thread queued([&] { call(tight_reserve, queued_out, queued_seq); });
    std::this_thread::sleep_for(build_time / 8);

    // Still building, so the second request is parked on the mutex rather than
    // past it — which is the only place the fallback is reachable.
    bool const still_holding = completed.load() == 0;

    holder.join();
    queued.join();

    REQUIRE(holding_when_queued);
    REQUIRE(still_holding);

    // It waited instead of being served. Under the tip-only fallback it would
    // have returned first, immediately, with the warm snapshot.
    CHECK(holder_seq == 1);
    CHECK(queued_seq == 2);

    REQUIRE(queued_out);
    CHECK(queued_out->previous_block_hash == warm->previous_block_hash);
    CHECK(queued_out->selection.txs.size() < warm->selection.txs.size());
    CHECK(queued_out->selection.total_size + tight_reserve <= warm->size_limit);
}

// =============================================================================
// A switch that touched nothing publishes nothing (#621)
// =============================================================================

TEST_CASE("a switch rejected before touching the chain reopens without publishing",
          "[chain_view][gate]") {
    // Several rejections happen before the first disconnect and still report the
    // current tip, so height alone cannot tell them from a switch that rewound
    // to where the chain already was. Publishing on those would move the
    // generation and drop the template cache for a switch that did nothing —
    // which is why `switch_result` carries whether anything was mutated rather
    // than leaving it to be inferred.
    built_chain built("gate_no_mutation", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto& chain = built.chain();
    auto const before = chain.chain_view();
    REQUIRE(before);

    // A null head is rejected at the top of switch_to_branch, before anything is
    // read, let alone disconnected.
    auto const outcome = chain.switch_to_branch(
        database::header_index::null_index, trunk_len);

    CHECK_FALSE(outcome.ok);
    CHECK_FALSE(outcome.mutated);
    // It still reports a tip — which is exactly why `mutated` has to be carried.
    CHECK(outcome.validated_tip);

    auto const after = chain.chain_view();
    REQUIRE(after);
    CHECK(after->generation == before->generation);
    CHECK(after->tip_hash == before->tip_hash);
}

TEST_CASE("a fork above the connected tip disconnects nothing", "[chain_view][gate]") {
    // The other shape of "nothing moved": the fork is above where the chain is
    // connected, so the loop never runs. The tip does not move and neither does
    // the generation.
    built_chain built("gate_fork_above", trunk_len);
    built.add_headers(built.trunk, 1);

    // Connect only part of it, so there are headers above the connected tip.
    std::vector<domain::chain::block> first(built.trunk.begin(), built.trunk.begin() + 20);
    connect_bodies(built.fixture, first, 1);

    auto& chain = built.chain();
    auto const before = chain.chain_view();
    REQUIRE(before);
    REQUIRE(before->connected_tip_height == 20u);

    // A branch that actually competes, forking at 30 — above the connected tip
    // at 20. Handing `switch_to_branch` the active chain's own head instead
    // would also be rejected without disconnecting anything, and from the
    // outside the two look identical: same `ok`, same `mutated`, same view. The
    // test would then pass while demonstrating nothing about a fork above the
    // tip. Long enough to be heavier than the ten trunk headers above the fork.
    auto const branch_head = competing_branch(built, 30u, 12u);

    // The precondition that gives the rest its meaning, asserted rather than
    // assumed: this head is off the active chain. Swap in the trunk's own head
    // and the test fails here.
    auto const head_hash = domain::chain::hash(chain.headers().get_header(branch_head));
    REQUIRE(std::none_of(built.trunk.begin(), built.trunk.end(),
        [&](domain::chain::block const& b) { return b.hash() == head_hash; }));

    auto const outcome = chain.switch_to_branch(branch_head, 30u);
    CHECK_FALSE(outcome.mutated);

    auto const after = chain.chain_view();
    REQUIRE(after);
    CHECK(after->generation == before->generation);
    CHECK(after->connected_tip_height == before->connected_tip_height);
}

TEST_CASE("a switch that failed after disconnecting publishes where it stopped",
          "[chain_view][gate]") {
    // The branch between the two above: some blocks came off and then the switch
    // gave up. `mutated` has to be true there, because the chain did move and the
    // published state has to say where it stopped — otherwise the node keeps
    // validating against a tip it no longer has, and the gate never reopens.
    //
    // Provoked with a real failure rather than a seam: an undo record carries the
    // hash of the block that owns it (#604), and `read_undo` refuses one that
    // disagrees. Damaging that hash makes the disconnect of that block fail
    // *before* its inverse delta is applied, which is a clean failure — the
    // blocks already disconnected stay disconnected.
    built_chain built("gate_partial_rewind", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto& chain = built.chain();
    auto const before = chain.chain_view();
    REQUIRE(before);
    REQUIRE(before->connected_tip_height == trunk_len);

    // Disconnection runs newest first, so damaging the second one down lets the
    // top block come off before the failure.
    corrupt_undo_owner(built.fixture, chain, trunk_len - 1u);

    auto const fork_height = trunk_len - 4u;
    auto const head = competing_branch(built, fork_height, /*length*/ 9u);

    REQUIRE(chain.begin_transition());
    auto const outcome = chain.switch_to_branch(head, fork_height);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.mutated);
    REQUIRE(outcome.validated_tip);
    // The top block came off; the damaged one did not.
    CHECK(*outcome.validated_tip == trunk_len - 1u);

    REQUIRE_FALSE(chain.publish_chain_view(*outcome.validated_tip));
    chain.end_transition();

    auto const after = chain.chain_view();
    REQUIRE(after);
    CHECK(after->connected_tip_height == trunk_len - 1u);
    CHECK(after->generation == before->generation + 1u);
    CHECK_FALSE(chain.transition_in_progress());

    // Serving again, and honestly: the chain is now below its headers.
    auto const ready = chain.synchronization();
    CHECK_FALSE(ready.caught_up);
}

TEST_CASE("a switch that failed on its first disconnect publishes nothing",
          "[chain_view][gate]") {
    // The control for the case above. Damaging the undo of the *first* block to
    // come off makes the switch fail before anything moves, so `mutated` is
    // false — which is what shows it describes what happened rather than merely
    // echoing `ok`.
    built_chain built("gate_first_disconnect", trunk_len);
    built.add_headers(built.trunk, 1);
    connect_bodies(built.fixture, built.trunk, 1);

    auto& chain = built.chain();
    auto const before = chain.chain_view();
    REQUIRE(before);

    corrupt_undo_owner(built.fixture, chain, trunk_len);

    auto const head = competing_branch(built, trunk_len - 4u, /*length*/ 9u);

    auto const outcome = chain.switch_to_branch(head, trunk_len - 4u);
    CHECK_FALSE(outcome.ok);
    CHECK_FALSE(outcome.mutated);

    auto const after = chain.chain_view();
    REQUIRE(after);
    CHECK(after->generation == before->generation);
    CHECK(after->connected_tip_height == before->connected_tip_height);
}
