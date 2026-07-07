// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/infrastructure/message/message_tools.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: messages tests

TEST_CASE("messages variable uint size one byte expected", "[messages]") {
    static uint64_t const value = 1;
    REQUIRE(infrastructure::message::variable_uint_size(value) == 1u);
}

TEST_CASE("messages variable uint size two byte expected", "[messages]") {
    static uint64_t const value = 0xfe;
    REQUIRE(infrastructure::message::variable_uint_size(value) == 3u);
}

TEST_CASE("messages variable uint size four byte expected", "[messages]") {
    static uint64_t const value = 0x10000;
    REQUIRE(infrastructure::message::variable_uint_size(value) == 5u);
}

TEST_CASE("messages variable uint size eight byte expected", "[messages]") {
    static uint64_t const value = 0x100000000;
    REQUIRE(infrastructure::message::variable_uint_size(value) == 9u);
}

// End Test Suite
