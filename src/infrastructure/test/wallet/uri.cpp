// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure::wallet;

// Start Test Suite: uri tests

// TEST_CASE("uri parse http roundtrip test", "[uri tests]") {
//     auto const test = "http://github.com/k-nuth?good=true#nice";
//     uri parsed;
//     REQUIRE(parsed.decode(test));

//     REQUIRE(parsed.has_authority());
//     REQUIRE(parsed.has_query());
//     REQUIRE(parsed.has_fragment());

//     REQUIRE(parsed.scheme() == "http");
//     REQUIRE(parsed.authority() == "github.com");
//     REQUIRE(parsed.path() == "/kth");
//     REQUIRE(parsed.query() == "good=true");
//     REQUIRE(parsed.fragment() == "nice");

//     REQUIRE(parsed.encoded() == test);
// }

TEST_CASE("uri parse messy roundtrip test", "[uri tests]") {
    auto const test = "TEST:%78?%79#%7a";
    uri parsed;
    REQUIRE(parsed.decode(test));

    REQUIRE( ! parsed.has_authority());
    REQUIRE(parsed.has_query());
    REQUIRE(parsed.has_fragment());

    REQUIRE(parsed.scheme() == "test");
    REQUIRE(parsed.path() == "x");
    REQUIRE(parsed.query() == "y");
    REQUIRE(parsed.fragment() == "z");

    REQUIRE(parsed.encoded() == test);
}

TEST_CASE("uri parse scheme test", "[uri tests]") {
    uri parsed;
    REQUIRE( ! parsed.decode(""));
    REQUIRE( ! parsed.decode(":"));
    REQUIRE( ! parsed.decode("1:"));
    REQUIRE( ! parsed.decode("%78:"));

    REQUIRE(parsed.decode("x:"));
    REQUIRE(parsed.scheme() == "x");

    REQUIRE(parsed.decode("x::"));
    REQUIRE(parsed.scheme() == "x");
    REQUIRE(parsed.path() == ":");
}

TEST_CASE("uri parsing non strict test", "[uri tests]") {
    uri parsed;
    REQUIRE( ! parsed.decode("test:?テスト"));

    REQUIRE(parsed.decode("test:テスト", false));
    REQUIRE(parsed.scheme() == "test");
    REQUIRE(parsed.path() == "テスト");
}

TEST_CASE("uri parse authority test", "[uri tests]") {
    uri parsed;
    REQUIRE(parsed.decode("test:/"));
    REQUIRE( ! parsed.has_authority());
    REQUIRE(parsed.path() == "/");

    REQUIRE(parsed.decode("test://"));
    REQUIRE(parsed.has_authority());
    REQUIRE(parsed.authority() == "");
    REQUIRE(parsed.path() == "");

    REQUIRE(parsed.decode("test:///"));
    REQUIRE(parsed.has_authority());
    REQUIRE(parsed.authority() == "");
    REQUIRE(parsed.path() == "/");

    REQUIRE(parsed.decode("test:/x//"));
    REQUIRE( ! parsed.has_authority());
    REQUIRE(parsed.path() == "/x//");

    REQUIRE(parsed.decode("ssh://git@github.com:22/path/"));
    REQUIRE(parsed.has_authority());
    REQUIRE(parsed.authority() == "git@github.com:22");
    REQUIRE(parsed.path() == "/path/");
}

TEST_CASE("uri parse query test", "[uri tests]") {
    uri parsed;
    REQUIRE(parsed.decode("test:#?"));
    REQUIRE( ! parsed.has_query());

    REQUIRE(parsed.decode("test:?&&x=y&z"));
    REQUIRE(parsed.has_query());
    REQUIRE(parsed.query() == "&&x=y&z");

    auto map = parsed.decode_query();
    REQUIRE(map.end() != map.find(""));
    REQUIRE(map.end() != map.find("x"));
    REQUIRE(map.end() != map.find("z"));
    REQUIRE(map.end() == map.find("y"));

    REQUIRE(map[""] == "");
    REQUIRE(map["x"] == "y");
    REQUIRE(map["z"] == "");
}

TEST_CASE("uri parse fragment test", "[uri tests]") {
    uri parsed;
    REQUIRE(parsed.decode("test:?"));
    REQUIRE( ! parsed.has_fragment());

    REQUIRE(parsed.decode("test:#"));
    REQUIRE(parsed.has_fragment());
    REQUIRE(parsed.fragment() == "");

    REQUIRE(parsed.decode("test:#?"));
    REQUIRE(parsed.has_fragment());
    REQUIRE(parsed.fragment() == "?");
}

TEST_CASE("uri encode positive test", "[uri tests]") {
    uri out;
    out.set_scheme("test");
    out.set_authority("user@hostname");
    out.set_path("/some/path/?/#");
    out.set_query("tacos=yummy");
    out.set_fragment("good evening");
    REQUIRE(out.encoded() == "test://user@hostname/some/path/%3F/%23?tacos=yummy#good%20evening");

    out.remove_authority();
    out.remove_query();
    out.remove_fragment();
    REQUIRE(out.encoded() == "test:/some/path/%3F/%23");
}

// End Test Suite
