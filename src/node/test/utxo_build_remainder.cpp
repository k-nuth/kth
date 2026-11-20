// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_future.hpp>

#include <kth/node/detail/body_range.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sync/orchestrator.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;
using namespace kth::node::sync;

// =============================================================================
// A node whose only peers are behind still builds everything they have
// =============================================================================
//
// A node synchronizing from a peer whose tip was days old stopped a few hundred
// blocks into the chain and stayed there, with no error:
//
//   UTXO built to height 1500/2000
//   SLOW block sync held at 2001: the UTXO set is at 1500 and block 2001 needs
//   it at 2000; waiting for the builder
//
// While the chain is behind, the builder waits for a full batch before it
// builds, because more blocks are on their way. The coordinator asks for the
// next range only once the set describes the block before it. With the bodies
// of one range all stored and fewer of them left than a batch, each waited for
// the other. A peer at the real tip hides it: the headers reach a recent block,
// the chain stops counting as behind, and the builder takes whatever is stored.
// A peer that is itself behind never gets the node there, and the same wait
// left the last blocks of its chain unbuilt once every body had arrived.
//
// The environment below is that one, small: a chain whose newest header is a
// month old, served in two ranges. The storage task, the builder and the
// function that decides which bodies to ask for are the node's own; the test
// stands where the peers and the coordinator's event loop would be, handing
// over the bodies that were asked for and asking again on each event.

namespace {

constexpr uint32_t first_range = 5;
constexpr uint32_t chain_len = 9;
constexpr uint32_t a_month = 30 * 24 * 60 * 60;

node::settings make_quiet_settings(std::filesystem::path const& dir) {
    node::settings settings(domain::config::network::regtest);
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.hosts_file = (dir / "hosts_nonexistent.dat").string();
    settings.banlist_file = (dir / "banlist_nonexistent.dat").string();
    return settings;
}

// A chain whose last block is `age` seconds old.
std::vector<domain::chain::block> make_chain_aged(uint32_t len, uint32_t age) {
    std::vector<domain::chain::block> blocks;
    auto prev = domain::chain::block::genesis_regtest().hash();
    auto const base = uint32_t(zulu_time()) - age - len * block_spacing;
    for (uint32_t h = 1; h <= len; ++h) {
        blocks.push_back(mine_block(prev, h, base + h * block_spacing, 0, {}, 0));
        prev = blocks.back().hash();
    }
    return blocks;
}

std::optional<block_range_request> taken_range(block_download_input_channel& channel) {
    std::optional<block_range_request> last;
    std::error_code ec;
    while (auto msg = channel.try_receive(ec)) {
        if (auto* range = std::get_if<block_range_request>(&*msg)) {
            last = *range;
        }
    }
    return last;
}

// What a run reached, read after every task has ended.
struct outcome {
    bool deadline_fired{false};
    // The step the script was at when it ended, for the failure message.
    std::string step{"not started"};
    std::vector<std::string> fatals;
};

// What a script has in its hands: the place of the peers and of the
// coordinator's event loop, over the node's own tasks.
struct rig {
    blockchain::block_chain& chain;
    body_range_deps const& deps;
    body_range_log_memory& memory;
    block_download_input_channel& requests;
    outcome& out;
    // The peers: the bodies of these heights, stored.
    std::function<::asio::awaitable<bool>(uint32_t, uint32_t)> deliver;
    // The builder's reports, until the set describes the height.
    std::function<::asio::awaitable<bool>(uint32_t)> built_through;
    // Time for a builder that has been rung to do what it is going to do.
    std::function<::asio::awaitable<void>()> settle;
};

using script = std::function<::asio::awaitable<void>(rig&)>;

// The storage task and the builder over `fixture`, from `start_height`, with
// `steps` standing where the peers and the coordinator would be.
// `tell_the_builder` is whether the coordinator has a channel to tell the
// builder that a remainder is all there is. `stored_through` is the last body
// already on disk when the tasks start.
outcome run_node_tasks(chain_fixture& fixture, std::vector<domain::chain::block> const& blocks,
        bool tell_the_builder, std::chrono::milliseconds deadline, script const& steps,
        uint32_t start_height = 1, uint32_t stored_through = 0) {
    auto& chain = fixture.chain();
    outcome out;

    auto settings = make_quiet_settings(fixture.dir());
    node::p2p_node network(settings);
    body_range_log_memory memory;

    ::asio::io_context ctx;
    block_download_input_channel requests(16);
    block_storage_input_channel input(ctx.get_executor(), 16);
    chunk_validated_channel stored(ctx.get_executor(), 256);
    utxo_build_wake_channel wake(ctx.get_executor(), 1);
    utxo_build_remainder_channel remainder(ctx.get_executor(), 1);
    utxo_build_progress_channel progress(ctx.get_executor(), 1);
    std::atomic<uint32_t> contiguous{std::max(start_height, stored_through + 1)};
    bool stop = false;

    body_range_deps const deps{chain, network, requests, 0,
        tell_the_builder ? &remainder : nullptr, tell_the_builder ? &wake : nullptr};

    auto const close_everything = [&] {
        stop = true;
        input.cancel();
        input.close();
        stored.cancel();
        stored.close();
        progress.cancel();
        progress.close();
        wake.cancel();
        wake.close();
    };

    ::asio::steady_timer watchdog(ctx);
    watchdog.expires_after(deadline);
    watchdog.async_wait([&](std::error_code ec) {
        if (ec) return;
        out.deadline_fired = true;
        close_everything();
    });

    auto storage = ::asio::co_spawn(ctx,
        block_storage_task(chain, input, stored, std::max(start_height, stored_through + 1),
            fixture.organizer(), &contiguous, &wake),
        ::asio::use_future);

    auto builder = ::asio::co_spawn(ctx,
        utxo_build_task(chain, contiguous, wake, start_height, domain::config::network::regtest,
            [&stop] { return stop; },
            [&out](std::string const& reason) { out.fatals.push_back(reason); },
            &progress, nullptr, nullptr, tell_the_builder ? &remainder : nullptr),
        ::asio::use_future);

    rig hands{chain, deps, memory, requests, out,
        [&](uint32_t from, uint32_t to) -> ::asio::awaitable<bool> {
            std::vector<std::shared_ptr<domain::chain::light_block const>> light;
            for (uint32_t h = from; h <= to; ++h) {
                light.push_back(to_light(blocks[h - 1]));
            }
            auto [send_ec] = co_await input.async_send(std::error_code{}, downloaded_chunk{
                .start_height = from,
                .chunk_id = from,
                .blocks = std::move(light),
                .source_peer = nullptr,
                .generation = chain.headers().generation()
            }, ::asio::as_tuple(::asio::use_awaitable));
            if (send_ec) co_return false;
            auto [stored_ec, report] = co_await stored.async_receive(::asio::as_tuple(::asio::use_awaitable));
            co_return ! stored_ec;
        },
        [&](uint32_t height) -> ::asio::awaitable<bool> {
            for (;;) {
                if (auto const built = chain.get_utxo_built_height(); built && *built >= height) {
                    refresh_validated_tip(chain, fixture.organizer());
                    co_return true;
                }
                auto [ec, advanced] = co_await progress.async_receive(::asio::as_tuple(::asio::use_awaitable));
                if (ec) co_return false;
            }
        },
        [&]() -> ::asio::awaitable<void> {
            // A build that started leaves this thread for the validation pool,
            // so no number of turns of this context shows that none did.
            ::asio::steady_timer timer(ctx);
            timer.expires_after(std::chrono::milliseconds(400));
            (void) co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
        }};

    // However the steps end, the tasks are let go: by the watchdog when a wait
    // never ended, and here otherwise.
    auto run = ::asio::co_spawn(ctx, [&]() -> ::asio::awaitable<void> {
        co_await steps(hands);
        watchdog.cancel();
        close_everything();
    }, ::asio::use_future);

    ctx.run();
    run.get();
    storage.get();
    builder.get();
    return out;
}

// What the coordinator decided at each event of a synchronization in two
// ranges.
struct two_ranges {
    std::optional<uint32_t> first_opened;
    std::optional<uint32_t> held;
    std::optional<uint32_t> second_opened;
    std::optional<uint32_t> at_the_tip;
};

// The whole synchronization of a chain of `tip` blocks, served as
// 1..first_range and then the rest.
script synchronize_in_two_ranges(two_ranges& seen, uint32_t tip) {
    return [&seen, tip](rig& r) -> ::asio::awaitable<void> {
        // The first headers arrive, and their bodies are asked for and stored.
        r.out.step = "asking for the first range";
        seen.first_opened = co_await open_body_range_if_owed(r.deps, r.memory,
            body_range_trigger::headers_advanced, 0, first_range, std::nullopt);
        if ( ! seen.first_opened || ! taken_range(r.requests)) co_return;
        r.out.step = "storing the first range";
        if ( ! co_await r.deliver(1, first_range)) co_return;

        // The rest of the headers arrive. The next range needs the set at the
        // end of the first, and the builder has fewer blocks than a batch.
        r.out.step = "asking for the second range before the first is built";
        seen.held = co_await open_body_range_if_owed(r.deps, r.memory,
            body_range_trigger::headers_advanced, first_range, tip, seen.first_opened);
        r.out.step = "waiting for the builder to reach the end of the first range";
        if ( ! co_await r.built_through(first_range)) co_return;

        r.out.step = "asking for the second range";
        seen.second_opened = co_await open_body_range_if_owed(r.deps, r.memory,
            body_range_trigger::utxo_build_advanced, first_range, tip, seen.first_opened);
        if ( ! seen.second_opened || ! taken_range(r.requests)) co_return;
        r.out.step = "storing the second range";
        if ( ! co_await r.deliver(first_range + 1, tip)) co_return;

        // Every body there is has arrived: nothing is left to ask for, and the
        // builder has, again, fewer blocks than a batch.
        r.out.step = "asking once the bodies are level with the headers";
        seen.at_the_tip = co_await open_body_range_if_owed(r.deps, r.memory,
            body_range_trigger::chunk_validated, tip, tip, seen.second_opened);
        r.out.step = "waiting for the builder to reach the tip";
        if ( ! co_await r.built_through(tip)) co_return;
        r.out.step = "done";
    };
}

uint32_t built_height(chain_fixture& fixture) {
    auto const built = fixture.chain().get_utxo_built_height();
    return built ? *built : 0u;
}

struct stale_peer_chain {
    chain_fixture fixture;
    std::vector<domain::chain::block> blocks;

    explicit stale_peer_chain(char const* name)
        : fixture(name), blocks(make_chain_aged(chain_len, a_month)) {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == blocks.size());
        persist_headers(fixture, blocks, 1);
        // The condition everything below is about: by its newest header, this
        // node is behind the network, and no peer will ever say otherwise.
        REQUIRE(fixture.chain().is_stale());
    }
};

} // namespace

TEST_CASE("a node whose peers are behind builds every block they have",
    "[node][sync][utxo][remainder]") {

    stale_peer_chain env("utxo_build_remainder_stale_peer");

    two_ranges seen;
    auto const out = run_node_tasks(env.fixture, env.blocks, true, std::chrono::seconds(60),
        synchronize_in_two_ranges(seen, chain_len));

    INFO("the run ended while " << out.step);
    CHECK(out.fatals.empty());
    REQUIRE_FALSE(out.deadline_fired);

    // The first range was asked for at once; the second was held for the
    // builder, and opened when the builder reached the end of the first.
    REQUIRE(seen.first_opened);
    CHECK(*seen.first_opened == first_range);
    CHECK_FALSE(seen.held);
    REQUIRE(seen.second_opened);
    CHECK(*seen.second_opened == chain_len);
    CHECK_FALSE(seen.at_the_tip);

    // THE assertion: the set describes the last block the peers had.
    CHECK(built_height(env.fixture) == chain_len);
}

TEST_CASE("without the coordinator's word a builder that is behind waits for a full batch",
    "[node][sync][utxo][remainder]") {

    // The control for the case above: the same chain and the same tasks, with
    // nothing to tell the builder that the blocks it has are all there are. It
    // waits for a batch it will never have, the second range is never asked
    // for, and the run ends at its deadline. This is the wait the coordinator's
    // word exists to end, and it must stay a wait wherever that word has not
    // been given: a builder that took short batches whenever it caught up with
    // the download would pay a durability barrier for each of them.
    stale_peer_chain env("utxo_build_remainder_control");

    two_ranges seen;
    auto const out = run_node_tasks(env.fixture, env.blocks, false, std::chrono::seconds(2),
        synchronize_in_two_ranges(seen, chain_len));

    INFO("the run ended while " << out.step);
    CHECK(out.fatals.empty());
    CHECK(out.deadline_fired);
    CHECK(out.step == "waiting for the builder to reach the end of the first range");
    CHECK_FALSE(seen.second_opened);
    CHECK(built_height(env.fixture) == 0u);
}

TEST_CASE("a lower height told to the builder takes the place of a higher one",
    "[node][sync][utxo][remainder]") {
    // The coordinator's cursor moves back when the chain switches or a block is
    // refused, and the height it tells then is lower than the one before. The
    // builder goes by the last it was told: above it, blocks are on their way
    // again, and a short batch waits to be full.
    stale_peer_chain env("utxo_build_remainder_lower");
    bool waited_at_the_lower = false;

    auto const out = run_node_tasks(env.fixture, env.blocks, true, std::chrono::seconds(60),
        [&](rig& r) -> ::asio::awaitable<void> {
            // Told through the tip with the first range stored: it builds that.
            r.out.step = "storing the first range";
            if ( ! co_await r.deliver(1, first_range)) co_return;
            tell_builder_remainder(r.deps, chain_len);
            r.out.step = "waiting for the builder to reach the end of the first range";
            if ( ! co_await r.built_through(first_range)) co_return;

            // Told the end of the first range, lower than before. The rest
            // arrives, fewer blocks than a batch, and is not built.
            tell_builder_remainder(r.deps, first_range);
            r.out.step = "storing the rest";
            if ( ! co_await r.deliver(first_range + 1, chain_len)) co_return;
            co_await r.settle();
            waited_at_the_lower = built_height(env.fixture) == first_range;

            // And told the tip again, it builds them.
            tell_builder_remainder(r.deps, chain_len);
            r.out.step = "waiting for the builder to reach the tip";
            if ( ! co_await r.built_through(chain_len)) co_return;
            r.out.step = "done";
        });

    INFO("the run ended while " << out.step);
    CHECK(out.fatals.empty());
    REQUIRE_FALSE(out.deadline_fired);
    CHECK(waited_at_the_lower);
    CHECK(built_height(env.fixture) == chain_len);
}

TEST_CASE("a node restarted with every body stored builds the ones it had not",
    "[node][sync][utxo][remainder]") {
    // The node stops with the set at the end of the first range and every body
    // on disk. When it starts again no header arrives and no body is owed, so
    // nothing the coordinator hears afterwards would tell the builder; it is
    // told when the header sync finds the bodies level with the headers.
    stale_peer_chain env("utxo_build_remainder_restart");

    auto const before = run_node_tasks(env.fixture, env.blocks, true, std::chrono::seconds(60),
        [&](rig& r) -> ::asio::awaitable<void> {
            r.out.step = "storing the first range";
            if ( ! co_await r.deliver(1, first_range)) co_return;
            tell_builder_remainder(r.deps, first_range);
            r.out.step = "waiting for the builder to reach the end of the first range";
            if ( ! co_await r.built_through(first_range)) co_return;
            r.out.step = "storing the rest";
            if ( ! co_await r.deliver(first_range + 1, chain_len)) co_return;
            co_await r.settle();
            r.out.step = "done";
        });
    INFO("the first run ended while " << before.step);
    REQUIRE_FALSE(before.deadline_fired);
    REQUIRE(before.fatals.empty());
    REQUIRE(built_height(env.fixture) == first_range);

    // The same tasks again, from where the node stopped. Untold, the builder
    // waits; told that the bodies are level with the headers, it builds.
    bool waited_untold = false;
    auto const after = run_node_tasks(env.fixture, env.blocks, true, std::chrono::seconds(60),
        [&](rig& r) -> ::asio::awaitable<void> {
            r.out.step = "starting again";
            co_await r.settle();
            waited_untold = built_height(env.fixture) == first_range;
            tell_builder_remainder(r.deps, chain_len);
            r.out.step = "waiting for the builder to reach the tip";
            if ( ! co_await r.built_through(chain_len)) co_return;
            r.out.step = "done";
        }, first_range + 1, chain_len);

    INFO("the second run ended while " << after.step);
    CHECK(after.fatals.empty());
    REQUIRE_FALSE(after.deadline_fired);
    CHECK(waited_untold);
    CHECK(built_height(env.fixture) == chain_len);
}

TEST_CASE("the height told to the builder is the latest one", "[node][sync][utxo][remainder]") {
    // The channel holds one height. One the builder has not read yet is
    // replaced by the next, so what it reads is where the bodies stand now.
    stale_peer_chain env("utxo_build_remainder_latest");
    auto& chain = env.fixture.chain();

    auto settings = make_quiet_settings(env.fixture.dir());
    node::p2p_node network(settings);
    body_range_log_memory memory;

    ::asio::io_context ctx;
    block_download_input_channel requests(16);
    utxo_build_wake_channel wake(ctx.get_executor(), 1);
    utxo_build_remainder_channel remainder(ctx.get_executor(), 1);
    body_range_deps const deps{chain, network, requests, 0, &remainder, &wake};

    // One event at the coordinator: the bodies and headers stand at these
    // heights, and the last range asked for ended at `range_end`.
    auto const ask = [&](uint32_t bodies, uint32_t headers, uint32_t range_end) {
        auto done = ::asio::co_spawn(ctx, open_body_range_if_owed(deps, memory,
            body_range_trigger::chunk_validated, bodies, headers, std::optional<uint32_t>{range_end}),
            ::asio::use_future);
        ctx.restart();
        ctx.run_for(std::chrono::seconds(30));
        REQUIRE(done.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        return done.get();
    };
    auto const told = [&] {
        std::optional<uint32_t> height;
        (void)remainder.try_receive([&height](std::error_code ec, uint32_t h) { if ( ! ec) height = h; });
        return height;
    };
    auto const rung = [&] { return wake.try_receive([](std::error_code) {}); };

    // Bodies level with the headers: all of them are the remainder.
    CHECK_FALSE(ask(4, 4, 4));
    CHECK(rung());
    // Unread, and then the bodies moved on.
    CHECK_FALSE(ask(7, 7, 7));
    CHECK(rung());
    auto const height = told();
    REQUIRE(height);
    CHECK(*height == 7u);
    CHECK_FALSE(told());

    // A range held for the builder: the remainder ends where the range starts.
    CHECK_FALSE(ask(first_range, chain_len, first_range));
    CHECK(rung());
    auto const held_at = told();
    REQUIRE(held_at);
    CHECK(*held_at == first_range);

    // A range still in flight is not a remainder: its bodies are coming.
    CHECK_FALSE(ask(2, chain_len, chain_len));
    CHECK_FALSE(told());
    CHECK_FALSE(rung());
}
