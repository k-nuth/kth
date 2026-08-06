// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

#include <kth/blockchain/pools/branch.hpp>
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
    built.add_headers(built.trunk, 1);

    std::vector<domain::chain::block> first(built.trunk.begin(), built.trunk.begin() + 20);
    std::vector<domain::chain::block> second(built.trunk.begin() + 20, built.trunk.end());
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
