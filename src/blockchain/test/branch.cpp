// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <memory>
#include <kth/blockchain.hpp>

using namespace kth;
using namespace kd::message;
using namespace kth::blockchain;

// Start Test Suite: branch tests

#define DECLARE_BLOCK(name, number) \
    auto const name##number = std::make_shared<block>(); \
    name##number->header().set_bits(number);

// Access to protected members.
class branch_fixture
  : public branch
{
public:
    size_t index_of(size_t height) const
    {
        return branch::index_of(height);
    }

    size_t height_at(size_t index) const
    {
        return branch::height_at(index);
    }
};

// hash

TEST_CASE("branch hash default null hash", "[branch tests]") {
    branch instance;
    REQUIRE(instance.hash() == null_hash);
}

TEST_CASE("branch hash one block only previous block hash", "[branch tests]") {
    DECLARE_BLOCK(block, 0);
    DECLARE_BLOCK(block, 1);

    auto const expected = block0->hash();
    block1->header().set_previous_block_hash(expected);

    branch instance;
    REQUIRE(instance.push_front(block1));
    REQUIRE(instance.hash() == expected);
}

TEST_CASE("branch hash two blocks first previous block hash", "[branch tests]") {
    branch instance;
    DECLARE_BLOCK(top, 42);
    DECLARE_BLOCK(block, 0);
    DECLARE_BLOCK(block, 1);

    // Link the blocks.
    auto const expected = top42->hash();
    block0->header().set_previous_block_hash(expected);
    block1->header().set_previous_block_hash(block0->hash());

    REQUIRE(instance.push_front(block1));
    REQUIRE(instance.push_front(block0));
    REQUIRE(instance.hash() == expected);
}

// height/set_height

TEST_CASE("branch height default zero", "[branch tests]") {
    branch instance;
    REQUIRE(instance.height() == 0);
}

TEST_CASE("branch set height round trip unchanged", "[branch tests]") {
    static size_t const expected = 42;
    branch instance;
    instance.set_height(expected);
    REQUIRE(instance.height() == expected);
}

// index_of

TEST_CASE("branch index of one zero", "[branch tests]") {
    branch_fixture instance;
    instance.set_height(0);
    REQUIRE(instance.index_of(1) == 0u);
}

TEST_CASE("branch index of two one", "[branch tests]") {
    branch_fixture instance;
    instance.set_height(0);
    REQUIRE(instance.index_of(2) == 1u);
}

TEST_CASE("branch index of value expected", "[branch tests]") {
    branch_fixture instance;
    instance.set_height(42);
    REQUIRE(instance.index_of(53) == 10u);
}

// height_at

TEST_CASE("branch height at zero one", "[branch tests]") {
    branch_fixture instance;
    instance.set_height(0);
    REQUIRE(instance.height_at(0) == 1u);
}

TEST_CASE("branch height at one two", "[branch tests]") {
    branch_fixture instance;
    instance.set_height(0);
    REQUIRE(instance.height_at(1) == 2u);
}

TEST_CASE("branch height at value expected", "[branch tests]") {
    branch_fixture instance;
    instance.set_height(42);
    REQUIRE(instance.height_at(10) == 53u);
}

// size

TEST_CASE("branch size empty zero", "[branch tests]") {
    branch instance;
    REQUIRE(instance.size() == 0);
}

// empty

TEST_CASE("branch empty default true", "[branch tests]") {
    branch instance;
    REQUIRE(instance.empty());
}

TEST_CASE("branch empty push one false", "[branch tests]") {
    branch instance;
    DECLARE_BLOCK(block, 0);
    REQUIRE(instance.push_front(block0));
    REQUIRE( ! instance.empty());
}

// blocks

TEST_CASE("branch blocks default empty", "[branch tests]") {
    branch instance;
    REQUIRE(instance.blocks()->empty());
}

TEST_CASE("branch blocks one empty", "[branch tests]") {
    branch instance;
    DECLARE_BLOCK(block, 0);
    REQUIRE(instance.push_front(block0));
    REQUIRE( ! instance.empty());
    REQUIRE(instance.blocks()->size() == 1u);
}

// push_front

TEST_CASE("branch push front one success", "[branch tests]") {
    branch_fixture instance;
    DECLARE_BLOCK(block, 0);
    REQUIRE(instance.push_front(block0));
    REQUIRE( ! instance.empty());
    REQUIRE(instance.size() == 1u);
    REQUIRE((*instance.blocks())[0] == block0);
}

TEST_CASE("branch push front two linked success", "[branch tests]") {
    branch_fixture instance;
    DECLARE_BLOCK(block, 0);
    DECLARE_BLOCK(block, 1);

    // Link the blocks.
    block1->header().set_previous_block_hash(block0->hash());

    REQUIRE(instance.push_front(block1));
    REQUIRE(instance.push_front(block0));
    REQUIRE(instance.size() == 2u);
    REQUIRE((*instance.blocks())[0] == block0);
    REQUIRE((*instance.blocks())[1] == block1);
}

TEST_CASE("branch push front two unlinked link failure", "[branch tests]") {
    branch_fixture instance;
    DECLARE_BLOCK(block, 0);
    DECLARE_BLOCK(block, 1);

    // Ensure the blocks are not linked.
    block1->header().set_previous_block_hash(null_hash);

    REQUIRE(instance.push_front(block1));
    REQUIRE( ! instance.push_front(block0));
    REQUIRE(instance.size() == 1u);
    REQUIRE((*instance.blocks())[0] == block1);
}

// top

TEST_CASE("branch top default nullptr", "[branch tests]") {
    branch instance;
    REQUIRE( ! instance.top());
}

TEST_CASE("branch top two blocks expected", "[branch tests]") {
    branch_fixture instance;
    DECLARE_BLOCK(block, 0);
    DECLARE_BLOCK(block, 1);

    // Link the blocks.
    block1->header().set_previous_block_hash(block0->hash());

    REQUIRE(instance.push_front(block1));
    REQUIRE(instance.push_front(block0));
    REQUIRE(instance.size() == 2u);
    REQUIRE(instance.top() == block1);
}

// top_height

TEST_CASE("branch top height default 0", "[branch tests]") {
    branch instance;
    REQUIRE(instance.top_height() == 0u);
}

TEST_CASE("branch top height two blocks expected", "[branch tests]") {
    branch_fixture instance;
    DECLARE_BLOCK(block, 0);
    DECLARE_BLOCK(block, 1);

    static size_t const expected = 42;
    instance.set_height(expected - 2);

    // Link the blocks.
    block1->header().set_previous_block_hash(block0->hash());

    REQUIRE(instance.push_front(block1));
    REQUIRE(instance.push_front(block0));
    REQUIRE(instance.size() == 2u);
    REQUIRE(instance.top_height() == expected);
}

// work

TEST_CASE("branch work default zero", "[branch tests]") {
    branch instance;
    REQUIRE(instance.work() == 0);
}

TEST_CASE("branch work two blocks expected", "[branch tests]") {
    branch instance;
    DECLARE_BLOCK(block, 0);
    DECLARE_BLOCK(block, 1);

    // Link the blocks.
    block1->header().set_previous_block_hash(block0->hash());

    REQUIRE(instance.push_front(block1));
    REQUIRE(instance.push_front(block0));
    REQUIRE(instance.size() == 2u);

    ///////////////////////////////////////////////////////////////////////////
    // TODO: devise value tests.
    ///////////////////////////////////////////////////////////////////////////
    REQUIRE(instance.work() == 0);
}

// End Test Suite
