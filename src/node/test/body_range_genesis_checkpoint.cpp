// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <filesystem>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/blockchain/utxo_builder.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/detail/body_range.hpp>
#include <kth/node/sync/orchestrator.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;
using namespace kth::node::sync;

// =============================================================================
// A fresh store whose highest checkpoint is genesis downloads its first bodies
// =============================================================================
//
// A mainnet node started on a new datadir with its effective checkpoint at
// genesis synced every header and then never asked for a single body:
//
//   SLOW block sync held at 1: the UTXO built height could not be read
//   (key_not_found); refusing to validate against a set that cannot say how far
//   it describes
//
// With the checkpoint at genesis there is no FAST range, so the first thing the
// node does with bodies is the post-checkpoint range starting at 1, and that
// range is admitted only once the UTXO set says it describes height 0. A store
// that has just been created holds the empty set, which describes exactly that,
// but nothing had ever written the marker that says so; the only writer is a
// UTXO batch, and no batch can run before the bodies it would apply arrive.
//
// The control drives the REAL handler for "a batch of headers moved the tip" on a
// store made by the same create() a new datadir gets, and watches the REAL
// channel the download supervisor reads. The bodies it asks for are then fed
// through the node's own connect path, so the range is shown to be usable and not
// merely announced.
//
// The two controls after it pin what the absent marker must NOT be read as: a
// connected tip above zero, or a populated set. Both are states of a damaged or
// legacy store, and both must stay held.

namespace {

node::settings make_quiet_settings(std::filesystem::path const& dir) {
    node::settings settings(domain::config::network::regtest);
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    settings.threads = 1;
    settings.hosts_file = (dir / "hosts_nonexistent.dat").string();
    settings.banlist_file = (dir / "banlist_nonexistent.dat").string();
    return settings;
}

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

struct taken_ranges {
    std::optional<block_range_request> last;
    size_t count{0};
};

taken_ranges taken_range(block_download_input_channel& channel) {
    taken_ranges out;
    std::error_code ec;
    while (auto msg = channel.try_receive(ec)) {
        if (auto* range = std::get_if<block_range_request>(&*msg)) {
            out.last = *range;
            ++out.count;
        }
    }
    return out;
}

// The handler, as one coroutine with its own frame: everything it needs is a
// parameter, so the frame owns its copies and the references it keeps name
// objects declared before the io_context that runs it. Started with a future and
// a cancellation slot, so the test waits for THIS coroutine to finish before it
// reads a single result — and when it does not finish in time, cancels it and
// runs it to completion before failing, rather than leaving a frame alive over
// state that is about to be destroyed.
::asio::awaitable<headers_progress> drive(
    body_range_deps deps,
    body_range_log_memory& memory,
    headers_validated result,
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    std::optional<uint32_t> range_end) {
    co_return co_await on_headers_advanced(
        deps, memory, result, blocks_synced_to, headers_synced_to, range_end);
}

headers_progress run_to_completion(::asio::io_context& ctx, ::asio::awaitable<headers_progress> op) {
    ::asio::cancellation_signal cancel;
    auto done = ::asio::co_spawn(ctx, std::move(op),
        ::asio::bind_cancellation_slot(cancel.slot(), ::asio::use_future));
    ctx.run_for(std::chrono::seconds(30));
    if (done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        // Cancel the coroutine and run the context until it has unwound, so the
        // frame is gone — joined, not abandoned — before the state it borrows is
        // destroyed on the way out.
        cancel.emit(::asio::cancellation_type::terminal);
        ctx.restart();
        ctx.run();
        done.wait();
        FAIL("the body-range handler did not complete within 30 seconds");
    }
    return done.get();
}

headers_validated batch(uint32_t height, size_t count) {
    return headers_validated{
        .height = height,
        .count = count,
        .result = {},
        .source_peer = nullptr
    };
}

constexpr uint32_t genesis_checkpoint = 0;
constexpr uint32_t chain_len = 6;

} // namespace

TEST_CASE("body range - a fresh store with its checkpoint at genesis asks for its first bodies",
    "[node][sync][body_range]") {

    chain_fixture fixture("body_range_genesis_checkpoint");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // Headers only: the header sync has run ahead and no body has arrived, which
    // is the state the mainnet node stood in.
    auto const all = make_chain(domain::chain::block::genesis_regtest(), chain_len);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);

    auto const header_tip = uint32_t(fixture.organizer().header_height());
    REQUIRE(header_tip == chain_len);
    auto const before = chain.get_last_heights();
    REQUIRE(before);
    REQUIRE(before->block == 0u);
    REQUIRE(chain.utxo_count().value() == 0u);

    auto settings = make_quiet_settings(fixture.dir());
    node::p2p_node network(settings);
    body_range_log_memory memory;

    ::asio::io_context ctx;
    block_download_input_channel block_input(16);
    body_range_deps const deps{chain, network, block_input, genesis_checkpoint};

    auto const progress = run_to_completion(ctx,
        drive(deps, memory, batch(header_tip, chain_len), 0, 0, std::nullopt));

    // THE assertion. Under the defect nothing is sent: the admission refuses the
    // range because the built height of a new store cannot be read.
    auto const requested = taken_range(block_input);
    REQUIRE(requested.last);
    CHECK(requested.count == 1);
    CHECK(requested.last->start_height == 1u);
    CHECK(requested.last->end_height == header_tip);
    REQUIRE(progress.opened);
    CHECK(*progress.opened == header_tip);
    CHECK(progress.headers_synced_to == header_tip);

    // The bodies that were asked for, through the node's own connect path.
    std::vector<domain::chain::block> const bodies(
        all.begin() + (requested.last->start_height - 1),
        all.begin() + requested.last->end_height);
    REQUIRE(bodies.size() == chain_len);
    connect_bodies(fixture, bodies, requested.last->start_height);

    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    CHECK(*built == header_tip);
    auto const after = chain.get_last_heights();
    REQUIRE(after);
    CHECK(after->block == header_tip);

    // And a restart stands on what was written: the marker the batch published,
    // not the absence it started from.
    REQUIRE(fixture.restart());
    auto const reopened = fixture.chain().get_utxo_built_height();
    REQUIRE(reopened);
    CHECK(*reopened == header_tip);
}

TEST_CASE("body range - an absent marker under a connected tip above zero is held",
    "[node][sync][body_range]") {

    // A store whose connected marker claims blocks that the (empty) UTXO set does
    // not describe: a legacy database, or a corrupted one. The absent marker may
    // be read as height 0, and height 0 is exactly what makes a range starting
    // above 1 ineligible — the set must reach the block below the range.
    chain_fixture fixture("body_range_absent_marker_claimed_tip");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const all = make_chain(domain::chain::block::genesis_regtest(), chain_len);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);
    auto const header_tip = uint32_t(fixture.organizer().header_height());

    constexpr uint32_t claimed = 3;
    REQUIRE(chain.set_last_block_height(claimed) == database::result_code::success);
    REQUIRE_FALSE(chain.get_utxo_built_height());
    REQUIRE(chain.utxo_count().value() == 0u);

    auto settings = make_quiet_settings(fixture.dir());
    node::p2p_node network(settings);
    body_range_log_memory memory;

    ::asio::io_context ctx;
    block_download_input_channel block_input(16);
    body_range_deps const deps{chain, network, block_input, genesis_checkpoint};

    auto const progress = run_to_completion(ctx,
        drive(deps, memory, batch(header_tip, chain_len), claimed, claimed, std::nullopt));

    auto const requested = taken_range(block_input);
    CHECK_FALSE(requested.last);
    CHECK(requested.count == 0);
    CHECK_FALSE(progress.opened);
    CHECK(progress.headers_synced_to == header_tip);
}

TEST_CASE("body range - an absent marker over a populated set is held",
    "[node][sync][body_range]") {

    // Entries in the UTXO store and no marker saying how far they describe: the
    // state the startup reconciliation refuses, and the range admission must
    // refuse it too. Height 0 would be a lie here — the set is not empty.
    chain_fixture fixture("body_range_absent_marker_populated");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const all = make_chain(domain::chain::block::genesis_regtest(), chain_len);
    REQUIRE(fixture.organizer().add_headers(headers_of(all)).headers_added == all.size());
    persist_headers(fixture, all, 1);
    auto const header_tip = uint32_t(fixture.organizer().header_height());

    {
        auto const window = chain.begin_utxo_write().value();
        blockchain::utxo_raw_delta delta;
        hash_digest h{};
        h.fill(0xAB);
        delta.inserts.emplace(
            utxoz::make_outpoint(std::span<uint8_t const, 32>{h.data(), 32}, 0),
            blockchain::utxo_raw_value{std::vector<uint8_t>(8, 0x11), 7});
        REQUIRE(chain.apply_utxo_inserts_raw(window, delta.inserts) ==
                database::result_code::success);
    }
    REQUIRE(chain.utxo_count().value() > 0u);
    REQUIRE_FALSE(chain.get_utxo_built_height());

    auto settings = make_quiet_settings(fixture.dir());
    node::p2p_node network(settings);
    body_range_log_memory memory;

    ::asio::io_context ctx;
    block_download_input_channel block_input(16);
    body_range_deps const deps{chain, network, block_input, genesis_checkpoint};

    auto const progress = run_to_completion(ctx,
        drive(deps, memory, batch(header_tip, chain_len), 0, 0, std::nullopt));

    auto const requested = taken_range(block_input);
    CHECK_FALSE(requested.last);
    CHECK(requested.count == 0);
    CHECK_FALSE(progress.opened);
}
