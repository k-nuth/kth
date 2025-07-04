// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: send headers tests

TEST_CASE("send headers - roundtrip to data factory from data chunk", "[send headers]") {
    const message::send_headers expected{};
    auto const data = expected.to_data(message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::send_headers::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(0u == data.size());
    REQUIRE(result.is_valid());
    REQUIRE(0u == result.serialized_size(message::version::level::maximum));
}





// End Test Suite
