// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>

#include <kth/node/p2p_node.hpp>
#include <kth/node/detail/body_range.hpp>
#include <kth/node/sync/orchestrator.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;
using namespace kth::node::sync;

// =============================================================================
// A header tip that moves past a drained range reopens the download
// =============================================================================
//
// The mainnet run this comes from finished with seven bodies missing. Bodies had
// reached 964759 and the header tip then moved to 964766; the coordinator for the
// range ending at 964759 was complete, every worker that started exited with
// nothing to claim, and 964760-964766 ended the run carrying a header and
// `file=-1`.
//
// The decision was never wrong. It was never ASKED: the only events that looked
// at the body range were progress the node was already making — a block
// validated, a chunk validated, the builder advancing — and all of them go quiet
// exactly when the bodies are level with a tip that then moves on its own.
//
// So the control below drives the REAL handler for "a batch of headers moved the
// tip" and watches the REAL channel the download supervisor reads. A control over
// the decision alone would stay green with the defect fully restored, because
// under the defect the decision is never reached.

namespace {

// A network that connects to nothing, and writes nothing outside the fixture's
// own directory. The two files are named but never created; rooting them at the
// fixture keeps two cases from naming the same path, and the fixture removes the
// directory when it goes.
node::settings make_quiet_settings(std::filesystem::path const& dir) {
    node::settings settings(domain::config::network::regtest);
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.hosts_file = (dir / "hosts_nonexistent.dat").string();
    settings.banlist_file = (dir / "banlist_nonexistent.dat").string();
    return settings;
}

// A contiguous run of blocks whose timestamps end near the present, so the node
// is not judged stale and the builder drains a remainder shorter than a batch.
std::vector<domain::chain::block> make_chain(domain::chain::block const& genesis, uint32_t len) {
    std::vector<domain::chain::block> blocks;
    auto prev = genesis.hash();
    auto const base = uint32_t(zulu_time()) - (len + 2) * block_spacing;
    for (uint32_t i = 0; i < len; ++i) {
        blocks.push_back(mine_block(prev, i + 1, base + i * block_spacing, 0, {}, 0));
        prev = blocks.back().hash();
    }
    return blocks;
}

// A batch the way `header_validation_task` publishes one: `count` is what the
// organizer accepted, `result` is why it stopped, and both travel together.
headers_validated batch(uint32_t height, size_t count, code error = {}) {
    return headers_validated{
        .height = height,
        .count = count,
        .result = error,
        .source_peer = nullptr
    };
}

// What the coordinator asked the download supervisor for, and HOW MANY times.
//
// The count is part of the assertion, not a diagnostic: opening a second range
// over the same heights is in the defect class these controls guard, and keeping
// only the last request would report that as a pass.
struct taken_ranges {
    std::optional<block_range_request> last;
    size_t count{0};

    explicit operator bool() const { return last.has_value(); }
    block_range_request const* operator->() const { return &*last; }
};

taken_ranges taken_range(block_download_input_channel& channel) {
    taken_ranges out;
    while (channel.try_receive([&out](std::error_code, block_download_input msg) {
        if (auto* range = std::get_if<block_range_request>(&msg)) {
            out.last = *range;
            ++out.count;
        }
    })) {}
    return out;
}

constexpr uint32_t connected_len = 4;   // stands for 964759
constexpr uint32_t stranded_len = 7;    // stands for 964760-964766

} // namespace

TEST_CASE("body range - the seven bodies a moving tip left behind are asked for",
    "[node][sync][body_range]") {

    chain_fixture fixture("body_range_reopen");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // Every header the node has: the connected run and the seven above it. This
    // is the header sync having run ahead of the bodies, which is the shape the
    // mainnet node was in.
    auto const all = make_chain(domain::chain::block::genesis_regtest(),
        connected_len + stranded_len);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);

    // The bodies reach the end of the range that was opened for them, and the
    // UTXO set with them. From here nothing else in the node is moving: no block
    // is validating, no chunk is arriving, and the builder has stopped advancing.
    std::vector<domain::chain::block> const connected(all.begin(), all.begin() + connected_len);
    connect_bodies(fixture, connected, 1);

    auto const built_before = chain.get_utxo_built_height();
    REQUIRE(built_before);
    REQUIRE(*built_before == connected_len);

    // The fixture, made explicit: the tip really is above the bodies, and the
    // range that was in flight really did drain. Without both, what follows would
    // be measuring a range that was owed for some other reason.
    auto const header_tip = uint32_t(fixture.organizer().header_height());
    REQUIRE(header_tip == connected_len + stranded_len);
    REQUIRE(header_tip > connected_len);

    ::asio::io_context ctx;
    block_download_input_channel block_input(ctx.get_executor(), 16);

    // A checkpoint below the bodies: these heights are the post-checkpoint range's
    // to ask for, which is the case the defect was in.
    auto settings = make_quiet_settings(fixture.dir());
    node::p2p_node network(settings);
    body_range_deps deps{chain, network, block_input, 1};
    body_range_log_memory memory;

    std::optional<uint32_t> opened;
    uint32_t moved_to = 0;
    bool ran = false;

    // NAMED, not invoked in place. An immediately-invoked lambda coroutine
    // destroys the closure the moment the call returns, and the coroutine's
    // captures point into it — the frame resumes on a dangling closure.
    auto drive = [&]() -> ::asio::awaitable<void> {
        auto const progress = co_await on_headers_advanced(
            deps, memory, batch(header_tip, stranded_len),
            connected_len,          // bodies
            connected_len,          // the tip before this batch
            connected_len);         // the range that drained
        opened = progress.opened;
        moved_to = progress.headers_synced_to;
        ran = true;
        co_return;
    };
    ::asio::co_spawn(ctx, drive(), ::asio::detached);

    ctx.run_for(std::chrono::seconds(30));
    REQUIRE(ran);

    // THE assertion. Under the defect nothing is sent at all: the handler updated
    // the tip, asked for the next header batch and returned.
    auto const requested = taken_range(block_input);
    REQUIRE(requested);
    CHECK(requested.count == 1);
    CHECK(requested->start_height == connected_len + 1);
    CHECK(requested->end_height == header_tip);
    CHECK(requested->end_height - requested->start_height + 1 == stranded_len);

    // And it is recorded, so the next tip that moves is measured against it
    // rather than against a range that is no longer in flight.
    REQUIRE(opened);
    CHECK(*opened == header_tip);
    CHECK(moved_to == header_tip);

    // -------------------------------------------------------------------------
    // And the seven, through the node's own connect path
    // -------------------------------------------------------------------------
    //
    // Driven from what was ACTUALLY requested, not from what the test knows: with
    // the trigger removed there is no request, the bodies below are never fed,
    // and the seven heights keep their headers with no data — which is how the
    // run this comes from ended.
    std::vector<domain::chain::block> const stranded(
        all.begin() + (requested->start_height - 1),
        all.begin() + requested->end_height);
    REQUIRE(stranded.size() == stranded_len);

    connect_bodies(fixture, stranded, requested->start_height);

    // Seven blocks, applied without waiting for a batch of a thousand.
    auto const built_after = chain.get_utxo_built_height();
    REQUIRE(built_after);
    CHECK(*built_after == header_tip);
    CHECK(*built_after - *built_before == stranded_len);

    // The three markers describe the same block: header tip, connected tip and
    // the height the UTXO set describes.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == header_tip);
    CHECK(heights->block == *built_after);
}

TEST_CASE("body range - a valid prefix that ends in an error still owes the bodies",
    "[node][sync][body_range]") {

    // `header_validation_task` publishes ONE message for both facts: `count` is
    // what the organizer accepted and `result` is why it stopped. A batch that
    // added a valid prefix and then hit a bad header carries count > 0 AND an
    // error, and that prefix moved the tip exactly as a clean batch would.
    //
    // Reading the error here would leave the bodies behind a tip that really did
    // advance: the same defect this change is about, reached through the door
    // that reports a peer instead of the one that asks for the next batch.
    chain_fixture fixture("body_range_prefix_error");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const all = make_chain(domain::chain::block::genesis_regtest(),
        connected_len + stranded_len);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);
    connect_bodies(fixture, {all.begin(), all.begin() + connected_len}, 1);

    auto const built_before = chain.get_utxo_built_height();
    REQUIRE(built_before);
    REQUIRE(*built_before == connected_len);

    auto const header_tip = uint32_t(fixture.organizer().header_height());
    REQUIRE(header_tip == connected_len + stranded_len);

    auto settings = make_quiet_settings(fixture.dir());
    node::p2p_node network(settings);

    ::asio::io_context ctx;
    block_download_input_channel block_input(ctx.get_executor(), 16);
    body_range_deps deps{chain, network, block_input, 1};
    body_range_log_memory memory;

    headers_progress progress{0, std::nullopt};
    bool ran = false;

    auto drive = [&]() -> ::asio::awaitable<void> {
        // Seven accepted, and then the batch failed.
        progress = co_await on_headers_advanced(
            deps, memory,
            batch(header_tip, stranded_len, error::operation_failed),
            connected_len, connected_len, connected_len);
        ran = true;
        co_return;
    };
    ::asio::co_spawn(ctx, drive(), ::asio::detached);
    ctx.run_for(std::chrono::seconds{30});     // watchdog
    REQUIRE(ran);

    // The prefix moved the tip...
    CHECK(progress.headers_synced_to == header_tip);

    // ...and the bodies it left behind were asked for. Exactly one range.
    auto const requested = taken_range(block_input);
    REQUIRE(requested);
    CHECK(requested.count == 1);
    CHECK(requested->start_height == connected_len + 1);
    CHECK(requested->end_height == header_tip);
    REQUIRE(progress.opened);
    CHECK(*progress.opened == header_tip);

    // And the seven, through the node's own connect path, driven from what was
    // actually requested.
    std::vector<domain::chain::block> const stranded(
        all.begin() + (requested->start_height - 1),
        all.begin() + requested->end_height);
    REQUIRE(stranded.size() == stranded_len);
    connect_bodies(fixture, stranded, requested->start_height);

    auto const built_after = chain.get_utxo_built_height();
    REQUIRE(built_after);
    CHECK(*built_after == header_tip);

    auto const heights = chain.get_last_heights();
    REQUIRE(heights);
    CHECK(heights->block == header_tip);
}

TEST_CASE("body range - a batch that accepted nothing moves nothing and opens nothing",
    "[node][sync][body_range]") {

    // The other half of the rule above: `count == 0` is a peer to retry with, not
    // a tip that advanced — with an error and without one. Without this, the case
    // above could be passing because the error is ignored altogether.
    //
    // Deliberately no bodies are connected: `count == 0` answers before the chain
    // is consulted at all, and building a UTXO set to prove that would only make
    // the suite slower.
    chain_fixture fixture("body_range_no_progress");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const all = make_chain(domain::chain::block::genesis_regtest(),
        connected_len + stranded_len);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);

    auto settings = make_quiet_settings(fixture.dir());
    node::p2p_node network(settings);

    ::asio::io_context ctx;
    block_download_input_channel block_input(ctx.get_executor(), 16);
    body_range_deps deps{chain, network, block_input, 1};
    body_range_log_memory memory;

    auto const header_tip = uint32_t(fixture.organizer().header_height());

    headers_progress after_error{0, std::nullopt};
    headers_progress after_clean{0, std::nullopt};
    int ran = 0;

    auto drive = [&]() -> ::asio::awaitable<void> {
        // A failed batch that accepted nothing, reporting a height it did not
        // reach.
        after_error = co_await on_headers_advanced(
            deps, memory, batch(header_tip, 0, error::operation_failed),
            connected_len, connected_len, connected_len);
        ++ran;

        // And a clean batch that simply had nothing to add.
        after_clean = co_await on_headers_advanced(
            deps, memory, batch(connected_len, 0),
            connected_len, connected_len, connected_len);
        ++ran;
        co_return;
    };
    ::asio::co_spawn(ctx, drive(), ::asio::detached);
    ctx.run_for(std::chrono::seconds{30});     // watchdog
    REQUIRE(ran == 2);

    // The tip stays where it was — NOT at the height the failed batch reported.
    CHECK(after_error.headers_synced_to == connected_len);
    CHECK_FALSE(after_error.opened);

    CHECK(after_clean.headers_synced_to == connected_len);
    CHECK_FALSE(after_clean.opened);

    // And nothing was asked for, either time.
    CHECK(taken_range(block_input).count == 0);
}

// -----------------------------------------------------------------------------
// Holding an advance is not losing it
// -----------------------------------------------------------------------------

TEST_CASE("body range - an advance held while a range is in flight is opened when it drains",
    "[node][sync][body_range]") {

    // The sequence that would turn "leave the range in flight alone" into a
    // second way of losing bodies:
    //
    //   1. H+1..H+3 in flight
    //   2. the tip moves to H+7      -> nothing opened, and nothing lost
    //   3. H+1..H+3 drains
    //   4. NO further header event   -> H+4..H+7 must still be opened
    //
    // Step 4 is the one that matters. If the only door were the header event of
    // step 2, holding at step 2 would drop the advance for good.
    chain_fixture fixture("body_range_held");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const all = make_chain(domain::chain::block::genesis_regtest(),
        connected_len + stranded_len);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);
    connect_bodies(fixture, {all.begin(), all.begin() + connected_len}, 1);

    ::asio::io_context ctx;
    block_download_input_channel block_input(ctx.get_executor(), 16);

    auto settings = make_quiet_settings(fixture.dir());
    node::p2p_node network(settings);
    body_range_deps deps{chain, network, block_input, 1};
    body_range_log_memory memory;

    auto const tip = uint32_t(fixture.organizer().header_height());
    auto const in_flight_end = connected_len + 3;   // H+1..H+3 was asked for

    std::optional<uint32_t> held;
    bool held_ran = false;

    // Step 2: the tip moves while H+1..H+3 is still being downloaded.
    auto hold = [&]() -> ::asio::awaitable<void> {
        held = (co_await on_headers_advanced(
            deps, memory, batch(tip, stranded_len),
            connected_len, connected_len, in_flight_end)).opened;
        held_ran = true;
        co_return;
    };
    ::asio::co_spawn(ctx, hold(), ::asio::detached);
    ctx.run_for(std::chrono::seconds(30));
    REQUIRE(held_ran);

    // Held, not lost: nothing was asked for while the range was in flight...
    CHECK_FALSE(held);
    REQUIRE(taken_range(block_input).count == 0);

    // Step 3: that range drains, for real — the bodies are stored and the UTXO
    // set is built through them, which is what the admission below reads. A test
    // that only moved a number here would be held by the builder and would not
    // reach the question it is asking.
    connect_bodies(fixture,
        {all.begin() + connected_len, all.begin() + in_flight_end}, connected_len + 1);
    auto const drained = chain.get_utxo_built_height();
    REQUIRE(drained);
    REQUIRE(*drained == in_flight_end);

    std::optional<uint32_t> after_drain;
    bool drain_ran = false;

    // Step 4: the event that drained it, and NO header event after it.
    ctx.restart();
    auto drive = [&]() -> ::asio::awaitable<void> {
        after_drain = co_await open_body_range_if_owed(
            deps, memory, body_range_trigger::block_validated,
            in_flight_end, tip, in_flight_end);
        drain_ran = true;
        co_return;
    };
    ::asio::co_spawn(ctx, drive(), ::asio::detached);
    ctx.run_for(std::chrono::seconds(30));
    REQUIRE(drain_ran);

    // ...and the remainder above it is asked for as soon as it drains, with no
    // header event in between.
    REQUIRE(after_drain);
    CHECK(*after_drain == tip);

    auto const requested = taken_range(block_input);
    REQUIRE(requested);
    CHECK(requested.count == 1);
    CHECK(requested->start_height == in_flight_end + 1);
    CHECK(requested->end_height == tip);

    // Nothing else came out: the held advance did not also arrive late as a
    // second request over the same heights.
    CHECK(taken_range(block_input).count == 0);
}
