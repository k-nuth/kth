// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: memory pool tests

TEST_CASE("memory pool - from data insufficient version failure", "[memory pool]") {
    message::memory_pool const expected;
    data_chunk const data = expected.to_data(message::version::level::maximum);
    message::memory_pool instance{};
    byte_reader reader(data);
    auto result = message::memory_pool::from_data(reader, message::memory_pool::version_minimum - 1);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("memory pool - roundtrip to data factory from data chunk", "[memory pool]") {
    message::memory_pool const expected{};
    auto const data = expected.to_data(message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::memory_pool::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(0u == data.size());
    REQUIRE(result.is_valid());
    REQUIRE(0u == result.serialized_size(message::version::level::maximum));
}



// End Test Suite
