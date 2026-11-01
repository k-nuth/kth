// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <future>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/rpc/query.hpp>
#include <kth/domain/math/mmr.hpp>

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
        io.run_for(std::chrono::seconds(2));
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
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
    REQUIRE(verbose.find("shvroot") == std::string::npos);
    REQUIRE(request("") == verbose);
    REQUIRE(request(",null") == verbose);
    for (auto const* invalid : {",0", ",1", ",\"false\"", ",{}", ",[]"}) {
        CAPTURE(invalid);
        REQUIRE(request(invalid) == node::rpc::build_error("7", -32602, "verbose must be a boolean"));
    }
}

TEST_CASE("getshv dispatch exposes proofs and preserves BCHN semantic errors", "[rpc dispatch][shv]") {
    test::chain_fixture fixture("rpc_shv");
    fixture.settings().mmr_index = true;
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);

    auto request = [&](std::string const& method, std::string const& params) {
        auto const body = std::string(R"({"method":")") + method
            + R"(","params":)" + params + R"(,"id":7})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run_for(std::chrono::seconds(2));
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        return result.get();
    };

    auto const genesis = domain::chain::block::genesis_regtest();
    auto const hash = encode_hash(genesis.hash());
    for (auto const* type : {"root", "peak"}) {
        auto const params = std::string("[\"") + type + "\",0,0]";
        REQUIRE(request("getshv", params) == node::rpc::build_success("7",
            node::rpc::render_shv(genesis.header(), {}, genesis.hash(), std::string_view(type) == "root")));
    }
    auto const root_field = std::string(R"("shvroot":")") + hash + '"';
    REQUIRE(request("getblockheader", "[\"" + hash + "\"]").find(root_field) != std::string::npos);
    REQUIRE(request("getblockchaininfo", "[]").find(root_field) != std::string::npos);
    REQUIRE(request("getblockheader", "[\"" + hash + "\",false]").find("shvroot") == std::string::npos);

    auto expect_error = [&](std::string const& params, int code, char const* message) {
        CAPTURE(params);
        REQUIRE(request("getshv", params) == node::rpc::build_error("7", code, message));
    };
    for (auto const* params : {"[]", "[\"root\",0]", "[\"root\",0,0,0]", "{}"}) {
        expect_error(params, -32602, "getshv requires [type, commitment_height, block_height]");
    }
    for (auto const* type : {"0", "false", "null", "{}", "[]"}) {
        expect_error(std::string("[") + type + ",0,0]", -3, "type must be a string");
    }
    for (auto const* height : {"2147483648", "18446744073709551616", "\"0\"", "0.5", "true", "null", "{}", "[]"}) {
        expect_error(std::string("[\"root\",") + height + ",0]", -3,
            "commitment_height must be a 32-bit integer");
        expect_error(std::string("[\"root\",0,") + height + ']', -3,
            "block_height must be a 32-bit integer");
    }
    expect_error("[\"root\",-1,0]", -8, "commitment_height must be non-negative");
    expect_error("[\"root\",0,-1]", -8, "block_height must be non-negative");
    expect_error("[\"root\",0,1]", -8, "block_height cannot exceed commitment_height");
    expect_error("[\"root\",1,1]", -8, "Block not found");
    expect_error("[\"root\",1,0]", -8, "Failed to generate proof");
    expect_error("[\"peak\",1,0]", -8, "Failed to generate proof");
    expect_error("[\"unknown\",0,0]", -8, "Unknown type. Use: root, peak");
    expect_error("[\"\",0,0]", -8, "Unknown type. Use: root, peak");

    auto previous = genesis.hash();
    auto const base_time = uint32_t(zulu_time()) - 40 * test::block_spacing;
    std::vector<domain::chain::block> blocks;
    for (uint32_t height = 1; height <= 2; ++height) {
        blocks.push_back(test::mine_block(previous, height,
            base_time + height * test::block_spacing, 0, {}, 0));
        previous = blocks.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(test::headers_of(blocks)).headers_added == blocks.size());
    test::persist_headers(fixture, blocks, 1);
    auto const ahead = request("getblockheader", "[\"" + encode_hash(blocks.back().hash()) + "\"]");
    REQUIRE(ahead.find(R"("result":{)") != std::string::npos);
    REQUIRE(ahead.find("shvroot") == std::string::npos);
    REQUIRE(request("getblockchaininfo", "[]").find(root_field) != std::string::npos);
    test::connect_bodies(fixture, blocks, 1);

    domain::mmr_accumulator client;
    REQUIRE(client.extend(genesis.hash()));
    for (auto const& block : blocks) {
        REQUIRE(client.extend(block.hash()));
    }
    for (uint64_t height = 0; height <= 2; ++height) {
        for (bool to_root : {false, true}) {
            auto const proof = fixture.chain().shv_proof(height, 2, to_root);
            REQUIRE(proof);
            auto const params = std::string("[\"") + (to_root ? "root" : "peak")
                + "\",2," + std::to_string(height) + ']';
            REQUIRE(request("getshv", params) == node::rpc::build_success("7",
                node::rpc::render_shv(proof->header, proof->proof, proof->target, to_root)));
            auto const leaf = domain::chain::hash(proof->header);
            REQUIRE((to_root ? client.verify_proof_to_root(height, leaf, proof->proof)
                : client.verify_proof_to_peak(height, leaf, proof->proof)));
        }
    }
    REQUIRE(request("getblockchaininfo", "[]").find(
        std::string(R"("shvroot":")") + encode_hash(client.root()) + '"') != std::string::npos);
}

TEST_CASE("getshv requires an enabled index and blockchaininfo omits its root", "[rpc dispatch][shv]") {
    test::chain_fixture fixture("rpc_shv_disabled");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](std::string const& body) {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run_for(std::chrono::seconds(2));
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        return result.get();
    };
    REQUIRE(request(R"({"method":"getshv","params":["root",0,0],"id":7})") ==
        node::rpc::build_error("7", -1, "MMR index is not enabled. Start with --node.mmr_index=true"));
    auto const info = request(R"({"method":"getblockchaininfo","params":[],"id":7})");
    REQUIRE(info.find(R"("result":{)") != std::string::npos);
    REQUIRE(info.find("shvroot") == std::string::npos);
}
