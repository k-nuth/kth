// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/blockchain.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include "transaction_size_fixture.hpp"

using namespace kth;
using namespace kd::chain;
using namespace kth::blockchain;

namespace {
using kth::test::sized_transaction;

code check_pool(transaction const& tx, settings const& s) {
    ::asio::io_context ctx;
    database::settings db;
    block_chain chain{s, db, domain::config::network::regtest, false};
    validate_transaction validator{ctx.get_executor(), 1, chain, s};
    auto pending = ::asio::co_spawn(ctx, validator.check(
        std::make_shared<domain::message::transaction>(tx)), ::asio::use_future);
    ctx.run();
    return pending.get();
}
}

TEST_CASE("pool structural check supplies size and pool arguments on every network", "[tx_size]") {
    for (auto net : {domain::config::network::mainnet, domain::config::network::chipnet,
                     domain::config::network::regtest}) {
        settings s{net};
        CHECK(check_pool(sized_transaction(99999), s) == error::success);
        CHECK(check_pool(sized_transaction(100, true), s) == error::coinbase_transaction);
    }
}

TEST_CASE("transaction consensus maximum is inclusive and independent of block size", "[tx_size]") {
    for (auto const size : {999999u, 1000000u, 1000001u}) {
        for (bool cb : {false, true}) {
            auto tx = sized_transaction(size, cb);
            auto const expected = size > 1000000 ? code(error::transaction_size_limit) : code(error::success);
            CHECK(tx.check(static_absolute_max_block_size(), false) == expected);
            if (!cb) CHECK(tx.check(static_absolute_max_block_size(), true) == expected);
            // A block below its ABLA limit must still enforce the per-tx cap.
            auto coinbase = test::coinbase_tx(1, 0, 5000000000);
            block candidate{header{}, cb ? transaction::list{tx} : transaction::list{coinbase, tx}};
            candidate = block{header{1, null_hash, candidate.generate_merkle_root(), 0, 0, 0},
                              transaction::list(candidate.transactions())};
            CHECK(candidate.check_body() == expected);
        }
    }
}

TEST_CASE("pool size policy is inclusive and follows consensus", "[tx_size]") {
    settings s{domain::config::network::regtest};
    for (auto const size : {99999u, 100000u, 100001u, 999999u, 1000000u, 1000001u}) {
        CAPTURE(size);
        auto const expected = size > 1000000 ? code(error::transaction_size_limit)
                            : size > 100000 ? code(error::non_standard) : code(error::success);
        CHECK(check_pool(sized_transaction(size), s) == expected);
    }
}

TEST_CASE("disabling the size policy never relaxes the consensus cap", "[tx_size]") {
    settings s{domain::config::network::regtest};
    s.accept_non_standard_transactions = true;
    for (auto const size : {99999u, 100000u, 100001u, 999999u, 1000000u, 1000001u}) {
        CAPTURE(size);
        auto const expected = size > 1000000 ? code(error::transaction_size_limit) : code(error::success);
        CHECK(check_pool(sized_transaction(size), s) == expected);
    }
}

TEST_CASE("size policy defaults follow the reference node per network", "[tx_size]") {
    CHECK_FALSE(settings{}.accept_non_standard_transactions);
    CHECK_FALSE(settings{domain::config::network::mainnet}.accept_non_standard_transactions);
    CHECK_FALSE(settings{domain::config::network::regtest}.accept_non_standard_transactions);
    CHECK(settings{domain::config::network::testnet}.accept_non_standard_transactions);
#if defined(KTH_CURRENCY_BCH)
    CHECK_FALSE(settings{domain::config::network::testnet4}.accept_non_standard_transactions);
    CHECK_FALSE(settings{domain::config::network::chipnet}.accept_non_standard_transactions);
    CHECK(settings{domain::config::network::scalenet}.accept_non_standard_transactions);
#endif
}
