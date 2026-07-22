// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/node/rpc/json.hpp>

using namespace kth::node::rpc;

// Start Test Suite: rpc json tests

TEST_CASE("writer scalar number", "[rpc json]") {
    writer w;
    w.value(static_cast<std::uint64_t>(42));
    REQUIRE(w.str() == "42");
}

TEST_CASE("writer object with mixed fields", "[rpc json]") {
    writer w;
    w.begin_object();
    w.field("name", "kth");
    w.field("height", static_cast<std::int64_t>(7));
    w.field("ok", true);
    w.end_object();
    REQUIRE(w.str() == R"({"name":"kth","height":7,"ok":true})");
}

TEST_CASE("writer nested array of objects", "[rpc json]") {
    writer w;
    w.begin_object();
    w.key("items").begin_array();
    w.begin_object().field("id", static_cast<std::int64_t>(1)).end_object();
    w.begin_object().field("id", static_cast<std::int64_t>(2)).end_object();
    w.end_array();
    w.end_object();
    REQUIRE(w.str() == R"({"items":[{"id":1},{"id":2}]})");
}

TEST_CASE("writer escapes strings", "[rpc json]") {
    writer w;
    w.value(std::string_view("a\"b\\c"));
    REQUIRE(w.str() == R"("a\"b\\c")");
}

TEST_CASE("parse_request extracts method params id", "[rpc json]") {
    auto req = parse_request(R"({"jsonrpc":"2.0","id":7,"method":"getblockcount","params":[1,2]})");
    REQUIRE(req.has_value());
    REQUIRE(req->method == "getblockcount");
    REQUIRE(req->params == "[1,2]");
    REQUIRE(req->id == "7");
}

TEST_CASE("parse_request defaults id to null", "[rpc json]") {
    auto req = parse_request(R"({"method":"getblockcount"})");
    REQUIRE(req.has_value());
    REQUIRE(req->id == "null");
    REQUIRE(req->params.empty());
}

TEST_CASE("parse_request echoes string id verbatim", "[rpc json]") {
    auto req = parse_request(R"({"method":"m","id":"abc"})");
    REQUIRE(req.has_value());
    REQUIRE(req->id == "\"abc\"");
}

TEST_CASE("parse_request rejects malformed json", "[rpc json]") {
    REQUIRE_FALSE(parse_request("{bad").has_value());
}

TEST_CASE("parse_request rejects missing method", "[rpc json]") {
    REQUIRE_FALSE(parse_request(R"({"id":1})").has_value());
}

TEST_CASE("build_success wraps result", "[rpc json]") {
    REQUIRE(build_success("1", "0") == R"({"result":0,"error":null,"id":1})");
}

TEST_CASE("build_error wraps code and message", "[rpc json]") {
    REQUIRE(build_error("null", -32601, "Method not found") ==
        R"({"result":null,"error":{"code":-32601,"message":"Method not found"},"id":null})");
}

TEST_CASE("params_strings reads a string array", "[rpc json]") {
    auto const p = params_strings(R"(["aa","bb"])");
    REQUIRE(p.size() == 2u);
    REQUIRE(p[0] == "aa");
    REQUIRE(p[1] == "bb");
}

TEST_CASE("params_strings yields empty slots for non-strings", "[rpc json]") {
    auto const p = params_strings(R"([1,"x"])");
    REQUIRE(p.size() == 2u);
    REQUIRE(p[0].empty());
    REQUIRE(p[1] == "x");
}

TEST_CASE("params_strings tolerates empty and non-array params", "[rpc json]") {
    REQUIRE(params_strings("").empty());
    REQUIRE(params_strings("[]").empty());
    REQUIRE(params_strings("{}").empty());
}
