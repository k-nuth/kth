// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <memory>

#include <kth/blockchain/pools/mempool.hpp>
#include <kth/domain.hpp>
#include <kth/node/transaction_relay.hpp>

using namespace kth;
using domain::message::inventory;
using domain::message::inventory_vector;

namespace {

hash_digest make_hash(uint8_t seed) {
    hash_digest hash{};
    hash[0] = seed;
    hash[31] = seed;
    return hash;
}

inventory make_inventory(inventory_vector::list items) {
    auto inv = inventory::create(std::move(items));
    REQUIRE(inv);
    return *inv;
}

} // namespace

// Start Test Suite: transaction relay tests

TEST_CASE("recent_txids remembers each txid once and forgets the oldest", "[transaction relay]") {
    node::recent_txids recent(2);
    CHECK_FALSE(recent.remember(make_hash(1)));
    CHECK(recent.remember(make_hash(1)));
    CHECK_FALSE(recent.remember(make_hash(2)));
    // A third txid pushes the first out.
    CHECK_FALSE(recent.remember(make_hash(3)));
    CHECK_FALSE(recent.remember(make_hash(1)));
    CHECK(recent.remember(make_hash(3)));
}

TEST_CASE("transactions_to_request asks for unseen transactions only", "[transaction relay]") {
    blockchain::mempool pool{0x1111u, 0x2222u};
    domain::chain::input::list ins;
    ins.emplace_back(domain::chain::output_point{make_hash(9), 0}, domain::chain::script{}, 0xffffffffu);
    domain::chain::output::list outs;
    outs.emplace_back(1000u, domain::chain::script{}, domain::chain::token_data_opt{});
    auto const pooled = std::make_shared<domain::message::transaction>(
        domain::chain::transaction{2u, 0u, std::move(ins), std::move(outs)});
    REQUIRE(pool.add(blockchain::mempool_entry{pooled, 1000u, uint32_t(pooled->serialized_size(true)), 1u, 0u}));

    node::recent_txids recent(100);
    auto const inv = make_inventory({
        {inventory_vector::type_id::transaction, make_hash(1)},
        {inventory_vector::type_id::block, make_hash(2)},
        {inventory_vector::type_id::transaction, pooled->hash()},
        {inventory_vector::type_id::transaction, make_hash(3)},
        {inventory_vector::type_id::transaction, make_hash(1)}});

    // The block entry is not this path's, the pooled transaction is known, and
    // a repeated entry is asked for once.
    auto const wanted = node::transactions_to_request(inv, pool, recent);
    REQUIRE(wanted.size() == 2u);
    CHECK(wanted[0].hash() == make_hash(1));
    CHECK(wanted[1].hash() == make_hash(3));
    // Announced again, by this peer or another: not asked for twice.
    CHECK(node::transactions_to_request(inv, pool, recent).empty());
}
