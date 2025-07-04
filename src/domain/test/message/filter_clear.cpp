// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

// Start Test Suite: filter clear tests

TEST_CASE("filter clear - from data insufficient version failure", "[filter clear]") {
    static const filter_clear expected{};
    auto const raw = expected.to_data(version::level::maximum);
    filter_clear instance{};

    byte_reader reader(raw);
    auto result = filter_clear::from_data(reader, filter_clear::version_minimum - 1);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("filter clear - roundtrip to data factory from data chunk", "[filter clear]") {
    static const filter_clear expected{};
    auto const data = expected.to_data(version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = filter_clear::from_data(reader, version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(data.size() == 0u);
    REQUIRE(result.is_valid());
    REQUIRE(result.serialized_size(version::level::maximum) == 0u);
}



// End Test Suite
