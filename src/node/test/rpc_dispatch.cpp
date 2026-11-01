// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/rpc/query.hpp>
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

TEST_CASE("getblock and getrawtransaction dispatch read the block files and the mempool", "[rpc dispatch]") {
    test::chain_fixture fixture("rpc_block_file_reads");
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

    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](std::string const& method, std::string const& params) {
        auto const body = R"({"method":")" + method + R"(","params":)" + params + R"(,"id":12})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run();
        return result.get();
    };
    auto const quoted = [](hash_digest const& hash) { return '"' + encode_hash(hash) + '"'; };
    auto const hex = [](std::string const& text) { return '"' + text + '"'; };

    // Block 3, as stored, by hash.
    REQUIRE(request("getblock", "[" + quoted(trunk[2].hash()) + ",false]") ==
        node::rpc::build_success("12", hex(node::rpc::block_to_hex(trunk[2]))));
    REQUIRE(request("getblock", "[" + quoted(null_hash) + "]") ==
        node::rpc::build_error("12", -5, "Block not found"));

    // A confirmed transaction: with its block hash, and not without one.
    auto const& coinbase = trunk[2].transactions().front();
    REQUIRE(request("getrawtransaction", "[" + quoted(coinbase.hash()) + ",false," + quoted(trunk[2].hash()) + "]") ==
        node::rpc::build_success("12", hex(node::rpc::transaction_to_hex(coinbase))));
    REQUIRE(request("getrawtransaction", "[" + quoted(coinbase.hash()) + ",false," + quoted(trunk[1].hash()) + "]") ==
        node::rpc::build_error("12", -5, "No such transaction found in the provided block"));
    REQUIRE(request("getrawtransaction", "[" + quoted(coinbase.hash()) + "]") ==
        node::rpc::build_error("12", -5, "No such mempool transaction; pass the block hash for a confirmed one"));

    // A mempool transaction, by txid alone.
    domain::chain::input::list ins;
    ins.emplace_back(domain::chain::output_point{coinbase.hash(), 0}, domain::chain::script{}, 0xffffffffu);
    domain::chain::output::list outs;
    outs.emplace_back(1000u, test::miner_script(), domain::chain::token_data_opt{});
    auto const pooled = std::make_shared<domain::message::transaction>(
        domain::chain::transaction{2u, 0u, std::move(ins), std::move(outs)});
    REQUIRE(fixture.chain().mempool_ref().add(blockchain::mempool_entry{
        pooled, 1000u, uint32_t(pooled->serialized_size(true)), 1u, 0u}));
    REQUIRE(request("getrawtransaction", "[" + quoted(pooled->hash()) + "]") ==
        node::rpc::build_success("12", hex(node::rpc::transaction_to_hex(*pooled))));
}
