// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cmath>

#include <kth/infrastructure.hpp>

using namespace kth;

TEST_CASE("arithmetic right shift checks", "[operators tests]") {
    for (int a = 0; a <= std::numeric_limits<int8_t>::max(); ++a) {
        REQUIRE(sar(a, 1) == (a / 2));
    }

    for (int a = 0; a <= std::numeric_limits<int8_t>::max(); ++a) {
        REQUIRE(sar(a, 2) == (a / 4));
    }

    for (int a = std::numeric_limits<int8_t>::min(); a < 0; ++a) {
        REQUIRE(sar(a, 1) == int(std::floor(a / 2.0)));
    }

    for (int a = std::numeric_limits<int8_t>::min(); a < 0; ++a) {
        REQUIRE(sar(a, 2) == int(std::floor(a / 4.0)));
    }
}
