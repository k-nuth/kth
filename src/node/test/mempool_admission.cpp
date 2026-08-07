// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <future>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include <kth/blockchain/pools/mempool.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// Validating a transaction, and admitting one
// =============================================================================
//
// These are two operations, and the difference between them used to be a flag:
// `organize` read `transaction_validation::simulate` and returned early before
// the mempool, so the caller that wanted validation alone set the flag on a
// store entry and called the admitting path anyway. What a call did was decided
// somewhere other than the call.
//
// `transaction_validate` is the explicit path and always was. What is pinned
// here is that the two do different things through their own names — nothing
// else says so once the flag is gone.

namespace {

// Run one of the chain's coroutines where the node runs them: on the chain's own
// priority pool, which its threads are already servicing.
//
// The wait is unbounded on purpose. These coroutines hold the chain and the
// transaction by reference and cannot be cancelled, so a deadline here could
// only abandon one — and an abandoned coroutine would still be running when the
// fixture tears the chain down underneath it. A test that hangs is diagnosable;
// one that returns while its work continues into freed memory is not. Bounding
// how long a test may run is the harness's job, and CI's.
template <typename Make>
code run_on_chain(blockchain::block_chain& chain, Make make) {
    auto future = ::asio::co_spawn(chain.executor(),
        [make = std::move(make)]() -> ::asio::awaitable<code> {
            co_return co_await make();
        },
        ::asio::use_future);

    return future.get();
}

// A chain of `trunk_len` blocks built through the node's own connect path, so
// the UTXO set a transaction is validated against was written the way the node
// writes it.
struct admission_chain {
    test::chain_fixture fixture;
    std::vector<domain::chain::block> trunk;

    explicit admission_chain(char const* tag, uint32_t trunk_len)
        : fixture(tag)
    {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());

        auto const genesis = domain::chain::block::genesis_regtest();
        auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

        auto prev = genesis.hash();
        for (uint32_t h = 1; h <= trunk_len; ++h) {
            trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }

        auto const added = fixture.organizer().add_headers(headers_of(trunk));
        REQUIRE(added.headers_added == trunk_len);
        persist_headers(fixture, trunk, 1);
        connect_bodies(fixture, trunk, 1);

        // No restart. The connect path publishes the chain state at the close of
        // every batch (#605), so the state a transaction is validated against is
        // the one this chain just reached — which is what a running node needs,
        // since a node cannot restart itself to stay correct.
        auto const view = fixture.chain().chain_view();
        REQUIRE(view);
        REQUIRE(view->state->height() == trunk_len + 1u);
        REQUIRE(view->tip_hash == trunk.back().hash());
    }

    blockchain::block_chain& chain() { return fixture.chain(); }

    // A signed spend of the coinbase of block 1, which has matured by the tip of
    // a hundred-block trunk.
    [[nodiscard]]
    domain::chain::transaction matured_spend(uint64_t fee) {
        auto const& coinbase = trunk.front().transactions().front();
        return test::spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - fee,
                                 chain().chain_settings().enabled_flags());
    }
};

constexpr uint32_t trunk_len = 100;
constexpr uint64_t fee = 1000;

} // namespace

TEST_CASE("validating a transaction does not admit it to the mempool", "[mempool][admission]") {
    admission_chain built("validate_only", trunk_len);
    auto& chain = built.chain();

    auto const tx = std::make_shared<domain::message::transaction>(built.matured_spend(fee));
    REQUIRE(chain.mempool_ref().size() == 0);

    auto const ec = run_on_chain(chain, [&chain, tx] { return chain.transaction_validate(tx); });

    // Validation ran and the transaction passed it — otherwise the pool being
    // empty below would prove nothing, since a rejected transaction is not
    // admitted either.
    CHECK(ec == error::success);
    CHECK(chain.mempool_ref().size() == 0);
    CHECK_FALSE(chain.mempool_ref().contains(tx->hash()));
}

TEST_CASE("organizing a valid transaction admits it to the mempool", "[mempool][admission]") {
    admission_chain built("organize_admits", trunk_len);
    auto& chain = built.chain();

    auto const tx = std::make_shared<domain::message::transaction>(built.matured_spend(fee));
    REQUIRE(chain.mempool_ref().size() == 0);

    auto const ec = run_on_chain(chain, [&chain, tx] { return chain.organize(tx); });

    CHECK(ec == error::success);
    CHECK(chain.mempool_ref().size() == 1);
    CHECK(chain.mempool_ref().contains(tx->hash()));
}
