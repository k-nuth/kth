// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/domain/config/point.hpp>
#include <kth/domain/config/input.hpp>
#include <kth/domain/config/output.hpp>
#include <kth/infrastructure/error.hpp>

#include <string>

TEST_CASE("text config rejects invalid unsigned integers without throwing", "[config_integer]") {
    std::string const hash(64, '0');
    for (auto const* text : {"x", "-1", "4294967296", "1x", "+", "1.5"}) {
        INFO(text);
        auto point = kth::domain::config::point::parse_from(hash + ":" + text);
        REQUIRE_FALSE(point);
        REQUIRE(point.error() == kth::error::illegal_value);
        auto input = kth::domain::config::input::parse_from(hash + ":0:" + text);
        REQUIRE_FALSE(input);
        REQUIRE(input.error() == kth::error::illegal_value);
    }
    for (auto const* text : {"x", "-1", "18446744073709551616", "1x", "+", "1.5"}) {
        INFO(text);
        auto output = kth::domain::config::output::parse_from(std::string{"51:"} + text);
        REQUIRE_FALSE(output);
        REQUIRE(output.error() == kth::error::illegal_value);
    }
}

TEST_CASE("text config accepts unsigned decimal boundaries", "[config_integer]") {
    std::string const hash(64, '0');
    auto point = kth::domain::config::point::parse_from(hash + ":4294967295");
    REQUIRE(point);
    REQUIRE(point->value().index() == 0xffffffffu);
    auto input = kth::domain::config::input::parse_from(hash + ":+1:4294967295");
    REQUIRE(input);
    REQUIRE(input->value().sequence() == 0xffffffffu);
    REQUIRE(kth::domain::config::output::parse_from("51:0"));
    REQUIRE(kth::domain::config::output::parse_from("51:+42"));
}
