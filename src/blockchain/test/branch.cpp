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

// Header is now an immutable value type: build the block with the header
// pre-set instead of mutating fields after construction.
static
std::shared_ptr<block> make_test_block(uint32_t bits,
                                       hash_digest const& prev = null_hash) {
    return std::make_shared<block>(block{
        kd::chain::header{0u, prev, null_hash, 0u, bits, 0u},
        kd::chain::transaction::list{}
    });
}

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
    auto const block0 = make_test_block(0u);
    auto const expected = block0->hash();
    auto const block1 = make_test_block(1u, expected);

    branch instance;
    REQUIRE(instance.push_front(block1));
    REQUIRE(instance.hash() == expected);
}

TEST_CASE("branch hash two blocks first previous block hash", "[branch tests]") {
    branch instance;
    auto const top42 = make_test_block(42u);
    auto const expected = top42->hash();
    auto const block0 = make_test_block(0u, expected);
    auto const block1 = make_test_block(1u, block0->hash());

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
    auto const block0 = make_test_block(0u);
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
    auto const block0 = make_test_block(0u);
    REQUIRE(instance.push_front(block0));
    REQUIRE( ! instance.empty());
    REQUIRE(instance.blocks()->size() == 1u);
}

// push_front

TEST_CASE("branch push front one success", "[branch tests]") {
    branch_fixture instance;
    auto const block0 = make_test_block(0u);
    REQUIRE(instance.push_front(block0));
    REQUIRE( ! instance.empty());
    REQUIRE(instance.size() == 1u);
    REQUIRE((*instance.blocks())[0] == block0);
}

TEST_CASE("branch push front two linked success", "[branch tests]") {
    branch_fixture instance;
    auto const block0 = make_test_block(0u);
    auto const block1 = make_test_block(1u, block0->hash());

    REQUIRE(instance.push_front(block1));
    REQUIRE(instance.push_front(block0));
    REQUIRE(instance.size() == 2u);
    REQUIRE((*instance.blocks())[0] == block0);
    REQUIRE((*instance.blocks())[1] == block1);
}

TEST_CASE("branch push front two unlinked link failure", "[branch tests]") {
    branch_fixture instance;
    auto const block0 = make_test_block(0u);
    // block1 has null prev_hash by default, so it cannot link to block0.
    auto const block1 = make_test_block(1u);

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
    auto const block0 = make_test_block(0u);
    auto const block1 = make_test_block(1u, block0->hash());

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
    auto const block0 = make_test_block(0u);
    auto const block1 = make_test_block(1u, block0->hash());

    static size_t const expected = 42;
    instance.set_height(expected - 2);

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
    auto const block0 = make_test_block(0u);
    auto const block1 = make_test_block(1u, block0->hash());

    REQUIRE(instance.push_front(block1));
    REQUIRE(instance.push_front(block0));
    REQUIRE(instance.size() == 2u);

    ///////////////////////////////////////////////////////////////////////////
    // TODO: devise value tests.
    ///////////////////////////////////////////////////////////////////////////
    REQUIRE(instance.work() == 0);
}

// End Test Suite
