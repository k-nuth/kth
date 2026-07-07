// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure.hpp>
#include <test_helpers.hpp>

using namespace kth;

// Start Test Suite: duration jitter tests

TEST_CASE("jitter duration zero duration maximum", "[duration jitter tests]") {
    int const max_seconds = 0;
    kth::asio::seconds const maximum(max_seconds);
    auto const result = jitter_duration(maximum, 1);
    REQUIRE(result == maximum);
}

TEST_CASE("jitter duration subminute ratio 2 expected", "[duration jitter tests]") {
    int const max_seconds = 42;
    kth::asio::seconds const maximum(max_seconds);
    kth::asio::seconds const minimum(max_seconds - max_seconds / 2);
    auto const result = jitter_duration(maximum, 2);
    REQUIRE(result <= maximum);
    REQUIRE(result >= minimum);
}

TEST_CASE("jitter duration subminute ratio 0 maximum", "[duration jitter tests]") {
    int const max_seconds = 42;
    kth::asio::seconds const maximum(max_seconds);
    auto const result = jitter_duration(maximum, 0);
    REQUIRE(result == maximum);
}

TEST_CASE("jitter duration subminute ratio 1 expected", "[duration jitter tests]") {
    uint8_t const ratio = 1;
    int const max_seconds = 42;
    kth::asio::seconds const maximum(max_seconds);
    kth::asio::seconds const minimum(max_seconds - max_seconds / ratio);
    auto const result = jitter_duration(maximum, ratio);
    REQUIRE(result <= maximum);
    REQUIRE(result >= minimum);
}

// Use same (ms) resolution as function to prevent test rounding difference.
TEST_CASE("jitter duration superminute ratio 255 expected", "[duration jitter tests]") {
    uint8_t const ratio = 255;
    int const max_seconds = 420;
    kth::asio::milliseconds const maximum(max_seconds);
    kth::asio::milliseconds const minimum(max_seconds - max_seconds / ratio);
    auto const result = jitter_duration(maximum, ratio);
    REQUIRE(result <= maximum);
    REQUIRE(result >= minimum);
}

// End Test Suite
