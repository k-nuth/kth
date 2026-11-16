// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <test_helpers.hpp>
#include <kth/infrastructure/utility/string.hpp>

using namespace kth;

TEST_CASE("split preserves compressed separators and empty edge fields", "[string_algorithms]") {
    REQUIRE(split("") == string_list{""});
    REQUIRE(split("  \t\r\n") == string_list{""});
    REQUIRE(split("a,,,b", ",", false) == string_list{"a", "b"});
    REQUIRE(split(",,,a,,,", ",", false) == string_list{"", "a", ""});
    REQUIRE(split(",,,", ",", false) == string_list{"", ""});
    REQUIRE(split("a b", "", false) == string_list{"a b"});
    REQUIRE(split(" a ", " ", false) == string_list{"", "a", ""});
    REQUIRE(split(" a ", " ", true) == string_list{"a"});
    REQUIRE(split(" a,\tb ", ",\t", true) == string_list{"a", "b"});
    REQUIRE(split(std::string_view{"a\0b", 3}, std::string_view{"\0", 1}, false) == string_list{"a", "b"});
}

TEST_CASE("trim and join preserve byte contents", "[string_algorithms]") {
    REQUIRE(trim_view(" \ttext\r\n\f\v") == "text");
    REQUIRE(trim_view(" \t").empty());
    REQUIRE(trim_view("á 日本語") == "á 日本語");
    REQUIRE(join({}) == "");
    REQUIRE(join({"", "a", ""}, ":") == ":a:");
    REQUIRE(join({"a", "b"}, "") == "ab");
    REQUIRE(join({"a", "b"}, "::") == "a::b");
}
