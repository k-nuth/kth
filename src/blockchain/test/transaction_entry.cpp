// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <memory>

#include <kth/blockchain.hpp>

using namespace kth;
using namespace kd::chain;
using namespace kth::blockchain;
using namespace kd::machine;

// Start Test Suite: transaction entry tests

static
auto const default_tx_hash = "f702453dd03b0f055e5437d76128141803984fb10acb85fc3b2184fae2f3fa78"_hash;

static
chain_state::data data() {
    chain_state::data value;
    value.height = 1;
    value.bits = { 0, { 0 } };
    value.version = { 1, { 0 } };
    value.timestamp = { 0, 0, { 0 } };
    return value;
}

static
transaction_const_ptr make_tx() {
    auto const tx = std::make_shared<const domain::message::transaction>();
    tx->validation.state = std::make_shared<chain_state>(
#if defined(KTH_CURRENCY_BCH)
        chain_state {
            data(), 
            0, 
            {}, 
            domain::config::network::testnet4, 
            domain::chain::chain_state::assert_anchor_block_info_t{}, 
            0, 
            abla::config{},
            kth::leibniz_t(0),
            kth::cantor_t(0)
        });
#else
        chain_state {data(), 0, {}});
#endif //KTH_CURRENCY_BCH

    return tx;
}

static
transaction_entry::ptr make_instance() {
    return std::make_shared<transaction_entry>(transaction_entry(make_tx()));
}

// TODO: add populated tx and test property values.

// construct1/tx

TEST_CASE("transaction entry  construct1  default tx  expected values", "[transaction entry tests]") {
    const transaction_entry instance(make_tx());
    REQUIRE(instance.is_anchor());
    REQUIRE(instance.fees() == 0);
    REQUIRE(instance.forks() == 0);
    REQUIRE(instance.sigops() == 0);
    REQUIRE(instance.size() == 10u);
    REQUIRE(instance.hash() == default_tx_hash);
    REQUIRE( ! instance.is_marked());
    REQUIRE(instance.parents().empty());
    REQUIRE(instance.children().empty());
}

// construct2/hash

TEST_CASE("transaction entry  construct1  default block hash  expected values", "[transaction entry tests]") {
    const transaction_entry instance(make_tx()->hash());
    REQUIRE(instance.is_anchor());
    REQUIRE(instance.fees() == 0);
    REQUIRE(instance.forks() == 0);
    REQUIRE(instance.sigops() == 0);
    REQUIRE(instance.size() == 0);
    REQUIRE(instance.hash() == default_tx_hash);
    REQUIRE( ! instance.is_marked());
    REQUIRE(instance.parents().empty());
    REQUIRE(instance.children().empty());
}

// is_anchor

TEST_CASE("transaction entry  is anchor  parents  false", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    auto const parent = make_instance();
    instance.add_parent(parent);
    REQUIRE( ! instance.is_anchor());
}

TEST_CASE("transaction entry  is anchor  children  true", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    auto const child = make_instance();
    instance.add_child(child);
    REQUIRE(instance.is_anchor());
}

// mark

TEST_CASE("transaction entry  mark  true  expected", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    instance.mark(true);
    REQUIRE(instance.is_marked());
}

TEST_CASE("transaction entry  mark  true false  expected", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    instance.mark(true);
    instance.mark(false);
    REQUIRE( ! instance.is_marked());
}

// is_marked

TEST_CASE("transaction entry  mark  default  false", "[transaction entry tests]") {
    const transaction_entry instance(make_tx());
    REQUIRE( ! instance.is_marked());
}

TEST_CASE("transaction entry  is marked  true  true", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    instance.mark(true);
    REQUIRE(instance.is_marked());
}

// add_parent

TEST_CASE("transaction entry  add parent  one  expected parents", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    auto const parent = make_instance();
    instance.add_parent(parent);
    REQUIRE(instance.parents().size() == 1u);
    REQUIRE(instance.parents().front() == parent);
}

// add_child

TEST_CASE("transaction entry  add child  one  expected children", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    auto const child = make_instance();
    instance.add_child(child);
    REQUIRE(instance.children().size() == 1u);
    REQUIRE(instance.children().front() == child);
}

// remove_child

TEST_CASE("transaction entry  remove child  not found  empty", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    auto const child = make_instance();
    instance.remove_child(child);
    REQUIRE(instance.children().empty());
}

TEST_CASE("transaction entry  remove child  only found  empty", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    auto const child = make_instance();
    instance.add_child(child);
    REQUIRE(instance.children().size() == 1u);
    instance.remove_child(child);
    REQUIRE(instance.children().empty());
}

TEST_CASE("transaction entry  remove child  one of two  expected one remains", "[transaction entry tests]") {
    transaction_entry instance(make_tx());
    auto const child1 = make_instance();
    auto const child2 = make_instance();
    instance.add_child(child1);
    instance.add_child(child2);
    REQUIRE(instance.children().size() == 2u);
    instance.remove_child(child1);
    REQUIRE(instance.children().size() == 1u);
    REQUIRE(instance.children().front() == child2);
}

// End Test Suite
