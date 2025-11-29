// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;

// Start Test Suite: point value tests

static auto const hash1 = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

TEST_CASE("point value  default constructor  always  zero value", "[point value]") {
    static point_value const instance;
    REQUIRE(instance.hash() == null_hash);
    REQUIRE(instance.index() == 0u);
    REQUIRE(instance.value() == 0u);
}

TEST_CASE("point value  move constructor  always  expected", "[point value]") {
    point_value other{{hash1, 42}, 34};
    point_value const instance(std::move(other));
    REQUIRE(instance.hash() == hash1);
    REQUIRE(instance.index() == 42u);
    REQUIRE(instance.value() == 34u);
}

TEST_CASE("point value  copy constructor  always  expected", "[point value]") {
    static point_value const other{{hash1, 42}, 34};
    point_value const instance(other);
    REQUIRE(instance.hash() == hash1);
    REQUIRE(instance.index() == 42u);
    REQUIRE(instance.value() == 34u);
}

TEST_CASE("point value  constructor4  always  expected", "[point value]") {
    point foo{hash1, 42};
    static point_value const instance(std::move(foo), 34);
    REQUIRE(instance.hash() == hash1);
    REQUIRE(instance.index() == 42u);
    REQUIRE(instance.value() == 34u);
}

TEST_CASE("point value  constructor5  always  expected", "[point value]") {
    static point const foo{hash1, 42};
    static point_value const instance(foo, 34);
    REQUIRE(instance.hash() == hash1);
    REQUIRE(instance.index() == 42u);
    REQUIRE(instance.value() == 34u);
}

TEST_CASE("point value  move assign  always  expected", "[point value]") {
    point_value other{{hash1, 42}, 34};
    auto const instance = std::move(other);
    REQUIRE(instance.hash() == hash1);
    REQUIRE(instance.index() == 42u);
    REQUIRE(instance.value() == 34u);
}

TEST_CASE("point value  copy assign  always  expected", "[point value]") {
    static point_value const other{{hash1, 42}, 34};
    auto const instance = other;
    REQUIRE(instance.hash() == hash1);
    REQUIRE(instance.index() == 42u);
    REQUIRE(instance.value() == 34u);
}

TEST_CASE("point value  swap  always  expected reversal", "[point value]") {
    point_value instance1{{hash1, 42}, 34};
    point_value instance2{{null_hash, 43}, 35};

    // Must be unqualified (no std namespace).
    swap(instance1, instance2);

    CHECK(instance2.hash() == hash1);
    CHECK(instance2.index() == 42u);
    CHECK(instance2.value() == 34u);

    CHECK(instance1.hash() == null_hash);
    CHECK(instance1.index() == 43u);
    CHECK(instance1.value() == 35u);
}

TEST_CASE("point value  equality  same  true", "[point value]") {
    static point_value const instance1{{hash1, 42}, 34};
    static point_value const instance2{{hash1, 42}, 34};
    REQUIRE(instance1 == instance2);
}

TEST_CASE("point value  equality  different by hash  false", "[point value]") {
    static point_value const instance1{{hash1, 42}, 34};
    static point_value const instance2{{null_hash, 43}, 34};
    REQUIRE( ! (instance1 == instance2));
}

TEST_CASE("point value  equality  different by index  false", "[point value]") {
    static point_value const instance1{{hash1, 42}, 34};
    static point_value const instance2{{hash1, 43}, 34};
    REQUIRE( ! (instance1 == instance2));
}

TEST_CASE("point value  equality  different by value  false", "[point value]") {
    static point_value const instance1{{hash1, 42}, 34};
    static point_value const instance2{{hash1, 42}, 35};
    REQUIRE( ! (instance1 == instance2));
}

TEST_CASE("point value  inequality  same  false", "[point value]") {
    static point_value const instance1{{hash1, 42}, 34};
    static point_value const instance2{{hash1, 42}, 34};
    REQUIRE( ! (instance1 != instance2));
}

TEST_CASE("point value  inequality  different by hash  true", "[point value]") {
    static point_value const instance1{{hash1, 42}, 34};
    static point_value const instance2{{null_hash, 43}, 34};
    REQUIRE(instance1 != instance2);
}

TEST_CASE("point value  inequality  different by index  true", "[point value]") {
    static point_value const instance1{{hash1, 42}, 34};
    static point_value const instance2{{hash1, 43}, 34};
    REQUIRE(instance1 != instance2);
}

TEST_CASE("point value  inequality  different by value  true", "[point value]") {
    static point_value const instance1{{hash1, 42}, 34};
    static point_value const instance2{{hash1, 42}, 35};
    REQUIRE(instance1 != instance2);
}

TEST_CASE("point value  set value  42  42", "[point value]") {
    static auto const expected = 42u;
    point_value instance;
    instance.set_value(expected);
    REQUIRE(instance.value() == expected);
}

TEST_CASE("point value  set value  zeroize  zero", "[point value]") {
    point_value instance;
    instance.set_value(42);
    instance.set_value(0);
    REQUIRE(instance.value() == 0u);
}

// End Test Suite
