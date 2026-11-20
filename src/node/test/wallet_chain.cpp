// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wallet support answered from the node's own chain, against the blockchain
// test fixture: a regtest chain the node stores. Nothing is asked of any peer.

#include <test_helpers.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/chain/input_prefix_filter.hpp>
#include <kth/node/sync/reorg.hpp>
#include <kth/node/wallet/service.hpp>

#include "sync_harness.hpp"
#include "../../blockchain/test/reorg_chain_fixture.hpp"
#include "../../blockchain/test/regtest_miner.hpp"

using namespace kth;
using namespace std::chrono_literals;
namespace chain = kth::domain::chain;
namespace message = kth::domain::message;
namespace wallet = kth::node::wallet;

namespace {

template <typename Awaitable>
auto wait_for(wallet::service& service, Awaitable&& operation) {
    auto future = ::asio::co_spawn(service.executor(), std::forward<Awaitable>(operation), ::asio::use_future);
    REQUIRE(future.wait_for(60s) == std::future_status::ready);
    return future.get();
}

hash_digest digest_of(uint32_t seed) {
    uint8_t const bytes[] = {uint8_t(seed), uint8_t(seed >> 8), uint8_t(seed >> 16), uint8_t(seed >> 24), 0xc3};
    return sha256_hash(bytes);
}

// Blocks 1 to `length` paying the miner's script; when `spend_at` is set,
// that block spends block 1's coinbase back to the miner, so that coinbase is
// fully spent. The blocks are stored and connected by the fixture.
struct stored_chain {
    std::vector<chain::block> blocks;
    std::optional<chain::transaction> spend;
    test::chain_fixture fixture;

    stored_chain(char const* name, uint32_t length, std::optional<uint32_t> spend_at) : fixture(name) {
        auto const flags = blockchain::settings(domain::config::network::regtest).enabled_flags();
        auto const base_time = uint32_t(zulu_time()) - (length + 30) * test::block_spacing;
        auto previous = chain::block::genesis_regtest().hash();
        for (uint32_t height = 1; height <= length; ++height) {
            std::vector<chain::transaction> txs;
            uint64_t fees = 0;
            if (spend_at && height == *spend_at) {
                auto const& coinbase = blocks.front().transactions().front();
                spend = test::spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - 1000, flags);
                txs.push_back(*spend);
                fees = 1000;
            }
            blocks.push_back(test::mine_block(previous, height, base_time + height * test::block_spacing, 3, std::move(txs), fees));
            previous = blocks.back().hash();
        }
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        REQUIRE(fixture.organizer().add_headers(test::headers_of(blocks)).headers_added == blocks.size());
        test::persist_headers(fixture, blocks, 1);
        test::connect_bodies(fixture, blocks, 1);
        REQUIRE(fixture.chain().chain_view()->connected_tip_height == length);
    }
};

} // namespace

TEST_CASE("wallet from the node's own chain: histories, balances, unspent outputs, RPA and a spent transaction by hash alone", "[wallet][wallet_chain]") {
    stored_chain stored("wallet_chain_answers", 110, 101);
    auto own = wallet::service::from_chain(stored.fixture.chain(), domain::config::network::regtest);
    REQUIRE(own->current().source == "chain");
    REQUIRE(own->current().synced);
    REQUIRE(own->current().tip == 110u);

    // Found by its hash alone, before anything is watched: a coinbase whose
    // only output is spent.
    auto const coinbase_txid = stored.blocks.front().transactions().front().hash();
    auto const found = wait_for(*own, own->transaction(coinbase_txid, std::nullopt));
    REQUIRE(found);
    REQUIRE(found->confirmed);
    REQUIRE(found->confirmed->height == 1u);
    auto const proof = wait_for(*own, own->merkle(coinbase_txid, std::nullopt));
    REQUIRE(proof);
    REQUIRE(chain::block::merkle_branch_root(coinbase_txid, proof->confirmed.position, proof->branch) ==
        stored.blocks.front().header().merkle());
    auto const missing = wait_for(*own, own->transaction(digest_of(4242), std::nullopt));
    REQUIRE( ! missing);
    REQUIRE(missing.error() == error::not_found);

    // Every coinbase and the spend; every coinbase but the first unspent.
    auto const miner = wallet::target::of_script(test::miner_script().bytes());
    auto const history = wait_for(*own, own->history(miner));
    REQUIRE(history);
    REQUIRE(history->synced == 110u);
    REQUIRE(history->confirmed.size() == 111u);
    REQUIRE(history->confirmed[101].txid == stored.spend->hash());
    REQUIRE(history->confirmed[101].height == 101u);
    REQUIRE(history->confirmed[101].inputs.size() == 1u);
    auto const coins = wait_for(*own, own->unspent(miner, wallet::token_filter::include));
    REQUIRE(coins);
    REQUIRE(coins->size() == 110u);
    REQUIRE(std::ranges::none_of(*coins, [&](auto const& coin) { return coin.outpoint == chain::point(coinbase_txid, 0); }));
    auto const balance = wait_for(*own, own->get_balance(miner, wallet::token_filter::include));
    REQUIRE(balance);
    uint64_t total = 0;
    for (auto const& coin : *coins) total += coin.value;
    REQUIRE(balance->confirmed == total);
    REQUIRE(balance->unconfirmed == 0);
    // Tokens only: the miner holds none.
    auto const tokens = wait_for(*own, own->unspent(miner, wallet::token_filter::only));
    REQUIRE(tokens);
    REQUIRE(tokens->empty());

    // RPA: the spend's first input is in its own prefix; a prefix wider than
    // 16 bits is refused.
    auto const value = chain::input_prefix(stored.spend->inputs()[0]);
    auto const rpa = wait_for(*own, own->rpa_history(value, 16, 0, std::nullopt));
    REQUIRE(rpa);
    REQUIRE(std::ranges::any_of(*rpa, [&](auto const& match) { return match.txid == stored.spend->hash() && match.height == 101u; }));
    REQUIRE( ! wait_for(*own, own->rpa_history(0x1ff, 8, 0, std::nullopt)));

    // An output by its outpoint: spent, then unspent.
    auto const spent = wait_for(*own, own->output(chain::point(coinbase_txid, 0)));
    REQUIRE(spent);
    REQUIRE( ! *spent);
    auto const later = stored.blocks[50].transactions().front().hash();
    auto const unspent = wait_for(*own, own->output(chain::point(later, 0)));
    REQUIRE(unspent);
    REQUIRE(*unspent);
    REQUIRE((*unspent)->height == 51u);

    // Header proofs: each branch folds its block hash to the same root.
    auto const branch5 = wait_for(*own, own->header_branch(5, 10));
    auto const branch9 = wait_for(*own, own->header_branch(9, 10));
    REQUIRE(branch5);
    REQUIRE(branch9);
    REQUIRE(branch5->second == branch9->second);
    REQUIRE(chain::block::merkle_branch_root(stored.blocks[4].hash(), 5, branch5->first) == branch5->second);
    REQUIRE( ! wait_for(*own, own->header_branch(11, 10)));
}

TEST_CASE("wallet double-spend proofs: the full node lists its own", "[wallet][wallet_chain]") {
    stored_chain stored("wallet_chain_dsproofs", 110, std::nullopt);
    auto& node_chain = stored.fixture.chain();
    auto const flags = blockchain::settings(domain::config::network::regtest).enabled_flags();

    // A mempool transaction spending block 1's coinbase, and a proof that the
    // same output was spent twice.
    auto const& coinbase = stored.blocks.front().transactions().front();
    auto const pending = test::spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - 1000, flags);
    auto const pooled = std::make_shared<message::transaction const>(pending);
    auto submitted = ::asio::co_spawn(node_chain.executor(), [&node_chain, pooled]() -> ::asio::awaitable<code> {
        co_return co_await node_chain.submit_transaction(pooled);
    }, ::asio::use_future);
    REQUIRE(submitted.get() == error::success);

    auto own = wallet::service::from_chain(node_chain, domain::config::network::regtest);
    // No proof yet: nothing listed.
    REQUIRE(wait_for(*own, own->dsproof_transactions()).empty());
    REQUIRE( ! wait_for(*own, own->dsproof(pending.hash())));

    message::double_spend_proof::spender first{};
    first.out_sequence = 1;
    first.push_data = data_chunk{0x01, 0x02};
    message::double_spend_proof::spender second{};
    second.out_sequence = 2;
    second.push_data = data_chunk{0x03, 0x04};
    auto const proof = std::make_shared<message::double_spend_proof const>(chain::output_point{coinbase.hash(), 0}, first, second);
    auto organized = ::asio::co_spawn(node_chain.executor(), [&node_chain, proof]() -> ::asio::awaitable<code> {
        co_return co_await node_chain.organize(proof);
    }, ::asio::use_future);
    REQUIRE(organized.get() == error::success);

    REQUIRE(wait_for(*own, own->dsproof_transactions()) == std::vector<hash_digest>{pending.hash()});
    auto const by_txid = wait_for(*own, own->dsproof(pending.hash()));
    REQUIRE(by_txid);
    REQUIRE(by_txid->dspid == proof->hash());
    REQUIRE(by_txid->txid == pending.hash());
    REQUIRE(by_txid->outpoint == chain::point(coinbase.hash(), 0));
    auto const by_hash = wait_for(*own, own->dsproof(proof->hash()));
    REQUIRE(by_hash);
    REQUIRE(by_hash->txid == pending.hash());
    // The mempool transaction itself is in the mempool copy.
    auto const pending_found = wait_for(*own, own->transaction(pending.hash(), std::nullopt));
    REQUIRE(pending_found);
    REQUIRE( ! pending_found->confirmed);
}

TEST_CASE("wallet from the node's own chain: a mempool transaction touches the targets it pays and spends from, no others", "[wallet][wallet_chain]") {
    stored_chain stored("wallet_chain_touched", 110, std::nullopt);
    auto& node_chain = stored.fixture.chain();
    auto const flags = blockchain::settings(domain::config::network::regtest).enabled_flags();
    auto const& coinbase = stored.blocks.front().transactions().front();
    auto const miner = wallet::target::of_script(coinbase.outputs()[0].script().bytes());
    auto const other = wallet::target::of_script(data_chunk{0x51});

    auto own = wallet::service::from_chain(node_chain, domain::config::network::regtest);
    REQUIRE(wait_for(*own, own->history(miner)));
    REQUIRE(wait_for(*own, own->history(other)));
    auto const seen = wait_for(*own, own->changed(0, 0s));

    // Block 1's coinbase spent back to the miner, in the mempool.
    auto const pending = test::spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - 1000, flags);
    auto const pooled = std::make_shared<message::transaction const>(pending);
    auto submitted = ::asio::co_spawn(node_chain.executor(), [&node_chain, pooled]() -> ::asio::awaitable<code> {
        co_return co_await node_chain.submit_transaction(pooled);
    }, ::asio::use_future);
    REQUIRE(submitted.get() == error::success);
    auto const after = wait_for(*own, own->history(miner));
    REQUIRE(after);
    REQUIRE(after->unconfirmed.size() == 1u);

    // Who follows the miner or the transaction looks again; who follows a
    // target it did not touch need not.
    auto const change = wait_for(*own, own->touched(seen));
    REQUIRE(change.count > seen);
    REQUIRE(change.keys);
    REQUIRE(std::ranges::binary_search(*change.keys, miner.key()));
    REQUIRE(std::ranges::binary_search(*change.keys, pending.hash()));
    REQUIRE( ! std::ranges::binary_search(*change.keys, other.key()));
}

namespace {

// Mines `count` blocks on `parent` from `first` on, each paying the miner.
std::vector<chain::block> branch_of(hash_digest parent, uint32_t first, uint32_t count, uint32_t salt, uint32_t base_time) {
    std::vector<chain::block> blocks;
    for (uint32_t height = first; height < first + count; ++height) {
        blocks.push_back(test::mine_block(parent, height, base_time + height * test::block_spacing, salt, {}, 0));
        parent = blocks.back().hash();
    }
    return blocks;
}

std::vector<hash_digest> txids_of(wallet::target_history const& history) {
    std::vector<hash_digest> out;
    for (auto const& entry : history.confirmed) out.push_back(entry.txid);
    return out;
}

} // namespace

TEST_CASE("wallet from the node's own chain: a reorganization rolls the histories back and reads the new branch", "[wallet][wallet_chain]") {
    test::chain_fixture fixture("wallet_chain_reorg");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& node_chain = fixture.chain();
    constexpr uint32_t fork_height = 12;
    auto const base_time = uint32_t(zulu_time()) - 60 * test::block_spacing;
    auto const trunk = branch_of(chain::block::genesis_regtest().hash(), 1, fork_height, 0, base_time);
    REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
    test::persist_headers(fixture, trunk, 1);
    test::connect_bodies(fixture, trunk, 1);
    auto const a = branch_of(trunk.back().hash(), fork_height + 1, 2, 1, base_time);
    REQUIRE(fixture.organizer().add_headers(test::headers_of(a)).headers_added == a.size());
    test::persist_headers(fixture, a, fork_height + 1);
    test::connect_bodies(fixture, a, fork_height + 1);

    auto own = wallet::service::from_chain(node_chain, domain::config::network::regtest);
    auto running = ::asio::co_spawn(own->executor(), own->run(), ::asio::use_future);
    auto const miner = wallet::target::of_script(test::miner_script().bytes());
    auto const before = wait_for(*own, own->history(miner));
    REQUIRE(before);
    REQUIRE(before->confirmed.size() == 14u);
    auto const orphaned = a.back().transactions().front().hash();
    auto const listed_before = txids_of(*before);
    REQUIRE(std::ranges::find(listed_before, orphaned) != listed_before.end());

    // A heavier branch from the fork: the node switches to it.
    auto const b = branch_of(trunk.back().hash(), fork_height + 1, 3, 2, base_time);
    auto const added = fixture.organizer().add_headers(test::headers_of(b));
    REQUIRE(added.reorg_candidate);
    {
        ::asio::io_context io;
        auto switched = ::asio::co_spawn(io,
            node::sync::execute_reorg(node_chain, fixture.organizer(), node_chain.headers().find(b.back().hash()),
                fork_height, [] { return false; }, test::real_persister(node_chain)), ::asio::use_future);
        io.run();
        auto const outcome = switched.get();
        REQUIRE_FALSE(outcome.fatal);
        REQUIRE(outcome.result.ok);
    }
    test::connect_bodies(fixture, b, fork_height + 1);
    REQUIRE(node_chain.chain_view()->tip_hash == b.back().hash());

    // Woken by the node's chain views: the orphaned coinbases leave every
    // answer and the new branch's arrive.
    auto seen = wait_for(*own, own->changed(0, 0s));
    auto const deadline = std::chrono::steady_clock::now() + 30s;
    while (own->current().tip != fork_height + 3 && std::chrono::steady_clock::now() < deadline) {
        seen = wait_for(*own, own->changed(seen, 5s));
    }
    auto const after = wait_for(*own, own->history(miner));
    REQUIRE(after);
    auto const listed = txids_of(*after);
    REQUIRE(after->confirmed.size() == 15u);
    for (auto const& blk : a) REQUIRE(std::ranges::find(listed, blk.transactions().front().hash()) == listed.end());
    for (auto const& blk : b) REQUIRE(std::ranges::find(listed, blk.transactions().front().hash()) != listed.end());
    REQUIRE(after->confirmed.back().height == fork_height + 3);
    auto const coins = wait_for(*own, own->unspent(miner, wallet::token_filter::include));
    REQUIRE(coins);
    REQUIRE(coins->size() == 15u);
    // The orphaned coinbase is no longer a confirmed transaction.
    REQUIRE( ! wait_for(*own, own->transaction(orphaned, std::nullopt)));

    own->stop();
    REQUIRE(running.wait_for(30s) == std::future_status::ready);
}

TEST_CASE("wallet from the node's own chain: a birthday reads nothing below it", "[wallet][wallet_chain]") {
    stored_chain stored("wallet_chain_birthday", 60, std::nullopt);
    wallet::chain_settings settings;
    settings.birthday = 40;
    auto own = wallet::service::from_chain(stored.fixture.chain(), domain::config::network::regtest, settings);
    auto const miner = wallet::target::of_script(test::miner_script().bytes());
    auto const history = wait_for(*own, own->history(miner));
    REQUIRE(history);
    // The coinbases from the birthday on, and no block read below it.
    REQUIRE(history->confirmed.size() == 21u);
    REQUIRE(history->confirmed.front().height == 40u);
    REQUIRE(own->current().blocks_downloaded == 21u);
    // A later watch with its own, later birthday reads from there.
    auto const other = wallet::target::of_script(data_chunk{0x51});
    REQUIRE( ! wait_for(*own, own->watch({other}, 55)));
    REQUIRE(own->current().blocks_downloaded == 21u + 6u);
}

TEST_CASE("wallet from the node's own chain: a birthday scan, timed", "[wallet][wallet_chain]") {
    // 400 blocks; from 101 on, each spends the coinbase of the block 100 below.
    constexpr uint32_t length = 400;
    auto const flags = blockchain::settings(domain::config::network::regtest).enabled_flags();
    auto const base_time = uint32_t(zulu_time()) - (length + 30) * test::block_spacing;
    std::vector<chain::block> blocks;
    auto previous = chain::block::genesis_regtest().hash();
    for (uint32_t height = 1; height <= length; ++height) {
        std::vector<chain::transaction> txs;
        uint64_t fees = 0;
        if (height > 100) {
            auto const& coinbase = blocks[height - 101].transactions().front();
            txs.push_back(test::spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - 1000, flags));
            fees = 1000;
        }
        blocks.push_back(test::mine_block(previous, height, base_time + height * test::block_spacing, 5, std::move(txs), fees));
        previous = blocks.back().hash();
    }
    test::chain_fixture fixture("wallet_chain_timing");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    REQUIRE(fixture.organizer().add_headers(test::headers_of(blocks)).headers_added == blocks.size());
    test::persist_headers(fixture, blocks, 1);
    test::connect_bodies(fixture, blocks, 1);
    REQUIRE(fixture.chain().chain_view()->connected_tip_height == length);

    auto const timed = [&](wallet::target const& target) {
        auto own = wallet::service::from_chain(fixture.chain(), domain::config::network::regtest);
        auto const started = std::chrono::steady_clock::now();
        auto history = wait_for(*own, own->history(target));
        auto const seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        REQUIRE(history);
        return std::pair{std::move(*history), seconds};
    };
    auto const [dense, dense_seconds] = timed(wallet::target::of_script(test::miner_script().bytes()));
    auto const [sparse, sparse_seconds] = timed(wallet::target::of_script(data_chunk{0x51}));
    REQUIRE(dense.confirmed.size() == length + (length - 100));
    REQUIRE(sparse.confirmed.empty());
    std::printf("wallet birthday scan of %u blocks (direct parse): a script in every block %.3f s, "
        "a script in none %.3f s\n", length, dense_seconds, sparse_seconds);
    std::fflush(stdout);
}
