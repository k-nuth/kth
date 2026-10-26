// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/blockchain.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>

#include <memory>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kd::chain;
using namespace kth::blockchain;
using namespace kd::machine;

namespace {

transaction_const_ptr ordinary_transaction(size_t output_script_size = 25) {
    hash_digest previous{};
    previous[0] = 1;
    return std::make_shared<domain::message::transaction>(transaction{
        1, 0,
        {input{output_point{previous, 0}, script{}, max_input_sequence}},
        {output{1000, script{data_chunk(output_script_size, 0x51), false}}}
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

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("batch validation rejects an oversized transaction before resolving its inputs",
          "[validate transaction tests][transaction size]") {
    test::chain_fixture fixture("batch_transaction_size_check");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const oversized = ordinary_transaction(1'000'000);
    REQUIRE(oversized->serialized_size(true) > 1'000'000u);
    auto const block = std::make_shared<domain::message::block>(
        header{}, transaction::list{*oversized});
    settings const config(domain::config::network::regtest);

    // The header/prevout are deliberately absent. Size is context free and must
    // be rejected before header-chain or UTXO lookups can obscure that error.
    CHECK(validate_block_batch(fixture.chain(), config, domain::config::network::regtest,
                               {block}, 1) == error::transaction_size_limit);
}
#endif
