// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure::config;

// Start Test Suite: hash256 tests

// Start Test Suite: hash256  construct

TEST_CASE("hash256  construct  default  null hash", "[hash256  construct]") {
    hash256 const uninitialized_hash;
    auto const expectation = encode_hash(kth::null_hash);
    auto const result = encode_hash(uninitialized_hash);
    REQUIRE(expectation == result);
}

// End Test Suite

// End Test Suite
