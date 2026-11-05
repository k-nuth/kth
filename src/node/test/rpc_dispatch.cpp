// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <catch2/catch_approx.hpp>
#include <optional>
#include "sync_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <kth/node/detail/scope_action.hpp>
#include <string>
#include <type_traits>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/mining/job_store.hpp>
#include <kth/node/p2p_node.hpp>
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
        int64_t value = 0;
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

TEST_CASE("decoderawtransaction dispatch decodes for the node's network", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_decode_raw_transaction");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);

    auto request = [&](std::string const& params) {
        auto const body = R"({"method":"decoderawtransaction","params":)" + params + R"(,"id":6})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };

    // The regtest genesis coinbase, as EloPool decodes its own coinbase at startup.
    auto const coinbase = domain::chain::block::genesis_regtest().transactions().front();
    auto const hex = node::rpc::transaction_to_hex(coinbase);
    auto const reply = request(R"([")" + hex + R"("])");
    REQUIRE(reply.find(R"("txid":")" + encode_hash(coinbase.hash()) + '"') != std::string::npos);
    REQUIRE(reply.find(R"("vin":[{"coinbase":")") != std::string::npos);
    REQUIRE(reply.find(R"("error":null)") != std::string::npos);

    REQUIRE(request(R"([")" + hex + R"(00"])") ==
        node::rpc::build_error("6", -32602, "transaction decode failed"));
    REQUIRE(request(R"(["zz"])") == node::rpc::build_error("6", -32602, "hexstring is not valid hex"));
    REQUIRE(request("[]") == node::rpc::build_error("6", -32602, "decoderawtransaction requires [hexstring]"));
}

TEST_CASE("getnetworkhashps dispatch computes the rate over the connected chain", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_network_hash_ps");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Five connected blocks, two minutes apart.
    auto const base_time = uint32_t(zulu_time()) - 40u * test::block_spacing;
    auto const genesis = domain::chain::block::genesis_regtest();
    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= 5; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    auto const added = fixture.organizer().add_headers(test::headers_of(trunk));
    REQUIRE(added.headers_added == trunk.size());
    test::persist_headers(fixture, trunk, 1);
    test::connect_bodies(fixture, trunk, 1);
    REQUIRE(fixture.chain().chain_view()->connected_tip_height == 5u);

    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](std::string const& params) {
        auto const body = R"({"method":"getnetworkhashps","params":)" + params + R"(,"id":4})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };
    auto rate = [&](std::string const& params) {
        auto const raw = request(params);
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(raw);
        simdjson::ondemand::document document;
        REQUIRE(parser.iterate(padded).get(document) == simdjson::SUCCESS);
        double value = -1;
        REQUIRE(document["result"].get_double().get(value) == simdjson::SUCCESS);
        return value;
    };

    // Every regtest block adds the same work. The window of n blocks ending at
    // height h spans the times of blocks h - n through h.
    auto const work = domain::chain::header::proof(genesis.header().bits()).convert_to<double>();
    auto const span = [&](uint32_t first, uint32_t last) {
        auto const time = [&](uint32_t h) {
            return h == 0 ? genesis.header().timestamp() : trunk[h - 1].header().timestamp();
        };
        return double(time(last) - time(first));
    };

    REQUIRE(rate("[2,5]") == Catch::Approx(2 * work / span(3, 5)));
    REQUIRE(rate("[1,3]") == Catch::Approx(work / span(2, 3)));
    // The tip, by default and by -1; a window longer than the chain stops at genesis.
    REQUIRE(rate("[2]") == rate("[2,5]"));
    REQUIRE(rate("[2,-1]") == rate("[2,5]"));
    REQUIRE(rate("[2,99]") == rate("[2,5]"));
    REQUIRE(rate("[]") == Catch::Approx(5 * work / span(0, 5)));
    REQUIRE(rate("[null,null]") == rate("[]"));
    REQUIRE(rate("[2,null]") == rate("[2]"));
    // BCHN returns zero at genesis even for a non-positive lookup.
    REQUIRE(rate("[1,0]") == 0.0);
    REQUIRE(rate("[0,0]") == 0.0);
    REQUIRE(rate("[-1,0]") == 0.0);
    for (auto const* params : {"[0]", "[-1]"}) {
        CAPTURE(params);
        CHECK(request(params) == node::rpc::build_error("4", -8,
            "getnetworkhashps requires a positive number of blocks"));
    }
    for (auto const* params : {"null", "{}", "5", R"("blocks")", "[2,-1,7]",
         "[2147483648]", "[2,2147483648]", "[2,-2147483649]"}) {
        CAPTURE(params);
        CHECK(request(params) == node::rpc::build_error("4", -32602,
            "getnetworkhashps takes [nblocks, height] as integers"));
    }
    REQUIRE(request(R"(["x"])") == node::rpc::build_error("4", -32602,
        "getnetworkhashps takes [nblocks, height] as integers"));
}

namespace {

// The getblockchaininfo response from `chain`, through the dispatcher.
std::string blockchain_info(blockchain::block_chain& chain) {
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{chain, jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io,
        dispatcher.handle(context, R"({"method":"getblockchaininfo","params":[],"id":1})"),
        ::asio::use_future);
    io.run();
    return result.get();
}

} // namespace

TEST_CASE("getblockchaininfo dispatch reports the connected tip from one view", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_blockchain_info");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const raw = blockchain_info(fixture.chain());
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(raw);
    simdjson::ondemand::document document;
    REQUIRE(parser.iterate(padded).get(document) == simdjson::SUCCESS);
    simdjson::ondemand::object result;
    REQUIRE(document["result"].get_object().get(result) == simdjson::SUCCESS);

    auto const genesis = domain::chain::block::genesis_regtest();
    std::string_view text;
    uint64_t number = 0;
    bool flag = false;
    double progress = 0;

    REQUIRE(result["chain"].get_string().get(text) == simdjson::SUCCESS);
    REQUIRE(text == "regtest");
    REQUIRE(result["blocks"].get_uint64().get(number) == simdjson::SUCCESS);
    REQUIRE(number == 0u);
    REQUIRE(result["headers"].get_uint64().get(number) == simdjson::SUCCESS);
    REQUIRE(number == 0u);
    REQUIRE(result["bestblockhash"].get_string().get(text) == simdjson::SUCCESS);
    REQUIRE(text == encode_hash(genesis.hash()));
    REQUIRE(result["mediantime"].get_uint64().get(number) == simdjson::SUCCESS);
    REQUIRE(number == genesis.header().timestamp());
    REQUIRE(result["verificationprogress"].get_double().get(progress) == simdjson::SUCCESS);
    REQUIRE(progress == 1.0);
    // The regtest genesis is from 2011, far past the default 24-hour limit.
    REQUIRE(result["initialblockdownload"].get_bool().get(flag) == simdjson::SUCCESS);
    REQUIRE(flag);
    REQUIRE(result["chainwork"].get_string().get(text) == simdjson::SUCCESS);
    // Regtest genesis has proof 2, rendered as a padded 256-bit integer.
    REQUIRE(text == std::string(63, '0') + "2");
    REQUIRE(result["size_on_disk"].get_uint64().get(number) == simdjson::SUCCESS);
    REQUIRE(result["warnings"].get_string().get(text) == simdjson::SUCCESS);
    REQUIRE(text.empty());
}

TEST_CASE("getblockchaininfo dispatch leaves initial download once synchronized", "[rpc dispatch]") {
    // A limit of zero turns the clock half off; the genesis-only chain has
    // connected everything it has headers for, so the node is synchronized.
    test::chain_fixture fixture("rpc_blockchain_info_synced", /*notify_limit_hours*/ 0);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    REQUIRE(fixture.chain().synchronization().synchronized());

    auto const raw = blockchain_info(fixture.chain());
    REQUIRE(raw.find(R"("initialblockdownload":false)") != std::string::npos);
}

TEST_CASE("getchaintips dispatch reports the connected tip and the branches beyond it", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_chain_tips");
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
    REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
    test::persist_headers(fixture, trunk, 1);
    test::connect_bodies(fixture, {trunk.begin(), trunk.begin() + 3}, 1);
    REQUIRE(fixture.chain().chain_view()->connected_tip_height == 3u);

    // A two-block side branch off block 2, headers only, with less work than the
    // trunk. The organizer stores it and reports the batch as not advancing the
    // tip (`stale_chain`, nothing added), which is the contract for a side branch.
    std::vector<domain::chain::block> side;
    prev = trunk[1].hash();
    for (uint32_t h = 3; h <= 4; ++h) {
        side.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing + 60, /*salt*/ 1, {}, 0));
        prev = side.back().hash();
    }
    auto const added = fixture.organizer().add_headers(test::headers_of(side));
    REQUIRE(added.headers_added == 0u);
    REQUIRE(added.error == error::stale_chain);
    REQUIRE(fixture.chain().headers().find(side[1].hash()) != blockchain::header_index::null_index);

    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io,
        dispatcher.handle(context, R"({"method":"getchaintips","params":[],"id":3})"),
        ::asio::use_future);
    io.run();

    REQUIRE(result.get() == node::rpc::build_success("3", node::rpc::render_chain_tips({
        {5u, trunk[4].hash(), 2u, blockchain::header_index::tip_status::headers_only},
        {4u, side[1].hash(), 2u, blockchain::header_index::tip_status::headers_only},
        {3u, trunk[2].hash(), 0u, blockchain::header_index::tip_status::active}})));
}

TEST_CASE("getchaintips shares ancestry across many tips without stale availability", "[rpc dispatch][rpc chain tips]") {
    test::chain_fixture fixture("rpc_chain_tips_shared");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& index = fixture.chain().headers();
    auto const genesis = domain::chain::block::genesis_regtest().hash();
    auto add = [&](hash_digest const& parent, uint32_t nonce) {
        domain::chain::header header{1, parent, null_hash, 1700000000u + nonce, 0x207fffffu, nonce};
        auto const hash = domain::chain::hash(header);
        auto const result = index.add(hash, header);
        REQUIRE(result.inserted);
        index.add_status(result.index, blockchain::header_status::have_data);
        return std::pair{hash, result.index};
    };

    constexpr size_t shared_length = 512;
    constexpr size_t leaves = 64;
    auto parent = genesis;
    auto missing = blockchain::header_index::null_index;
    for (size_t i = 0; i < shared_length; ++i) {
        auto const [hash, idx] = add(parent, uint32_t(i + 1));
        if (i == 0) missing = idx;
        parent = hash;
    }
    index.set_status(missing, blockchain::header_status::none);
    std::vector<node::rpc::chain_tip> expected;
    for (size_t i = 0; i < leaves; ++i) {
        auto const [hash, idx] = add(parent, uint32_t(shared_length + i + 1));
        expected.push_back({shared_length + 1, hash, shared_length + 1, blockchain::header_index::tip_status::headers_only});
    }
    auto const [available, available_idx] = add(genesis, 9000);
    expected.push_back({1, available, 1, blockchain::header_index::tip_status::valid_headers});
    expected.push_back({0, genesis, 0, blockchain::header_index::tip_status::active});
    // An unlinked root must not appear as a branch of the connected chain.
    auto const [unlinked, unlinked_idx] = add(null_hash, 10000);
    REQUIRE(index.get_parent_index(unlinked_idx) == blockchain::header_index::null_index);

    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto query = [&] {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io,
            dispatcher.handle(context, R"({"method":"getchaintips","params":[],"id":3})"),
            ::asio::use_future);
        io.run();
        return result.get();
    };
    REQUIRE(query() == node::rpc::build_success("3", node::rpc::render_chain_tips(expected)));
    REQUIRE_FALSE(index.get_chain_tips(null_hash));
    auto const previous_tips = index.get_chain_tips(genesis);
    REQUIRE(previous_tips);
    // Data appearing on a shared ancestor changes every descendant's status.
    index.add_status(missing, blockchain::header_status::have_data);
    for (size_t i = 0; i < leaves; ++i) expected[i].status = blockchain::header_index::tip_status::valid_headers;
    // Returned records own their values; a later mutation cannot alter them.
    REQUIRE(previous_tips->front().status == blockchain::header_index::tip_status::headers_only);
    REQUIRE(query() == node::rpc::build_success("3", node::rpc::render_chain_tips(expected)));
}

TEST_CASE("gettxout dispatch reads the UTXO set and the mempool", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_get_tx_out");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Five connected blocks.
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
    test::connect_bodies(fixture, trunk, 1);
    REQUIRE(fixture.chain().chain_view()->connected_tip_height == 5u);

    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](std::string const& params) {
        auto const body = R"({"method":"gettxout","params":)" + params + R"(,"id":7})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };
    auto const quoted = [](hash_digest const& hash) { return '"' + encode_hash(hash) + '"'; };

    // The coinbase of block 2, four blocks deep.
    auto const coinbase = trunk[1].transactions().front().hash();
    auto const reply = request("[" + quoted(coinbase) + ",0]");
    CHECK(reply.find(R"("bestblock":)" + quoted(trunk[4].hash())) != std::string::npos);
    CHECK(reply.find(R"("confirmations":4,"value":50,)") != std::string::npos);
    CHECK(reply.find(R"("coinbase":true})") != std::string::npos);

    // No such output, and no such transaction.
    auto const none = node::rpc::build_success("7", "null");
    CHECK(request("[" + quoted(coinbase) + ",1]") == none);
    CHECK(request("[" + quoted(null_hash) + ",0]") == none);

    // A mempool transaction that spends it: spent with include_mempool, still
    // unspent in the confirmed set without; the pool transaction's own output
    // the other way around.
    domain::chain::input::list ins;
    ins.emplace_back(domain::chain::output_point{coinbase, 0}, domain::chain::script{}, 0xffffffffu);
    domain::chain::output::list outs;
    outs.emplace_back(4999990000u, test::miner_script(), domain::chain::token_data_opt{});
    auto const spend = std::make_shared<domain::message::transaction>(
        domain::chain::transaction{2u, 0u, std::move(ins), std::move(outs)});
    REQUIRE(fixture.chain().mempool_ref().add(blockchain::mempool_entry{
        spend, 10000u, uint32_t(spend->serialized_size(true)), 1u, 0u}));

    CHECK(request("[" + quoted(coinbase) + ",0]") == none);
    CHECK(request("[" + quoted(coinbase) + ",0,true]") == none);
    CHECK(request("[" + quoted(coinbase) + ",0,false]") == reply);
    auto const pooled = request("[" + quoted(spend->hash()) + ",0]");
    CHECK(pooled.find(R"("confirmations":0,"value":49.9999,)") != std::string::npos);
    CHECK(pooled.find(R"("coinbase":false})") != std::string::npos);
    CHECK(request("[" + quoted(spend->hash()) + ",0,false]") == none);

    CHECK(request("[" + quoted(coinbase) + "]") ==
        node::rpc::build_error("7", -32602, "gettxout requires [txid, n]"));
    CHECK(request("[" + quoted(coinbase) + R"(,0,"yes"])") ==
        node::rpc::build_error("7", -32602, "include_mempool must be a boolean"));
}

TEST_CASE("getnetworkinfo dispatch needs the P2P node", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_network_info");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    // No P2P node in this context, as when the server runs without one.
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);

    ::asio::io_context io;
    auto result = ::asio::co_spawn(io,
        dispatcher.handle(context, R"({"method":"getnetworkinfo","params":[],"id":8})"),
        ::asio::use_future);
    io.run();
    REQUIRE(result.get() == node::rpc::build_error("8", -32603, "network unavailable"));
}

TEST_CASE("getnetworkinfo reads the P2P node and rejects arguments", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_network_info_node");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    network::settings settings;
    settings.threads = 1;
    settings.inbound_port = 0;
    settings.outbound_connections = 0;
    settings.user_agent = "/test:1/";
    settings.hosts_file = fixture.dir() / "hosts.cache";
    settings.banlist_file = fixture.dir() / "banlist.cache";
    settings.peers_file = fixture.dir() / "peers.cache";
    node::p2p_node network(settings);
    REQUIRE(network.stopped());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs, &network};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](std::string_view params) {
        ::asio::io_context io;
        auto const body = params.empty()
            ? std::string(R"({"method":"getnetworkinfo","id":42})")
            : std::string(R"({"method":"getnetworkinfo","params":)") + std::string(params) + R"(,"id":42})";
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };

    // A node that has not started: inactive, no connections, its settings.
    auto const reply = request("[]");
    INFO(reply);
    CHECK(reply.find(R"("subversion":"/test:1/")") != std::string::npos);
    CHECK(reply.find(R"("networkactive":false)") != std::string::npos);
    CHECK(reply.find(R"("connections":0)") != std::string::npos);
    CHECK(reply.find(R"("error":null)") != std::string::npos);
    CHECK(request("") == reply);
    CHECK(request("[ ]") == reply);

    for (auto const params : {"[1]", "[null]", "[[]]", "[{},2]", "{}", "null", "1", R"("x")"}) {
        CAPTURE(params);
        CHECK(request(params) == node::rpc::build_error("42", -32602, "getnetworkinfo takes no arguments"));
    }
}

TEST_CASE("getrawmempool dispatch lists entries keyed by txid when verbose", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_raw_mempool_verbose");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // A parent and a child that spends it.
    auto const make = [](hash_digest const& prevout_hash, uint64_t value) {
        domain::chain::input::list ins;
        ins.emplace_back(domain::chain::output_point{prevout_hash, 0}, domain::chain::script{}, 0xffffffffu);
        domain::chain::output::list outs;
        outs.emplace_back(value, domain::chain::script{}, domain::chain::token_data_opt{});
        return std::make_shared<domain::message::transaction>(
            domain::chain::transaction{2u, 0u, std::move(ins), std::move(outs)});
    };
    hash_digest outside{};
    outside[0] = 0x42;
    auto const parent = make(outside, 5000u);
    auto const child = make(parent->hash(), 4000u);
    auto& pool = fixture.chain().mempool_ref();
    REQUIRE(pool.add(blockchain::mempool_entry{parent, 300u, uint32_t(parent->serialized_size(true)), 1u, 10u}));
    REQUIRE(pool.add(blockchain::mempool_entry{child, 200u, uint32_t(child->serialized_size(true)), 1u, 20u}));

    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](std::string const& params) {
        auto const body = R"({"method":"getrawmempool","params":)" + params + R"(,"id":14})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };
    auto const quoted = [](hash_digest const& hash) { return '"' + encode_hash(hash) + '"'; };

    auto const verbose = request("[true]");
    CHECK(verbose.find(quoted(parent->hash()) + R"(:{"fees":{"base":0.000003,"modified":0.000003},)") !=
        std::string::npos);
    CHECK(verbose.find(R"("depends":[],"spentby":[)" + quoted(child->hash()) + "]") != std::string::npos);
    CHECK(verbose.find(quoted(child->hash()) + R"(:{"fees":{"base":0.000002,"modified":0.000002},)") !=
        std::string::npos);
    CHECK(verbose.find(R"("depends":[)" + quoted(parent->hash()) + R"(],"spentby":[])") != std::string::npos);

    // Not verbose: the txids, as before.
    CHECK(request("[]") == request("[false]"));
    CHECK(request("[]").find(R"("result":[)") != std::string::npos);
    CHECK(request(R"(["yes"])") == node::rpc::build_error("14", -32602, "verbose must be a boolean"));
    for (auto const* params : {"{}", "null", "1", "[true,false]", "[false,{}]"}) {
        CAPTURE(params);
        CHECK(request(params) == node::rpc::build_error("14", -32602, "verbose must be a boolean"));
    }
    CHECK(request("[null]") == request("[]"));

}

TEST_CASE("getblocktemplate dispatch serves the template with its transactions", "[rpc dispatch]") {
    // A limit of zero keeps the genesis-only chain synchronized, so the node
    // serves mining work.
    test::chain_fixture fixture("rpc_block_template", /*notify_limit_hours*/ 0);
    REQUIRE(fixture.created());
    fixture.settings().gbt_template_refresh_seconds = max_uint32;
    REQUIRE(fixture.start());

    // One pooled transaction; the template builder takes the pool as given.
    domain::chain::input::list ins;
    hash_digest prevout{};
    prevout[0] = 0x42;
    ins.emplace_back(domain::chain::output_point{prevout, 0}, domain::chain::script{}, 0xffffffffu);
    domain::chain::output::list outs;
    outs.emplace_back(1000u, domain::chain::script{}, domain::chain::token_data_opt{});
    auto const tx = std::make_shared<domain::message::transaction>(
        domain::chain::transaction{1u, 0u, std::move(ins), std::move(outs)});
    REQUIRE(fixture.chain().mempool_ref().add(blockchain::mempool_entry{
        tx, 500u, uint32_t(tx->serialized_size(true)), 3u, 0u}));

    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](std::string const& params) {
        auto const body = R"({"method":"getblocktemplate","params":)" + params + R"(,"id":11})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };

    // The request ASICseer pool sends, and EloPool's without rules.
    auto const reply = request(R"([{"capabilities":["coinbasetxn","workid","coinbase/append"],"rules":[]}])");
    REQUIRE(reply.find(R"("txid":")" + encode_hash(tx->hash()) + R"(","hash":")") != std::string::npos);
    REQUIRE(reply.find(R"("fee":500,"sigops":3})") != std::string::npos);
    REQUIRE(reply.find(R"("previousblockhash":")" +
        encode_hash(domain::chain::block::genesis_regtest().hash()) + '"') != std::string::npos);
    REQUIRE(reply.find(R"("height":1})") != std::string::npos);
    REQUIRE(request(R"([{"capabilities":["coinbasetxn","workid","coinbase/append"]}])").find(R"("error":null)") !=
        std::string::npos);
    REQUIRE(request("[]").find(R"("error":null)") != std::string::npos);
    REQUIRE(request(R"([{"mode":"proposal","data":"00"}])") ==
        node::rpc::build_error("11", -32602, "only template mode is supported"));

    // A cache hit must retain metadata from the same capture as its selection.
    // Change only the pool, leaving the template's parent and refresh budget intact.
    domain::chain::block removed;
    removed.set_transactions({*tx});
    fixture.chain().mempool_remove_for_block(removed);
    REQUIRE_FALSE(fixture.chain().mempool_ref().entry(tx->hash()));
    CHECK(request(R"([{"capabilities":["coinbasetxn","workid","coinbase/append"],"rules":[]}])") == reply);
    REQUIRE(fixture.chain().mempool_ref().add(blockchain::mempool_entry{
        tx, 900u, uint32_t(tx->serialized_size(true)), 7u, 0u}));
    CHECK(request(R"([{"capabilities":["coinbasetxn","workid","coinbase/append"],"rules":[]}])") == reply);

}

TEST_CASE("getblocktemplate rejects invalid requests and omits unsupported long polling", "[rpc dispatch][rpc gbt validation]") {
    test::chain_fixture fixture("rpc_block_template_validation", 0);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](std::string const& params) {
        auto const body = R"({"method":"getblocktemplate","params":)" + params + R"(,"id":17})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };
    auto const normal = request("[]");
    REQUIRE(normal.find(R"("error":null)") != std::string::npos);
    REQUIRE(request(R"([{"mode":"proposal"}])").find(R"("result":null)") != std::string::npos);
    SECTION("invalid argument types and extra arguments are rejected") {
        for (auto const* params : {"1", "{}", "[1]", R"([{"mode":3}])", R"([{"mode":false}])", "[{},{}]"}) {
            CAPTURE(params);
            auto const raw = request(params);
            INFO(raw);
            CHECK(raw.find(R"("result":null)") != std::string::npos);
        }
    }
    // BIP22 requires omitting longpollid when long polling is unsupported.
    SECTION("no longpollid when long polling is unsupported") {
        INFO(normal);
        CHECK(normal.find(R"("longpollid")") == std::string::npos);
    }
}

TEST_CASE("getblockchaininfo disk size counts only block and undo files", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_disk_size");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto size = [&]() {
        simdjson::ondemand::parser parser;
        auto const raw = blockchain_info(fixture.chain());
        simdjson::padded_string padded(raw);
        uint64_t value = 0;
        simdjson::ondemand::document document;
        REQUIRE(parser.iterate(padded).get(document) == simdjson::SUCCESS);
        REQUIRE(document["result"]["size_on_disk"].get_uint64().get(value) == simdjson::SUCCESS);
        return value;
    };
    auto const before = size();
    auto const blocks = fixture.dir() / "blocks";
    auto make_file = [&](std::filesystem::path const& path, uint64_t bytes) {
        auto* file = std::fopen(path.string().c_str(), "wb");
        REQUIRE(file != nullptr);
        REQUIRE(std::fclose(file) == 0);
        std::filesystem::resize_file(path, bytes);
    };
    make_file(blocks / "blk99999.dat", 11);
    make_file(blocks / "rev100000.dat", 7);
    make_file(blocks / "notes.txt", 100);
    make_file(blocks / "blk00001.dat.bak", 200);
    make_file(blocks / "blkabcde.dat", 300);
    make_file(blocks / "blk1234.dat", 400);
    std::filesystem::create_directory(blocks / "blk12345.dat");
    uint64_t linked = 0;
#if ! defined(_WIN32)
    // A block file moved to another disk and linked back still counts; a
    // dangling link is skipped.
    make_file(fixture.dir() / "external.dat", 500);
    std::filesystem::create_symlink(fixture.dir() / "external.dat", blocks / "rev99999.dat");
    std::filesystem::create_symlink(fixture.dir() / "missing.dat", blocks / "blk88888.dat");
    linked = 500;
#endif
    REQUIRE(size() == before + 18 + linked);
}

TEST_CASE("getblockchaininfo still answers when the block files cannot be listed", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_disk_size_error");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const blocks = fixture.dir() / "blocks";
    auto const saved = fixture.dir() / "saved-blocks";
    // Genesis-only fixtures have no open block/undo files (also on Windows).
    REQUIRE(std::filesystem::is_empty(blocks));
    std::filesystem::rename(blocks, saved);
    node::detail::scope_action restore([&]() { std::filesystem::rename(saved, blocks); }, true);
    auto const raw = blockchain_info(fixture.chain());
    // Restore the open store's paths before checking the RPC observation.
    restore.run();
    REQUIRE(std::filesystem::is_directory(blocks));
    // size_on_disk is informational: the listing error is logged, not returned.
    REQUIRE(raw.find(R"("error":null)") != std::string::npos);
    REQUIRE(raw.find(R"("size_on_disk":0,)") != std::string::npos);
}

TEST_CASE("getblockchaininfo uses the active header tip while blocks are behind", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_headers_ahead", 0);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& index = fixture.chain().headers();
    auto previous = domain::chain::block::genesis_regtest().hash();
    for (uint32_t height = 1; height <= 2; ++height) {
        domain::chain::header header{1u, previous, null_hash, 1296688602u + height, 0x207fffffu, height};
        previous = domain::chain::hash(header);
        auto const added = index.add(previous, header);
        REQUIRE(added.inserted);
        index.active_push(added.index);
    }
    // This is the header-only reception state: no connected block publication.
    REQUIRE(fixture.chain().chain_view()->connected_tip_height == 0u);
    REQUIRE(index.active_tip_height() == 2);
    auto const raw = blockchain_info(fixture.chain());
    REQUIRE(raw.find(R"("blocks":0)") != std::string::npos);
    REQUIRE(raw.find(R"("headers":2)") != std::string::npos);
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(raw);
    simdjson::ondemand::document document;
    REQUIRE(parser.iterate(padded).get(document) == simdjson::SUCCESS);
    double progress = 1.0;
    REQUIRE(document["result"]["verificationprogress"].get_double().get(progress) == simdjson::SUCCESS);
    REQUIRE(progress == 0.0);
    REQUIRE(raw.find(R"("initialblockdownload":true)") != std::string::npos);
}
