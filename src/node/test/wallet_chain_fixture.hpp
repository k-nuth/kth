// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_TEST_WALLET_CHAIN_FIXTURE_HPP
#define KTH_NODE_TEST_WALLET_CHAIN_FIXTURE_HPP

// A regtest chain the node stores, for wallet support answered from the
// node's own chain. Nothing is asked of any peer.

#include <test_helpers.hpp>

#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include <kth/node/wallet/service.hpp>

#include "sync_harness.hpp"
#include "../../blockchain/test/reorg_chain_fixture.hpp"
#include "../../blockchain/test/regtest_miner.hpp"

using namespace kth;
using namespace std::chrono_literals;
namespace chain = kth::domain::chain;
namespace message = kth::domain::message;
namespace wallet = kth::node::wallet;

// Runs `operation` on the service's executor; a minute at most.
template <typename Awaitable>
inline auto wait_for(wallet::service& service, Awaitable&& operation) {
    auto future = ::asio::co_spawn(service.executor(), std::forward<Awaitable>(operation), ::asio::use_future);
    REQUIRE(future.wait_for(60s) == std::future_status::ready);
    return future.get();
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

#endif // KTH_NODE_TEST_WALLET_CHAIN_FIXTURE_HPP
