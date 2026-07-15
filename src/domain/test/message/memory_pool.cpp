// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: memory pool tests

TEST_CASE("memory pool - from data insufficient version failure", "[memory pool]") {
    message::memory_pool const expected;
    data_chunk const data = kth::to_data_chunk(expected, message::version::level::maximum);
    message::memory_pool instance{};
    byte_reader reader(data);
    auto result = message::memory_pool::from_data(reader, message::memory_pool::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("memory pool - roundtrip to data factory from data chunk", "[memory pool]") {
    message::memory_pool const expected{};
    auto const data = kth::to_data_chunk(expected, message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::memory_pool::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(0u == data.size());
    REQUIRE(0u == result.serialized_size(message::version::level::maximum));
}



TEST_CASE("memory pool is a regular type", "[memory pool]") {
    // An empty-payload marker still has to behave like a value: declaring the
    // move constructor here would delete copy-assignment and suppress
    // move-assignment, leaving the type unassignable.
    static_assert(std::is_default_constructible_v<message::memory_pool>);
    static_assert(std::is_copy_constructible_v<message::memory_pool>);
    static_assert(std::is_move_constructible_v<message::memory_pool>);
    static_assert(std::is_copy_assignable_v<message::memory_pool>);
    static_assert(std::is_move_assignable_v<message::memory_pool>);

    message::memory_pool a;
    message::memory_pool b;
    a = b;
    b = std::move(a);
}

TEST_CASE("memory pool is usable in a constant expression", "[memory pool]") {
    static_assert(message::memory_pool::satoshi_fixed_size(message::version::level::maximum) == 0);
    static_assert(message::memory_pool{}.serialized_size(message::version::level::maximum) == 0);
    REQUIRE(true);
}

// End Test Suite
