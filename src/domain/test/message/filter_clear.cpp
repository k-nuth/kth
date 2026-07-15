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
    auto const raw = kth::to_data_chunk(expected, version::level::maximum);
    filter_clear instance{};

    byte_reader reader(raw);
    auto result = filter_clear::from_data(reader, filter_clear::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("filter clear - roundtrip to data factory from data chunk", "[filter clear]") {
    static const filter_clear expected{};
    auto const data = kth::to_data_chunk(expected, version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = filter_clear::from_data(reader, version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(data.size() == 0u);
    REQUIRE(result.serialized_size(version::level::maximum) == 0u);
}



TEST_CASE("filter clear is a regular type", "[filter clear]") {
    // An empty-payload marker still has to behave like a value: declaring the
    // move constructor here would delete copy-assignment and suppress
    // move-assignment, leaving the type unassignable.
    static_assert(std::is_default_constructible_v<message::filter_clear>);
    static_assert(std::is_copy_constructible_v<message::filter_clear>);
    static_assert(std::is_move_constructible_v<message::filter_clear>);
    static_assert(std::is_copy_assignable_v<message::filter_clear>);
    static_assert(std::is_move_assignable_v<message::filter_clear>);

    message::filter_clear a;
    message::filter_clear b;
    a = b;
    b = std::move(a);
}

TEST_CASE("filter clear is usable in a constant expression", "[filter clear]") {
    static_assert(message::filter_clear::satoshi_fixed_size(message::version::level::maximum) == 0);
    static_assert(message::filter_clear{}.serialized_size(message::version::level::maximum) == 0);
    REQUIRE(true);
}

// End Test Suite
