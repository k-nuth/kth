// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/node.hpp>

using namespace kth;
using namespace kth::node;

// Start Test Suite: performance tests

// normal
//-----------------------------------------------------------------------------

TEST_CASE("performance normal 21 over 42 0 point 5", "[performance tests]") {
    performance instance;
    instance.idle = true;
    instance.events = 21;
    instance.window = 84;
    instance.database = 42;
    REQUIRE(instance.normal() == 0.5);
}

TEST_CASE("performance normal 1 over negative 1 negative 1 point 0", "[performance tests]") {
    performance instance;
    instance.events = 1;
    instance.window = 0;
    instance.database = 1;
    REQUIRE(instance.normal() == -1.0);
}

TEST_CASE("performance normal 0 over 0 0 point 0", "[performance tests]") {
    performance instance;
    instance.events = 0;
    instance.window = 1;
    instance.database = 1;
    REQUIRE(instance.normal() == 0.0);
}

TEST_CASE("performance normal 0 over 1 0 point 0", "[performance tests]") {
    performance instance;
    instance.events = 0;
    instance.window = 1;
    instance.database = 0;
    REQUIRE(instance.normal() == 0.0);
}

TEST_CASE("performance normal 1 over 0 0 point 0", "[performance tests]") {
    performance instance;
    instance.events = 1;
    instance.window = 2;
    instance.database = 2;
    REQUIRE(instance.normal() == 0.0);
}

TEST_CASE("performance normal 1 over 1 1 point 0", "[performance tests]") {
    performance instance;
    instance.events = 1;
    instance.window = 2;
    instance.database = 1;
    REQUIRE(instance.normal() == 1.0);
}

TEST_CASE("performance normal 2 over 1 2 point 0", "[performance tests]") {
    performance instance;
    instance.events = 2;
    instance.window = 2;
    instance.database = 1;
    REQUIRE(instance.normal() == 2.0);
}

TEST_CASE("performance normal 1 over 2 0 point 5", "[performance tests]") {
    performance instance;
    instance.events = 1;
    instance.window = 4;
    instance.database = 2;
    REQUIRE(instance.normal() == 0.5);
}

// ratio
//-----------------------------------------------------------------------------

TEST_CASE("performance ratio 21 over 42 0 point 5", "[performance tests]") {
    performance instance;
    instance.idle = true;
    instance.events = 1;
    instance.database = 21;
    instance.window = 42;
    REQUIRE(instance.ratio() == 0.5);
}

TEST_CASE("performance ratio 0 over 0 0 point 0", "[performance tests]") {
    performance instance;
    instance.database = 0;
    instance.window = 0;
    REQUIRE(instance.ratio() == 0.0);
}

TEST_CASE("performance ratio 0 over 1 0 point 0", "[performance tests]") {
    performance instance;
    instance.database = 0;
    instance.window = 1;
    REQUIRE(instance.ratio() == 0.0);
}

TEST_CASE("performance ratio 1 over 0 0 point 0", "[performance tests]") {
    performance instance;
    instance.database = 1;
    instance.window = 0;
    REQUIRE(instance.ratio() == 0.0);
}

TEST_CASE("performance ratio 1 over 1 1 point 0", "[performance tests]") {
    performance instance;
    instance.database = 1;
    instance.window = 1;
    REQUIRE(instance.ratio() == 1.0);
}

TEST_CASE("performance ratio 2 over 1 2 point 0", "[performance tests]") {
    performance instance;
    instance.database = 2;
    instance.window = 1;
    REQUIRE(instance.ratio() == 2.0);
}

TEST_CASE("performance ratio 1 over 2 0 point 5", "[performance tests]") {
    performance instance;
    instance.database = 1;
    instance.window = 2;
    REQUIRE(instance.ratio() == 0.5);
}

// total
//-----------------------------------------------------------------------------

TEST_CASE("performance total 21 over 42 0 point 5", "[performance tests]") {
    performance instance;
    instance.idle = true;
    instance.database = 1;
    instance.events = 21;
    instance.window = 42;
    REQUIRE(instance.total() == 0.5);
}

TEST_CASE("performance total 0 over 0 0 point 0", "[performance tests]") {
    performance instance;
    instance.events = 0;
    instance.window = 0;
    REQUIRE(instance.total() == 0.0);
}

TEST_CASE("performance total 0 over 1 0 point 0", "[performance tests]") {
    performance instance;
    instance.events = 0;
    instance.window = 1;
    REQUIRE(instance.total() == 0.0);
}

TEST_CASE("performance total 1 over 0 0 point 0", "[performance tests]") {
    performance instance;
    instance.events = 1;
    instance.window = 0;
    REQUIRE(instance.total() == 0.0);
}

TEST_CASE("performance total 1 over 1 1 point 0", "[performance tests]") {
    performance instance;
    instance.events = 1;
    instance.window = 1;
    REQUIRE(instance.total() == 1.0);
}

TEST_CASE("performance total 2 over 1 2 point 0", "[performance tests]") {
    performance instance;
    instance.events = 2;
    instance.window = 1;
    REQUIRE(instance.total() == 2.0);
}

TEST_CASE("performance total 1 over 2 0 point 5", "[performance tests]") {
    performance instance;
    instance.events = 1;
    instance.window = 2;
    REQUIRE(instance.total() == 0.5);
}

// End Test Suite
