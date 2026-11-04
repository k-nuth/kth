// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <optional>
#include "sync_harness.hpp"

#include <algorithm>
#include <string>
#include <type_traits>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/rpc/query.hpp>
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

TEST_CASE("getblockheader dispatch counts confirmations from the connected tip", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_header_confirmations");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Five blocks with their headers stored, three of them connected.
    auto const base_time = uint32_t(zulu_time()) - 40u * test::block_spacing;
    std::vector<domain::chain::block> trunk;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    auto const added = fixture.organizer().add_headers(test::headers_of(trunk));
    REQUIRE(added.headers_added == trunk.size());
    test::persist_headers(fixture, trunk, 1);
    test::connect_bodies(fixture, {trunk.begin(), trunk.begin() + 3}, 1);
    REQUIRE(fixture.chain().chain_view()->connected_tip_height == 3u);

    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto confirmations = [&](hash_digest const& hash) {
        auto const body = std::string(R"({"method":"getblockheader","params":[")")
            + encode_hash(hash) + R"(",true],"id":9})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        auto const raw = result.get();
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(raw);
        simdjson::ondemand::document document;
        REQUIRE(parser.iterate(padded).get(document) == simdjson::SUCCESS);
        std::int64_t value = 0;
        REQUIRE(document["result"]["confirmations"].get_int64().get(value) == simdjson::SUCCESS);
        return value;
    };

    REQUIRE(confirmations(domain::chain::block::genesis_regtest().hash()) == 4);
    REQUIRE(confirmations(trunk[1].hash()) == 2);
    REQUIRE(confirmations(trunk[2].hash()) == 1);
    // Stored, but its block is not connected yet.
    REQUIRE(confirmations(trunk[4].hash()) == -1);
}

TEST_CASE("unconnected replacement header never gets positive confirmations during reorg", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_confirmations_reorg_window");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const stamp = uint32_t(zulu_time()) - 40u * test::block_spacing;
    auto const genesis = domain::chain::block::genesis_regtest().hash();
    auto const common = test::mine_block(genesis, 1, stamp, 0, {}, 0);
    std::vector<domain::chain::block> a;
    auto previous = common.hash();
    for (uint32_t height = 2; height <= 3; ++height) {
        a.push_back(test::mine_block(previous, height, stamp + height * test::block_spacing, 1, {}, 0));
        previous = a.back().hash();
    }
    std::vector<domain::chain::block> connected{common};
    connected.insert(connected.end(), a.begin(), a.end());
    REQUIRE(fixture.organizer().add_headers(test::headers_of(connected)).headers_added == 3);
    test::persist_headers(fixture, connected, 1);
    test::connect_bodies(fixture, connected, 1);
    REQUIRE(chain.chain_view()->connected_tip_height == 3);
    std::vector<domain::chain::block> b;
    previous = common.hash();
    for (uint32_t height = 2; height <= 4; ++height) {
        b.push_back(test::mine_block(previous, height, stamp + height * test::block_spacing + 60, 2, {}, 0));
        previous = b.back().hash();
    }
    auto const candidate = fixture.organizer().add_headers(test::headers_of(b));
    REQUIRE(candidate.reorg_candidate);
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{chain, jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    std::string observed;
    size_t captured_height = 0;
    std::optional<uint32_t> built_height;
    // Use the real persister, then query while execute_reorg still owes its
    // publication. This fixes the interleaving without timing or extra threads.
    auto persist = [&](domain::chain::header::list const& headers, size_t start) {
        auto const ec = chain.replace_headers_from(headers, start);
        if ( ! ec) {
            captured_height = chain.chain_view()->connected_tip_height;
            if (auto const built = chain.get_utxo_built_height(); built) {
                built_height = *built;
            }
            auto const body = std::string(R"({"method":"getblockheader","params":[")") +
                encode_hash(b.front().hash()) + R"(",true],"id":7})";
            ::asio::io_context rpc_io;
            auto result = ::asio::co_spawn(rpc_io, dispatcher.handle(context, body), ::asio::use_future);
            rpc_io.run();
            observed = result.get();
        }
        return ec;
    };
    ::asio::io_context io;
    auto switched = ::asio::co_spawn(io, node::sync::execute_reorg(chain, fixture.organizer(),
        candidate.reorg_branch_head, 1, [] { return false; }, persist), ::asio::use_future);
    io.run();
    auto const outcome = switched.get();
    REQUIRE(outcome.result.ok);
    REQUIRE_FALSE(outcome.fatal);
    REQUIRE(chain.chain_view()->connected_tip_height == 1);
    REQUIRE(captured_height == 3);
    REQUIRE(built_height == 1);
    CAPTURE(observed);
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(observed);
    simdjson::ondemand::document document;
    REQUIRE(parser.iterate(padded).get(document) == simdjson::SUCCESS);
    bool no_error = false;
    REQUIRE(document["error"].is_null().get(no_error) == simdjson::SUCCESS);
    if (no_error) {
        int64_t confirmations = 0;
        REQUIRE(document["result"]["confirmations"].get_int64().get(confirmations) == simdjson::SUCCESS);
        CHECK(confirmations == -1);
    } else {
        // A defined transient failure is acceptable during the closed capture
        // window. A malformed response or an argument error must not pass.
        bool no_result = false;
        REQUIRE(document["result"].is_null().get(no_result) == simdjson::SUCCESS);
        REQUIRE(no_result);
        int64_t code = 0;
        REQUIRE(document["error"]["code"].get_int64().get(code) == simdjson::SUCCESS);
        CHECK((code == -32603 || code == -1));
        std::string_view message;
        REQUIRE(document["error"]["message"].get_string().get(message) == simdjson::SUCCESS);
        CHECK_FALSE(message.empty());
    }
}

TEST_CASE("estimatefee dispatch answers with or without a target", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_estimate_fee");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);

    auto request = [&](std::string const& params) {
        auto const body = R"({"method":"estimatefee","params":)" + params + R"(,"id":2})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };

    auto const expected = node::rpc::build_success("2",
        node::rpc::render_estimate_fee(fixture.chain().chain_settings().byte_fee_satoshis));
    // Fulcrum sends one argument to nodes it does not know to take none.
    REQUIRE(request("[6]") == expected);
    REQUIRE(request("[]") == expected);

    // Anything else is refused, not answered with a rate.
    for (auto const* invalid : {"[{}]", "[{},6]", "[1,2]", "[-1]", "[6.5]", R"(["six"])", "[null]"}) {
        CAPTURE(invalid);
        REQUIRE(request(invalid) == node::rpc::build_error("2", -32602,
            "estimatefee takes no argument or [nblocks] (a non-negative integer)"));
    }
}

TEST_CASE("estimatefee dispatch rejects non-array params", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_estimatefee_roots");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    for (auto const* params : {"null", "{}", R"({"nblocks":6})", R"("six")"}) {
        CAPTURE(params);
        auto const body = std::string(R"({"method":"estimatefee","params":)") + params + R"(,"id":8})";
        ::asio::io_context io;
        auto answer = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        auto const raw = answer.get();
        CAPTURE(raw);
        CHECK(raw == node::rpc::build_error("8", -32602,
            "estimatefee takes no argument or [nblocks] (a non-negative integer)"));
    }
}
