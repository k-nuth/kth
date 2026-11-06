// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <stop_token>
#include <variant>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/detail/local_admission_tasks.hpp>
#include <kth/node/sync/block_tasks.hpp>
#include <kth/node/sync/messages.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::node;
using namespace kth::node::detail;
using namespace kth::node::sync;
using namespace kth::test;

// =============================================================================
// The outcome of a local body reaches its submission, whatever else is full
// =============================================================================
//
// A peer's chunk reports on shared outputs that may refuse under pressure, and
// a refused report is a peer to ask again. A local body has no peer to ask
// again: its one terminal outcome goes to a slot of its own, carried to the
// coordinator by the task that owns the submission. These cases drive the real
// fast validation and storage tasks over real channels with the shared outputs
// FULL and never drained, and read the outcome from the slot.

namespace {

// Run what is ready, and return when nothing more can happen without time
// passing or an outside event.
void drain(::asio::io_context& ioc) {
    ioc.restart();
    while (ioc.poll() > 0) {
    }
}

bool ended(std::future<void> const& task) {
    return task.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

downloaded_chunk local_chunk(uint32_t height, uint64_t submission, uint64_t generation,
                             std::shared_ptr<domain::chain::light_block const> body) {
    return downloaded_chunk{
        .start_height = height,
        .chunk_id = 0,
        .blocks = {std::move(body)},
        .source_peer = nullptr,
        .generation = generation,
        .submission = submission,
        .local_result = std::make_shared<local_body_result_channel>(1, 1)};
}

// A body that is not any header's: an empty raw block under a blank header.
std::shared_ptr<domain::chain::light_block const> blank_body() {
    return std::make_shared<domain::chain::light_block const>(
        domain::chain::header{}, data_chunk{}, std::vector<uint32_t>{});
}

std::optional<chunk_validated> take(local_body_result_channel& slot) {
    std::error_code ec;
    auto taken = slot.try_receive(ec);
    if ( ! taken) {
        return std::nullopt;
    }
    return *taken;
}

std::optional<chunk_validated> next_outcome_event(sync_coordinator_event_channel& events) {
    std::optional<chunk_validated> found;
    while (true) {
        std::optional<sync_coordinator_event> event;
        bool const got = events.try_receive([&](std::error_code, sync_coordinator_event value) {
            event = std::move(value);
        });
        if ( ! got || ! event) {
            break;
        }
        if (auto* typed = std::get_if<chunk_validated>(&*event)) {
            found = *typed;
            break;
        }
    }
    return found;
}

// Fill a shared output of one slot, which is then never drained.
void fill(chunk_validated_channel& output) {
    REQUIRE(output.try_send(std::error_code{}, chunk_validated{
        .start_height = 0, .block_count = 0, .result = error::success, .source_peer = nullptr}));
}

size_t drained_count(chunk_validated_channel& output) {
    size_t n = 0;
    while (output.try_receive([](std::error_code, chunk_validated) {})) {
        ++n;
    }
    return n;
}

} // namespace

// -----------------------------------------------------------------------------
// The handoff task
// -----------------------------------------------------------------------------

TEST_CASE("local body handoff: stopped before it starts, it hands nothing over and ends",
          "[local_admission][tasks][cancellation]") {
    ::asio::io_context ioc;
    fast_validation_input_channel fast_val(4);
    sync_coordinator_event_channel events(ioc.get_executor(), 4);
    std::stop_source stop;
    stop.request_stop();                                // before anything runs

    auto chunk = local_chunk(7, 11, 0, blank_body());
    auto task = ::asio::co_spawn(ioc,
        local_body_handoff(fast_val, std::move(chunk), stop.get_token(), events),
        ::asio::use_future);
    drain(ioc);

    CHECK(ended(task));
    std::error_code ec;
    CHECK_FALSE(bool(fast_val.try_receive(ec)));        // never handed over
    CHECK_FALSE(next_outcome_event(events).has_value());
}

TEST_CASE("local body handoff: stopped while it waits for the outcome, it ends without forwarding",
          "[local_admission][tasks][cancellation]") {
    ::asio::io_context ioc;
    fast_validation_input_channel fast_val(4);
    sync_coordinator_event_channel events(ioc.get_executor(), 4);
    std::stop_source stop;

    auto chunk = local_chunk(7, 11, 0, blank_body());
    auto const slot = chunk.local_result;
    auto task = ::asio::co_spawn(ioc,
        local_body_handoff(fast_val, std::move(chunk), stop.get_token(), events),
        ::asio::use_future);
    drain(ioc);

    // Taken by validation; the task is now waiting on the slot.
    std::error_code ec;
    REQUIRE(bool(fast_val.try_receive(ec)));
    REQUIRE_FALSE(ended(task));

    stop.request_stop();
    drain(ioc);
    CHECK(ended(task));
    CHECK_FALSE(next_outcome_event(events).has_value());

    // An outcome arriving afterwards finds nobody: it stays in the slot, which
    // dies with the submission. Nothing is forwarded by a task that ended.
    REQUIRE(slot->try_send(chunk_validated{
        .start_height = 7, .block_count = 1, .result = error::success,
        .source_peer = nullptr, .submission = 11}));
    drain(ioc);
    CHECK_FALSE(next_outcome_event(events).has_value());
}

TEST_CASE("local body handoff: the outcome is carried to the coordinator with its submission",
          "[local_admission][tasks]") {
    ::asio::io_context ioc;
    fast_validation_input_channel fast_val(4);
    sync_coordinator_event_channel events(ioc.get_executor(), 4);
    std::stop_source stop;

    auto chunk = local_chunk(7, 11, 0, blank_body());
    auto const slot = chunk.local_result;
    auto task = ::asio::co_spawn(ioc,
        local_body_handoff(fast_val, std::move(chunk), stop.get_token(), events),
        ::asio::use_future);
    drain(ioc);

    std::error_code ec;
    auto taken = fast_val.try_receive(ec);
    REQUIRE(bool(taken));
    auto const* handed = std::get_if<downloaded_chunk>(&*taken);
    REQUIRE(handed != nullptr);
    CHECK(handed->submission == 11u);
    CHECK(handed->local_result == slot);

    REQUIRE(slot->try_send(chunk_validated{
        .start_height = 7, .block_count = 1, .result = error::merkle_mismatch,
        .source_peer = nullptr, .submission = 11}));
    drain(ioc);

    CHECK(ended(task));
    auto const carried = next_outcome_event(events);
    REQUIRE(carried.has_value());
    CHECK(carried->submission == 11u);
    CHECK(carried->start_height == 7u);
    CHECK(carried->result == error::merkle_mismatch);
}

TEST_CASE("local body handoff: fast validation closing while it waits for room ends it",
          "[local_admission][tasks][shutdown]") {
    ::asio::io_context ioc;
    fast_validation_input_channel fast_val(1);
    sync_coordinator_event_channel events(ioc.get_executor(), 4);
    std::stop_source stop;

    // No room: the one slot is taken.
    REQUIRE(fast_val.try_send(fast_validation_input{stop_request{}}));

    auto task = ::asio::co_spawn(ioc,
        local_body_handoff(fast_val, local_chunk(7, 11, 0, blank_body()), stop.get_token(), events),
        ::asio::use_future);
    drain(ioc);
    REQUIRE_FALSE(ended(task));                         // waiting for room

    fast_val.close();
    drain(ioc);
    CHECK(ended(task));
    CHECK_FALSE(next_outcome_event(events).has_value());
}

// -----------------------------------------------------------------------------
// Fast validation: a refusal goes to the slot, whatever the shared output holds
// -----------------------------------------------------------------------------

TEST_CASE("fast validation: a local body that does not match its header is refused on its own slot",
          "[local_admission][tasks][fast]") {
    chain_fixture fixture("local_fast_refusal");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const right = mine_block(genesis.hash(), 1, genesis.header().timestamp() + block_spacing, 0, {}, 0);
    domain::chain::transaction::list other;
    other.push_back(coinbase_tx(1, 77, domain::chain::block::subsidy(1, false)));
    domain::chain::block const wrong(right.header(), std::move(other));
    REQUIRE_FALSE(wrong.is_valid_merkle_root());

    ::asio::io_context ioc;
    fast_validation_input_channel input(4);
    chunk_validated_channel output(ioc.get_executor(), 1);
    fill(output);                                       // the shared output, full

    auto task = ::asio::co_spawn(ioc,
        fast_validation_task(chain, input, output, nullptr), ::asio::use_future);

    auto chunk = local_chunk(1, 21, chain.chain_generation(), to_light(wrong));
    auto const slot = chunk.local_result;
    REQUIRE(input.try_send(fast_validation_input{std::move(chunk)}));
    REQUIRE(input.try_send(fast_validation_input{stop_request{}}));
    ioc.run_for(std::chrono::seconds(10));

    CHECK(ended(task));
    auto const outcome = take(*slot);
    REQUIRE(outcome.has_value());
    CHECK(outcome->result == error::merkle_mismatch);
    CHECK(outcome->submission == 21u);
    CHECK(outcome->start_height == 1u);
    // The full shared output was never asked to take it.
    CHECK(drained_count(output) == 1u);
}

// -----------------------------------------------------------------------------
// Storage: stale, stored and failed all answer on the slot
// -----------------------------------------------------------------------------

TEST_CASE("storage: a local body from a branch since abandoned is reported stale on its slot, not stored",
          "[local_admission][tasks][storage][reorg]") {
    chain_fixture fixture("local_storage_stale");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto& index = chain.headers();

    // Stamped now; then the chain moves onto a sibling branch.
    auto const generation_at_handoff = index.generation();
    auto const genesis = index.active_at(0);
    REQUIRE(genesis != blockchain::header_index::null_index);
    auto const genesis_hash = index.get_hash(genesis);
    auto const add_child = [&](uint32_t nonce) {
        domain::chain::header hdr{1, genesis_hash, null_hash, 1000 + nonce, 0x207fffff, nonce};
        auto const r = index.add(domain::chain::hash(hdr), hdr);
        REQUIRE(r.inserted);
        return r.index;
    };
    index.active_set_tip(add_child(1));
    index.active_set_tip(add_child(2));
    REQUIRE(index.generation() > generation_at_handoff);

    ::asio::io_context ioc;
    block_storage_input_channel input(ioc.get_executor(), 4);
    chunk_validated_channel output(ioc.get_executor(), 1);
    fill(output);
    std::atomic<uint32_t> contiguous{1};
    auto task = ::asio::co_spawn(ioc,
        block_storage_task(chain, input, output, 1, fixture.organizer(), &contiguous),
        ::asio::use_future);

    auto chunk = local_chunk(1, 31, generation_at_handoff, blank_body());
    auto const slot = chunk.local_result;
    REQUIRE(input.try_send(std::error_code{}, std::move(chunk)));
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));
    ioc.run_for(std::chrono::seconds(10));

    CHECK(ended(task));
    auto const outcome = take(*slot);
    REQUIRE(outcome.has_value());
    CHECK(outcome->result == error::stale_chain);
    CHECK(outcome->submission == 31u);
    CHECK_FALSE(index.has_block_data(index.active_at(1)));
    CHECK(contiguous.load() == 1u);
    CHECK(drained_count(output) == 1u);
}

TEST_CASE("storage: a local body that is stored is reported stored on its slot, with the shared output full",
          "[local_admission][tasks][storage]") {
    chain_fixture fixture("local_storage_stored");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const one = mine_block(genesis.hash(), 1, genesis.header().timestamp() + block_spacing, 0, {}, 0);
    REQUIRE(fixture.organizer().add_headers(headers_of({one})).headers_added == 1u);

    ::asio::io_context ioc;
    block_storage_input_channel input(ioc.get_executor(), 4);
    chunk_validated_channel output(ioc.get_executor(), 1);
    fill(output);
    std::atomic<uint32_t> contiguous{1};
    auto task = ::asio::co_spawn(ioc,
        block_storage_task(chain, input, output, 1, fixture.organizer(), &contiguous),
        ::asio::use_future);

    auto chunk = local_chunk(1, 41, chain.chain_generation(), to_light(one));
    auto const slot = chunk.local_result;
    REQUIRE(input.try_send(std::error_code{}, std::move(chunk)));
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));
    ioc.run_for(std::chrono::seconds(10));

    CHECK(ended(task));
    auto const outcome = take(*slot);
    REQUIRE(outcome.has_value());
    CHECK(outcome->result == error::success);
    CHECK(outcome->submission == 41u);
    auto const& index = chain.headers();
    CHECK(index.has_block_data(index.find(one.hash())));
    CHECK(contiguous.load() == 2u);
    // Storage did not wait on the full shared output, and did not use it.
    CHECK(drained_count(output) == 1u);
}
