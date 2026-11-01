// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <catch2/catch_approx.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/dispatch.hpp>
#include "../../blockchain/test/reorg_chain_fixture.hpp"
#include "sync_harness.hpp"

using namespace kth;

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
        return static_cast<double>(time(last) - time(first));
    };

    REQUIRE(rate("[2,5]") == Catch::Approx(2 * work / span(3, 5)));
    REQUIRE(rate("[1,3]") == Catch::Approx(work / span(2, 3)));
    // The tip, by default and by -1; a window longer than the chain stops at genesis.
    REQUIRE(rate("[2]") == rate("[2,5]"));
    REQUIRE(rate("[2,-1]") == rate("[2,5]"));
    REQUIRE(rate("[2,99]") == rate("[2,5]"));
    REQUIRE(rate("[]") == Catch::Approx(5 * work / span(0, 5)));
    REQUIRE(rate("[0]") == rate("[]"));
    REQUIRE(request(R"(["x"])") == node::rpc::build_error("4", -32602,
        "getnetworkhashps takes [nblocks, height] as integers"));
}
