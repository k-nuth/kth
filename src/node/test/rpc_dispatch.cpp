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
    CHECK(reply.find(R"("confirmations":4,"value":50.00000000,)") != std::string::npos);
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
    CHECK(pooled.find(R"("confirmations":0,"value":49.99990000,)") != std::string::npos);
    CHECK(pooled.find(R"("coinbase":false})") != std::string::npos);
    CHECK(request("[" + quoted(spend->hash()) + ",0,false]") == none);

    CHECK(request("[" + quoted(coinbase) + "]") ==
        node::rpc::build_error("7", -32602, "gettxout requires [txid, n]"));
    CHECK(request("[" + quoted(coinbase) + R"(,0,"yes"])") ==
        node::rpc::build_error("7", -32602, "include_mempool must be a boolean"));
}
