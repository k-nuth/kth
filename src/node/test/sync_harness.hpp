// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_TEST_SYNC_HARNESS_HPP
#define KTH_NODE_TEST_SYNC_HARNESS_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <array>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>

#include <test_helpers.hpp>

#include <kth/node/sync/block_tasks.hpp>
#include <kth/node/sync/messages.hpp>
#include <kth/node/sync/reorg.hpp>

#include "../../blockchain/test/regtest_miner.hpp"
#include "../../blockchain/test/reorg_chain_fixture.hpp"

// Driving the node's own connect path from a test: headers into the index and
// the by-height table, bodies through block_storage_task, and utxo_build_task
// reading them back off disk to validate them, apply the UTXO delta and write
// the undo records. Shared by the tests that need a chain that was built the way
// the node builds one, rather than one assembled by hand.

namespace kth::test {

using namespace kth::node::sync;


// Blocks are spaced two minutes apart and the chain ends near the present. The
// node treats an old tip as "still in IBD" and then batches UTXO work a thousand
// blocks at a time, so a chain stuck in 2011 would never build anything here.
constexpr uint32_t block_spacing = 120;

// Serialize a mined block the way a peer would send it and parse it back the way
// the node does, so what reaches storage arrived through the same code path as a
// block off the wire.
inline std::shared_ptr<domain::chain::light_block const> to_light(domain::chain::block const& blk) {
    data_chunk raw(blk.serialized_size());
    byte_writer writer(raw);
    auto const written = blk.to_data(writer);
    REQUIRE(written);

    byte_reader reader(raw);
    auto light = domain::chain::light_block::from_data(reader, true);
    REQUIRE(light);
    return std::make_shared<domain::chain::light_block const>(std::move(*light));
}

// Write the headers into the internal database, standing in for the node's
// header-persist task, which runs when a header sync completes. get_header(height)
// reads that table, and the UTXO build needs it twice over: to rebuild its
// median-time-past window, and to decide whether the chain is still far enough
// behind to batch a thousand blocks at a time. Without it the build sits idle.
//
// Deliberately NOT called after the switch below: rewriting the heights a reorg
// replaced is the reorg's own job, and doing it here would paper over whether it
// actually happens.
inline void persist_headers(test::chain_fixture& fixture, std::vector<domain::chain::block> const& blocks,
                     uint32_t start_height) {
    domain::chain::header::list batch;
    batch.reserve(blocks.size());
    for (auto const& blk : blocks) {
        batch.push_back(blk.header());
    }
    REQUIRE( ! fixture.chain().organize_headers_batch(batch, start_height));
}

inline domain::message::header::list headers_of(std::vector<domain::chain::block> const& blocks) {
    domain::message::header::list headers;
    headers.reserve(blocks.size());
    for (auto const& blk : blocks) {
        headers.push_back(blk.header());
    }
    return headers;
}

// Run the real storage and UTXO-build tasks over `blocks`, a contiguous run
// starting at `start_height` whose headers are already in the index and in the
// by-height table. This is the node's connect path: bodies through
// block_storage_task, then utxo_build_task reading them back off disk to
// validate them, apply the UTXO delta and write the undo records.
// Runs the connect tasks and returns when they are done, whatever the outcome:
// a batch that fails validation ends the UTXO build, and this returns with
// nothing connected. Callers that require progress use connect_bodies below.
//
// `on_fatal` is what the build reports a fatal condition through. Most callers
// want the default below, which fails the test; a caller that is deliberately
// staging one of those conditions passes its own and inspects what came out.
inline void run_connect_tasks(test::chain_fixture& fixture,
                              std::vector<domain::chain::block> const& blocks,
                              uint32_t start_height,
                              std::function<void(std::string const&)> on_fatal) {

    auto& chain = fixture.chain();
    auto const end_height = start_height + uint32_t(blocks.size()) - 1;

    ::asio::io_context ctx;
    block_storage_input_channel input(ctx.get_executor(), 16);
    chunk_validated_channel output(ctx.get_executor(), 256);
    std::atomic<uint32_t> contiguous{start_height};

    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    light.reserve(blocks.size());
    for (auto const& blk : blocks) {
        light.push_back(to_light(blk));
    }

    ::asio::co_spawn(ctx,
        block_storage_task(chain, input, output, start_height, fixture.organizer(), &contiguous),
        ::asio::detached);

    ::asio::co_spawn(ctx,
        utxo_build_task(chain, contiguous, start_height, domain::config::network::regtest,
            [&chain, end_height] {
                auto const built = chain.get_utxo_built_height();
                return built && *built >= end_height;
            },
            std::move(on_fatal)),
        ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, downloaded_chunk{
        .start_height = start_height,
        .chunk_id = 0,
        .blocks = std::move(light),
        .source_peer = nullptr,
        .generation = chain.headers().generation()
    }));
    REQUIRE(input.try_send(std::error_code{}, stop_request{}));

    ctx.run_for(std::chrono::seconds(120));

    // Drain what the storage task reported, so nothing is left owning blocks.
    while (output.try_receive([](std::error_code, chunk_validated) {})) {}
}

// The ordinary form: a fatal condition means the test has found something, so
// it stops here rather than at a height assertion further down that would point
// at the wrong thing.
inline void run_connect_tasks(test::chain_fixture& fixture,
                              std::vector<domain::chain::block> const& blocks,
                              uint32_t start_height) {
    run_connect_tasks(fixture, blocks, start_height,
        [](std::string const& reason) {
            FAIL("the UTXO build reported a fatal condition: " << reason);
        });
}

// For a test that STAGES a fatal condition: the build must report it, and it
// must report that one. A build that stopped for a different reason would leave
// the state under test unreached, and every assertion after it would be about
// something else.
inline void run_connect_tasks_expect_fatal(test::chain_fixture& fixture,
                                           std::vector<domain::chain::block> const& blocks,
                                           uint32_t start_height,
                                           std::string const& expected) {
    std::vector<std::string> fatals;
    run_connect_tasks(fixture, blocks, start_height,
        [&fatals](std::string const& reason) { fatals.push_back(reason); });

    REQUIRE(fatals.size() == 1);
    REQUIRE(fatals.front() == expected);
}

// The same, for a run that is expected to connect: fails here if the tasks
// stalled, rather than at a height assertion further down that would point at
// the wrong thing.
inline void connect_bodies(test::chain_fixture& fixture,
                           std::vector<domain::chain::block> const& blocks,
                           uint32_t start_height) {
    run_connect_tasks(fixture, blocks, start_height);

    auto const built = fixture.chain().get_utxo_built_height();
    REQUIRE(built);
    REQUIRE(*built >= start_height + uint32_t(blocks.size()) - 1);
}

// What the coordinator passes: the real write, through the chain.
inline header_persister real_persister(blockchain::block_chain& chain) {
    return [&chain](domain::chain::header::list const& headers, size_t start_height) {
        return chain.replace_headers_from(headers, start_height);
    };
}

// UTXO-Z answers a probe with "not resolved", not "absent": the active versions
// cannot say, and the older ones were not consulted. Concluding a UTXO is gone
// without resolving the request would report a question that was never finished
// asking, so the miss is carried into a batch this helper owns and resolved.
inline bool utxo_present(blockchain::block_chain& chain, hash_digest const& txid, uint32_t index,
                  uint32_t at_height) {
    auto const direct = chain.get_utxo(domain::chain::output_point{txid, index}, at_height);
    if (direct) {
        return true;
    }
    if (direct.error() != database::result_code::not_resolved) {
        // Only "the active versions cannot answer" is a reason to resolve. Any other
        // code is the store failing, and sweeping on it would answer a question
        // the store just refused.
        throw std::runtime_error(
            "utxo_present: the UTXO store failed; presence is unknown");
    }
    auto const key = utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, index);

    // This helper's OWN batch of one. The store keeps nothing, so nothing here
    // can consume or be consumed by anything else running.
    std::array<utxoz::lookup_request, 1> const own{utxoz::lookup_request{key, at_height}};
    auto const resolved = chain.utxo_resolve(own);
    if ( ! resolved) {
        // "Could not look" is not "not there". Returning false here would make
        // every assertion built on this helper pass or fail for the wrong reason.
        throw std::runtime_error(
            "utxo_present: the resolution could not run; presence is unknown");
    }
    return resolved->found.contains(key);
}


} // namespace kth::test

#endif // KTH_NODE_TEST_SYNC_HARNESS_HPP
