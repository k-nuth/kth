// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <string>
#include <type_traits>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/dispatch.hpp>
#include "../../blockchain/test/reorg_chain_fixture.hpp"

using namespace kth;

static_assert(!std::is_copy_constructible_v<node::rpc::dispatcher>);
static_assert(!std::is_move_constructible_v<node::rpc::dispatcher>);

TEST_CASE("getblockheader dispatch honors verbose and rejects invalid types", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_header_verbose");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const hash = encode_hash(genesis.hash());
    auto request = [&](std::string const& suffix) {
        auto const body = std::string(R"({"method":"getblockheader","params":[")")
            + hash + '"' + suffix + R"(],"id":7})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };

    auto const raw = request(",false");
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(raw);
    simdjson::ondemand::document document;
    REQUIRE(parser.iterate(padded).get(document) == simdjson::SUCCESS);
    std::string_view hex;
    REQUIRE(document["result"].get_string().get(hex) == simdjson::SUCCESS);
    REQUIRE(hex.size() == 160);
    auto const bytes = decode_base16(hex);
    REQUIRE(bytes);
    byte_reader reader(*bytes);
    auto const decoded = domain::chain::header::from_data(reader);
    REQUIRE(decoded);
    REQUIRE(*decoded == genesis.header());
    REQUIRE(raw == node::rpc::build_success("7", '"' + std::string(hex) + '"'));
    auto const verbose = request(",true");
    REQUIRE(verbose.find(R"("result":{)") != std::string::npos);
    REQUIRE(verbose.find(R"("hash":")" + hash + '"') != std::string::npos);
    REQUIRE(request("") == verbose);
    REQUIRE(request(",null") == verbose);
    for (auto const* invalid : {",0", ",1", ",\"false\"", ",{}", ",[]"}) {
        CAPTURE(invalid);
        REQUIRE(request(invalid) == node::rpc::build_error("7", -32602, "verbose must be a boolean"));
    }
}

TEST_CASE("validateaddress dispatch checks addresses against the node's network", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_validate_address");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);

    auto request = [&](std::string const& params) {
        auto const body = R"({"method":"validateaddress","params":)" + params + R"(,"id":3})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };

    // The fixture chain is regtest, so bchreg addresses are this network's.
    REQUIRE(request(R"(["bchreg:qpzz8n7jp6847yyx8t33matrgcsdx6c0cv99t2zdvc"])") == node::rpc::build_success("3",
        R"({"isvalid":true,"address":"bchreg:qpzz8n7jp6847yyx8t33matrgcsdx6c0cv99t2zdvc",)"
        R"("scriptPubKey":"76a9144423cfd20e8f5f10863ae31df5634620d36b0fc388ac",)"
        R"("isscript":false,"istokenaware":false})"));
    REQUIRE(request(R"(["bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz"])") ==
        node::rpc::build_success("3", R"({"isvalid":false})"));
    CHECK(request(R"([""])") == node::rpc::build_success("3", R"({"isvalid":false})"));
    // Invalid addresses are results, while wrong types and arity are RPC errors.
    CHECK(request(R"(["not an address"])") == node::rpc::build_success("3", R"({"isvalid":false})"));
    for (auto const* params : {
        "[]", "[null]", "[false]", "[123]", "[{}]", "[[]]", "null", "{}", R"("")",
        R"(["bchreg:qpzz8n7jp6847yyx8t33matrgcsdx6c0cv99t2zdvc", true])",
        R"(["", "ignored"])", R"(["not an address", "ignored"])"}) {
        CAPTURE(params);
        CHECK(request(params) == node::rpc::build_error("3", -32602, "validateaddress requires [address]"));
    }
}

TEST_CASE("help dispatch lists the registered methods", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_help");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    // Custom methods are registered before the final built-in help snapshot.
    dispatcher.add("custommethod", [](node::rpc::method_context&, node::rpc::request const&)
        -> ::asio::awaitable<std::expected<std::string, node::rpc::rpc_error>> {
        co_return std::string("null");
    });
    node::rpc::register_builtin_methods(dispatcher);

    auto request = [&](std::string const& params) {
        auto const body = R"({"method":"help","params":)" + params + R"(,"id":5})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };

    // help is in its own list.
    auto const methods = dispatcher.methods();
    REQUIRE(std::ranges::is_sorted(methods));
    REQUIRE(std::ranges::binary_search(methods, std::string("help")));
    REQUIRE(std::ranges::binary_search(methods, std::string("getblockcount")));

    // One method per line, newlines escaped as the JSON string carries them.
    std::string listing;
    for (auto const& method : methods) {
        listing += (listing.empty() ? "" : "\\n") + method;
    }
    REQUIRE(request("[]") == node::rpc::build_success("5", '"' + listing + '"'));
    REQUIRE(request(R"([""])") == request("[]"));

    // Fulcrum sends this on every connection and needs a successful reply.
    REQUIRE(request(R"(["help"])") == node::rpc::build_success("5", R"("help")"));
    REQUIRE(request(R"(["getblockhash"])") == node::rpc::build_success("5", R"("getblockhash")"));
    REQUIRE(request(R"(["nosuchmethod"])") ==
        node::rpc::build_success("5", R"("help: unknown command: nosuchmethod")"));

    REQUIRE(request(R"(["custommethod"])") == node::rpc::build_success("5", R"("custommethod")"));
}

TEST_CASE("help dispatch rejects wrong argument types and excess arguments", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_help_argument_validation");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](std::string const& body) {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };
    for (auto const* params : {"[123]", "[false]", "[null]", "[{}]", "[[]]",
        "null", "{}", R"("help")", R"(["help","extra"])",
        R"(["nosuchmethod",7])", R"(["",false])"}) {
        CAPTURE(params);
        auto const body = std::string(R"({"method":"help","params":)") + params + R"(,"id":"query"})";
        CHECK(request(body) == node::rpc::build_error(R"("query")", -32602, "help requires [] or [command]"));
    }
    // Omitted params and an empty string retain the listing behavior.
    REQUIRE(request(R"({"method":"help","id":9})") ==
        request(R"({"method":"help","params":[],"id":9})"));
    REQUIRE(request(R"({"method":"help","params":[""],"id":9})") ==
        request(R"({"method":"help","params":[],"id":9})"));
    REQUIRE(request(R"({"method":"help","params":["help"],"id":null})") ==
        node::rpc::build_success("null", R"("help")"));
}

TEST_CASE("dispatcher reuses the prepared sorted method list", "[rpc dispatch]") {
    node::rpc::dispatcher dispatcher;
    auto handler = [](node::rpc::method_context&, node::rpc::request const&)
        -> ::asio::awaitable<std::expected<std::string, node::rpc::rpc_error>> {
        co_return std::string("null");
    };
    dispatcher.add("z", handler);
    dispatcher.add("a", handler);
    auto const& names = dispatcher.methods();
    REQUIRE(names == std::vector<std::string>{"a", "z"});
    // Repeated reads must reuse the prepared storage, not allocate/copy it.
    CHECK(dispatcher.methods().data() == names.data());
    dispatcher.add("a", handler);
    CHECK(dispatcher.methods() == std::vector<std::string>{"a", "z"});
}

TEST_CASE("help keeps prepared JSON current when methods are added or replaced", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_prepared_help");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    auto request = [&](std::string const& body) {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };
    dispatcher.add(R"(a"b\c)", [](node::rpc::method_context&, node::rpc::request const&)
        -> ::asio::awaitable<std::expected<std::string, node::rpc::rpc_error>> {
        co_return std::string("7");
    });
    node::rpc::register_builtin_methods(dispatcher);
    auto const listing = request(R"({"method":"help","params":[],"id":10})");
    CHECK(listing.find(R"(a\"b\\c)") != std::string::npos);
    REQUIRE(request(R"({"method":"help","params":["a\"b\\c"],"id":10})") ==
        node::rpc::build_success("10", R"("a\"b\\c")"));
    REQUIRE(request(R"({"method":"a\"b\\c","id":10})") == node::rpc::build_success("10", "7"));
    dispatcher.add(R"(a"b\c)", [](node::rpc::method_context&, node::rpc::request const&)
        -> ::asio::awaitable<std::expected<std::string, node::rpc::rpc_error>> {
        co_return std::string("9");
    });
    CHECK(request(R"({"method":"help","params":[],"id":10})") == listing);
    REQUIRE(request(R"({"method":"a\"b\\c","id":10})") == node::rpc::build_success("10", "9"));
    REQUIRE(request(R"({"method":"help","params":["a\"b\\c"],"id":10})") ==
        node::rpc::build_success("10", R"("a\"b\\c")"));
}
