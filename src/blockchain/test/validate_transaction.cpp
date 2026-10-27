// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/blockchain.hpp>

#include <memory>
#include <optional>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kd::chain;
using namespace kth::blockchain;
using namespace kd::machine;

namespace {

transaction_const_ptr ordinary_transaction() {
    hash_digest previous{};
    previous[0] = 1;
    return std::make_shared<domain::message::transaction>(transaction{
        1, 0,
        {input{output_point{previous, 0}, script{}, max_input_sequence}},
        {output{1000, script{data_chunk(25, 0x51), false}, std::nullopt}}
    });
}

code context_free_check(transaction_const_ptr tx, domain::config::network network,
                        block_chain const& chain) {
    ::asio::io_context context;
    settings const config(network);
    validate_transaction validator(context.get_executor(), 1, chain, config);
    auto result = ::asio::co_spawn(context, validator.check(std::move(tx)), ::asio::use_future);
    context.run();
    return result.get();
}

} // namespace

TEST_CASE("mempool structural checks accept ordinary transactions on retargeting networks",
          "[validate transaction tests][mempool]") {
    test::chain_fixture fixture("mempool_structural_check");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const tx = ordinary_transaction();
    REQUIRE(tx->serialized_size(true) > 1u);

    CHECK(context_free_check(tx, domain::config::network::mainnet, fixture.chain()) == error::success);
#if defined(KTH_CURRENCY_BCH)
    CHECK(context_free_check(tx, domain::config::network::chipnet, fixture.chain()) == error::success);
#endif
    CHECK(context_free_check(tx, domain::config::network::regtest, fixture.chain()) == error::success);
}

TEST_CASE("mempool structural checks reject coinbase on non-retargeting networks",
          "[validate transaction tests][mempool]") {
    test::chain_fixture fixture("mempool_coinbase_check");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const genesis = block::genesis_regtest();
    auto const coinbase = std::make_shared<domain::message::transaction>(genesis.transactions().front());

    CHECK(context_free_check(coinbase, domain::config::network::regtest, fixture.chain()) == error::coinbase_transaction);
    CHECK(context_free_check(coinbase, domain::config::network::mainnet, fixture.chain()) == error::coinbase_transaction);
}
