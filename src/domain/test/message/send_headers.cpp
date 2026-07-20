// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: send headers tests

TEST_CASE("send headers - roundtrip to data factory from data chunk", "[send headers]") {
    const message::send_headers expected{};
    auto const data = kth::to_data_chunk(expected, message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::send_headers::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(0u == data.size());
    REQUIRE(0u == result.serialized_size(message::version::level::maximum));
}





TEST_CASE("send headers is a regular type", "[send headers]") {
    // An empty-payload marker still has to behave like a value: declaring the
    // move constructor here would delete copy-assignment and suppress
    // move-assignment, leaving the type unassignable.
    static_assert(std::is_default_constructible_v<message::send_headers>);
    static_assert(std::is_copy_constructible_v<message::send_headers>);
    static_assert(std::is_move_constructible_v<message::send_headers>);
    static_assert(std::is_copy_assignable_v<message::send_headers>);
    static_assert(std::is_move_assignable_v<message::send_headers>);

    message::send_headers a;
    message::send_headers b;
    a = b;
    b = std::move(a);
}

TEST_CASE("send headers is usable in a constant expression", "[send headers]") {
    static_assert(message::send_headers::satoshi_fixed_size(message::version::level::maximum) == 0);
    static_assert(message::send_headers{}.serialized_size(message::version::level::maximum) == 0);
    REQUIRE(true);
}

// End Test Suite
