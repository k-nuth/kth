// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include <kth/blockchain/pools/mempool.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;
using kth::blockchain::mempool_entry;

// =============================================================================
// When the connect path tells the mempool
// =============================================================================
//
// block_chain::mempool_remove_for_block is what the mempool sees; that it does
// the right thing is pinned in the blockchain suite. What matters here is when
// the connect path calls it: only for a block that is actually connected — its
// UTXO delta applied, its undo written, the built-height marker moved. Storing
// bytes is not connecting, and a batch that fails to validate connects nothing.

namespace {

// A transaction the pool will hold, spending a made-up outpoint. Nothing
// validates it here: it is placed in the pool directly, which is what a
// previously admitted transaction looks like from the connect path's side.
domain::chain::transaction pooled_tx(uint32_t tag) {
    hash_digest prevout{};
    prevout[0] = uint8_t(tag);
    prevout[1] = 0xab;

    domain::chain::input::list ins;
    ins.emplace_back(domain::chain::output_point{prevout, 0}, domain::chain::script{}, 0xffffffffu);

    domain::chain::output::list outs;
    domain::chain::output o;
    o.set_value(1000u + tag);
    o.set_script(domain::chain::script{});
    outs.push_back(std::move(o));

    return domain::chain::transaction{1u, 0u, std::move(ins), std::move(outs)};
}

void put_in_pool(blockchain::block_chain& chain, domain::chain::transaction const& tx) {
    auto const ptr = std::make_shared<domain::message::transaction>(tx);
    REQUIRE(chain.mempool_ref().add(mempool_entry{
        ptr, 1000u, static_cast<uint32_t>(ptr->serialized_size(true)), 1u, 1u}));
}

} // namespace

TEST_CASE("connecting a block through the sync path updates the mempool", "[mempool][wiring]") {
    // The transaction has to be one the block can really confirm, which means a
    // spend of a matured coinbase: full validation resolves every prevout, so a
    // block carrying an invented transaction would be rejected and connect
    // nothing — and the test would pass for the wrong reason, having watched a
    // block that never arrived.
    chain_fixture fixture("mp_wire_ok");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    constexpr uint32_t trunk_len = 100;   // coinbase maturity
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    // A spend of block 1's coinbase, which matures exactly at 101, sitting in
    // the pool the way an admitted transaction would.
    auto const& matured = trunk.front().transactions().front();
    constexpr uint64_t fee = 1000;
    auto const spend = spend_p2pkh(matured, 0, matured.outputs()[0].value() - fee,
        chain.chain_settings().enabled_flags());
    put_in_pool(chain, spend);

    auto const bystander = pooled_tx(2);
    put_in_pool(chain, bystander);
    REQUIRE(chain.mempool_ref().size() == 2);

    // The block at 101 carries that same spend.
    auto const blk = mine_block(trunk.back().hash(), 101,
        base_time + 101 * block_spacing, 1, {spend}, fee);
    std::vector<domain::chain::block> const blocks{blk};

    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == 1);
    persist_headers(fixture, blocks, 101);
    connect_bodies(fixture, blocks, 101);

    // Connected: the marker moved.
    auto const built = chain.get_utxo_built_height();
    REQUIRE(built);
    REQUIRE(*built == 101);

    // And what it confirmed is gone, while the rest of the pool is not.
    CHECK_FALSE(chain.mempool_ref().entry(spend.hash()));
    CHECK(chain.mempool_ref().entry(bystander.hash()));
}

TEST_CASE("a block that fails to connect leaves the mempool alone", "[mempool][wiring]") {
    // The direction that shows the update follows connection rather than
    // accompanying it: this block is stored and then rejected by validation, so
    // the delta is never applied and the marker never moves. A cleanup that ran
    // on storage, or before the marker, would evict a transaction that is still
    // unconfirmed — and nothing would put it back.
    chain_fixture fixture("mp_wire_fail");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - 40 * block_spacing;

    auto const confirmed = pooled_tx(1);
    put_in_pool(chain, confirmed);
    REQUIRE(chain.mempool_ref().size() == 1);

    // Claims more than the subsidy: rejected by full validation, which runs
    // above the checkpoint — and regtest has none, so every height is above it.
    auto const blk = mine_block(genesis.hash(), 1, base_time + block_spacing, 0,
        {confirmed}, /*fees*/ 100'000'000);
    std::vector<domain::chain::block> const blocks{blk};

    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == 1);
    persist_headers(fixture, blocks, 1);
    run_connect_tasks(fixture, blocks, 1);

    // Nothing was connected. On a chain where nothing ever did, the marker has
    // not been written at all — which is as much "nothing built" as a zero.
    auto const built = chain.get_utxo_built_height();
    CHECK(( ! built || *built == 0));

    // ...so nothing left the pool.
    CHECK(chain.mempool_ref().size() == 1);
    CHECK(chain.mempool_ref().entry(confirmed.hash()));
}

TEST_CASE("admission and block-connect cleanup run against each other", "[mempool][wiring][stress]") {
    // Scope, so this is not read as more than it is. It exercises the real
    // admission path (transaction_organizer, which takes the validation mutex at
    // low priority) against the real cleanup (which takes it at high priority),
    // and under a thread sanitizer it is where a missing lock would show. It does
    // NOT demonstrate a particular interleaving, and it cannot demonstrate the
    // priority at all: the fixture builds its chain with relay off, which leaves
    // the mutex unprioritized — exclusion holds, ordering between the two sides
    // does not.
    //
    // Nothing is asserted about who won a given race while the threads run. The
    // strong assertions come after the join, past a final cleanup whose outcome
    // does not depend on the scheduler.
    chain_fixture fixture("mp_stress");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // Long enough that the first ten coinbases have matured at the tip.
    constexpr uint32_t trunk_len = 110;
    constexpr size_t spend_count = 10;
    constexpr size_t confirmed_count = 5;

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    std::vector<domain::chain::transaction> spends;
    spends.reserve(spend_count);
    for (size_t i = 0; i < spend_count; ++i) {
        auto const& coinbase = trunk[i].transactions().front();
        spends.push_back(spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - 1000,
            chain.chain_settings().enabled_flags()));
    }

    // The block the cleanup keeps announcing. It is never connected — only read
    // for its transactions — so it does not have to be a block anyone would mine.
    domain::chain::transaction::list block_txs;
    block_txs.push_back(trunk.front().transactions().front());   // stands in for a coinbase
    for (size_t i = 0; i < confirmed_count; ++i) {
        block_txs.push_back(spends[i]);
    }
    domain::chain::block const confirming{domain::chain::header{}, std::move(block_txs)};

    std::atomic<size_t> admitted{0};
    std::vector<std::thread> threads;

    for (size_t i = 0; i < spend_count; ++i) {
        threads.emplace_back([&chain, &spends, &admitted, i] {
            ::asio::io_context ctx;
            auto const tx = std::make_shared<domain::message::transaction>(spends[i]);
            code result = error::operation_failed;
            ::asio::co_spawn(ctx, chain.organize(tx),
                [&result](std::exception_ptr, code ec) { result = ec; });
            ctx.run();
            if ( ! result) {
                admitted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::atomic<bool> done{false};
    std::thread cleaner([&chain, &confirming, &done] {
        while ( ! done.load(std::memory_order_acquire)) {
            chain.mempool_remove_for_block(confirming);
        }
    });

    for (auto& t : threads) {
        t.join();
    }
    done.store(true, std::memory_order_release);
    cleaner.join();

    // A last cleanup with the threads stopped: from here the state is settled.
    chain.mempool_remove_for_block(confirming);

    // Whatever order the races took, the block's transactions are not in the pool.
    for (size_t i = 0; i < confirmed_count; ++i) {
        CHECK_FALSE(chain.mempool_ref().entry(spends[i].hash()));
    }

    // And the ones it never confirmed are still there, if they were admitted at
    // all — they spend different coinbases, so no cleanup could have touched them.
    size_t survivors = 0;
    for (size_t i = confirmed_count; i < spend_count; ++i) {
        if (chain.mempool_ref().entry(spends[i].hash())) {
            ++survivors;
        }
    }
    CHECK(survivors == chain.mempool_ref().size());
    INFO("admitted " << admitted.load() << " of " << spend_count);
}
