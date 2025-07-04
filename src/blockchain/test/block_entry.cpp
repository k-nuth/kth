// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <memory>
#include <kth/blockchain.hpp>

using namespace kth;
using namespace kth::blockchain;

// Start Test Suite: block entry tests

static auto const hash42 = hash_literal("4242424242424242424242424242424242424242424242424242424242424242");
static auto const default_block_hash = hash_literal("14508459b221041eab257d2baaa7459775ba748246c8403609eb708f0e57e74b");

// construct1/block

TEST_CASE("block entry  construct1  default block  expected", "[block entry tests]")
{
    auto const block = std::make_shared<const domain::message::block>();
    block_entry instance(block);
    REQUIRE(instance.block() == block);
    REQUIRE(instance.hash() == default_block_hash);
}

// construct2/hash

TEST_CASE("block entry  construct2  default block hash  round trips", "[block entry tests]") {
    block_entry instance(default_block_hash);
    REQUIRE(instance.hash() == default_block_hash);
}

// parent

TEST_CASE("block entry  parent  hash42  expected", "[block entry tests]")
{
    auto const block = std::make_shared<domain::message::block>();
    block->header().set_previous_block_hash(hash42);
    block_entry instance(block);
    REQUIRE(instance.parent() == hash42);
}

// children

TEST_CASE("block entry  children  default  empty", "[block entry tests]") {
    block_entry instance(default_block_hash);
    REQUIRE(instance.children().empty());
}

// add_child

TEST_CASE("block entry  add child  one  single", "[block entry tests]") {
    block_entry instance(null_hash);
    auto const child = std::make_shared<const domain::message::block>();
    instance.add_child(child);
    REQUIRE(instance.children().size() == 1u);
    REQUIRE(instance.children()[0] == child->hash());
}

TEST_CASE("block entry  add child  two  expected order", "[block entry tests]") {
    block_entry instance(null_hash);

    auto const child1 = std::make_shared<const domain::message::block>();
    instance.add_child(child1);

    auto const child2 = std::make_shared<domain::message::block>();
    child2->header().set_previous_block_hash(hash42);
    instance.add_child(child2);

    REQUIRE(instance.children().size() == 2u);
    REQUIRE(instance.children()[0] == child1->hash());
    REQUIRE(instance.children()[1] == child2->hash());
}

// equality

// TODO(2025-July)
TEST_CASE("block entry equality same true", "[block entry tests]") {
    auto const block = std::make_shared<const domain::message::block>();
    block_entry instance1(block);
    block_entry instance2(block->hash());
    REQUIRE(instance1 == instance2);
}

TEST_CASE("block entry  equality  different  false", "[block entry tests]") {
    auto const block = std::make_shared<const domain::message::block>();
    block_entry instance1(block);
    block_entry instance2(null_hash);
    REQUIRE( ! (instance1 == instance2));
}

// End Test Suite
