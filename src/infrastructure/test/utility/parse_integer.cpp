// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure/utility/string.hpp>

#include <cstdint>
#include <limits>

using namespace kth;

TEST_CASE("decimal integers parse completely without streams", "[parse_integer]") {
    REQUIRE(*parse_integer<uint32_t>("0") == 0);
    REQUIRE(*parse_integer<uint32_t>("+42") == 42);
    REQUIRE(*parse_integer<uint32_t>(" \t42\r\n\f\v") == 42);
    REQUIRE(*parse_integer<uint32_t>("00042") == 42);
    REQUIRE(*parse_integer<uint32_t>("4294967295") == std::numeric_limits<uint32_t>::max());
    REQUIRE(*parse_integer<uint64_t>("18446744073709551615") == std::numeric_limits<uint64_t>::max());
    REQUIRE(*parse_integer<int32_t>("-2147483648") == std::numeric_limits<int32_t>::min());
    REQUIRE(*parse_integer<int32_t>("2147483647") == std::numeric_limits<int32_t>::max());
}

TEST_CASE("invalid and overflowing integers return errors", "[parse_integer]") {
    for (auto text : {"", " \t", "+", "-", "+-1", "++1", "-1", "1x", "1 2", "0x10", "1.0", "4294967296"}) {
        auto const result = parse_integer<uint32_t>(text);
        INFO(text);
        REQUIRE_FALSE(result);
        REQUIRE(result.error() == error::illegal_value);
    }
    REQUIRE_FALSE(parse_integer<uint64_t>("18446744073709551616"));
    REQUIRE_FALSE(parse_integer<int32_t>("-2147483649"));
    REQUIRE_FALSE(parse_integer<uint32_t>(" 42", false));
    REQUIRE_FALSE(parse_integer<uint32_t>("42 ", false));
    REQUIRE_FALSE(parse_integer<uint32_t>(std::string_view{"42\0x", 4}));
}
