// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/dispatch.hpp>
#include "../../blockchain/test/reorg_chain_fixture.hpp"

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

TEST_CASE("getblocktemplate dispatch serves the template with its transactions", "[rpc dispatch]") {
    // A limit of zero keeps the genesis-only chain synchronized, so the node
    // serves mining work.
    test::chain_fixture fixture("rpc_block_template", /*notify_limit_hours*/ 0);
    REQUIRE(fixture.created());
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
}
