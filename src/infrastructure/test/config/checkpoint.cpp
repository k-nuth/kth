// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sstream>
#include <string>

#include <boost/lexical_cast.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <test_helpers.hpp>

#include <kth/infrastructure.hpp>
#include <kth/infrastructure/utility/collection.hpp>

using namespace kth;
using namespace kth::infrastructure::config;
#if ! defined(__EMSCRIPTEN__)
using namespace boost::program_options;
#endif

// Start Test Suite: checkpoint tests

constexpr char checkpoint_hash_a[] = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
constexpr char checkpoint_a[] = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f:0";
constexpr char checkpoint_b[] = "0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d:11111";
constexpr char checkpoint_c[] = "000000002dd5588a74784eaa7ab0507a18ad16a236e7b1ce69f00d7ddfb5d0a6:33333";
constexpr char checkpoint_abc[] = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f:0\n0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d:11111\n000000002dd5588a74784eaa7ab0507a18ad16a236e7b1ce69f00d7ddfb5d0a6:33333";

// ------------------------------------------------------------------------- //

// Start Test Suite: checkpoint construct

TEST_CASE("checkpoint construct  default  null hash", "[checkpoint construct]") {
    checkpoint const check;
    REQUIRE(check.hash() == null_hash);
    REQUIRE(check.height() == 0u);
}

TEST_CASE("checkpoint construct  copy  expected", "[checkpoint construct]") {
    checkpoint const check1(checkpoint_c);
    checkpoint const check2(check1);
    REQUIRE(check2.height() == check1.height());
    REQUIRE(encode_hash(check2.hash()) == encode_hash(check1.hash()));
}

TEST_CASE("checkpoint construct  string  expected", "[checkpoint construct]") {
    checkpoint const genesis(checkpoint_b);
    REQUIRE(genesis.height() == 11111u);
    REQUIRE(genesis.to_string() == checkpoint_b);
}

TEST_CASE("checkpoint construct  digest  expected", "[checkpoint construct]") {
    size_t const expected_height = 42;
    auto const expected_hash = checkpoint_hash_a;
    hash_digest digest;
    kth::decode_hash(digest, expected_hash);
    checkpoint const genesis(digest, expected_height);
    REQUIRE(genesis.height() == expected_height);
    REQUIRE(encode_hash(genesis.hash()) == expected_hash);
}


//TODO(fernando): fix these tests
// TEST_CASE("checkpoint construct  invalid height value  throws invalid option value", "[checkpoint construct]") {
//     // 2^64
//     // REQUIRE_THROWS_AS([](){checkpoint(checkpoint_hash_a ":18446744073709551616");}, invalid_option_value);

//     auto cp = checkpoint(checkpoint_hash_a ":18446744073709551616");
//     cp.height();
// }

// TEST_CASE("checkpoint construct  invalid height characters  throws invalid option value", "[checkpoint construct]") {
//     // 21 characters
//     REQUIRE_THROWS_AS([](){checkpoint(checkpoint_hash_a ":1000000000100000000001");}, invalid_option_value);
// }

// TEST_CASE("checkpoint construct  bogus height characters  throws invalid option value", "[checkpoint construct]") {
//     REQUIRE_THROWS_AS([](){checkpoint(checkpoint_hash_a ":xxx");}, invalid_option_value);
// }

// TEST_CASE("checkpoint construct  bogus line hash  throws invalid option value", "[checkpoint construct]") {
//     REQUIRE_THROWS_AS([](){checkpoint("bogus:42");}, invalid_option_value);
// }

// TEST_CASE("checkpoint construct  bogus hash  throws invalid option value", "[checkpoint construct]") {
//     REQUIRE_THROWS_AS([](){checkpoint("bogus", 42);}, invalid_option_value);
// }

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: checkpoint istream

TEST_CASE("checkpoint istream  empty  expected", "[checkpoint istream]") {
    checkpoint deserialized;
    std::stringstream serialized(checkpoint_a);
    serialized >> deserialized;
    REQUIRE(deserialized.to_string() == checkpoint_a);
}

// End Test Suite

// ------------------------------------------------------------------------- //

// Start Test Suite: checkpoint  ostream

static
checkpoint::list const test_checkpoints_list({
    checkpoint{checkpoint_a},
    checkpoint{checkpoint_b},
    checkpoint{checkpoint_c}
});

TEST_CASE("checkpoint  ostream  empty  expected", "[checkpoint  ostream]") {
    std::stringstream serialized;
    serialized << checkpoint::list();
    REQUIRE(serialized.str() == "");
}

TEST_CASE("checkpoint  ostream  populated  expected", "[checkpoint  ostream]") {
    std::stringstream serialized;
    serialized << test_checkpoints_list;
    REQUIRE(serialized.str() == checkpoint_abc);
}

TEST_CASE("checkpoint  ostream  boost lexical cast  expected", "[checkpoint  ostream]") {
    auto const serialized = boost::lexical_cast<std::string>(test_checkpoints_list);
    REQUIRE(serialized == checkpoint_abc);
}

// End Test Suite

// End Test Suite
