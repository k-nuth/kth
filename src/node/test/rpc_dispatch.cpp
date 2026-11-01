// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/infrastructure/utility/timer.hpp>
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

TEST_CASE("submitblock dispatch rejects malformed parameters and block data", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_submitblock_malformed");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);

    auto request = [&](std::string const& method, std::string const& params) {
        auto body = std::string(R"({"method":")") + method + R"(","id":8)";
        if ( ! params.empty()) {
            body += R"(,"params":)" + params;
        }
        body += '}';
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };

    auto const hex = encode_base16(to_data_chunk(domain::chain::block::genesis_regtest()));
    for (auto const& params : std::vector<std::string>{
             "", "[]", "{}", "[0]", "[null]", R"([""])", R"(["zz"])", R"(["0"])", R"(["00"])",
             R"([")" + hex.substr(0, hex.size() - 2) + R"("])",
             R"([")" + hex + R"(00"])",
             R"([")" + hex + R"(",null,null])"}) {
        CAPTURE(params);
        auto const response = request("submitblock", params);
        REQUIRE(response.find(R"("result":null)") != std::string::npos);
        REQUIRE(response.find(R"("code":-32602)") != std::string::npos);
    }
}

TEST_CASE("submitblock dispatch returns chain rejection reasons and ignores the dummy parameter", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_submitblock_rejected");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const timestamp = uint32_t(zulu_time()) - test::block_spacing;
    std::vector<domain::chain::block> const blocks{
        test::mine_block(genesis.hash(), 1, timestamp, 0, {}, 0)};
    REQUIRE(fixture.organizer().add_headers(test::headers_of(blocks)).headers_added == 1u);
    test::persist_headers(fixture, blocks, 1);
    test::connect_bodies(fixture, blocks, 1);
    auto const view = fixture.chain().chain_view();
    REQUIRE(view);
    REQUIRE(view->connected_tip_height == 1u);
    REQUIRE(view->tip_hash == blocks.front().hash());

    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](domain::chain::block const& block, std::string const& suffix) {
        auto const body = std::string(R"({"method":"submitblock","params":[")")
            + encode_base16(to_data_chunk(block)) + '"' + suffix + R"(],"id":9})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };

    for (auto const* suffix : {"", ",\"dummy\"", ",null", ",{}"}) {
        CAPTURE(suffix);
        REQUIRE(request(blocks.front(), suffix) == node::rpc::build_success("9", R"("duplicate block")"));
    }

    // The block passes validation, but this fixture has no pipeline accepting submissions.
    auto const next = test::mine_block(blocks.front().hash(), 2,
        timestamp + test::block_spacing, 0, {}, 0);
    REQUIRE(request(next, "") == node::rpc::build_success("9", R"("service stopped")"));
    auto const after = fixture.chain().chain_view();
    REQUIRE(after);
    REQUIRE(after->connected_tip_height == 1u);
    REQUIRE(after->tip_hash == blocks.front().hash());
}
