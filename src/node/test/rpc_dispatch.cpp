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
    CHECK(verbose.find(quoted(parent->hash()) + R"(:{"fees":{"base":0.00000300,"modified":0.00000300},)") !=
        std::string::npos);
    CHECK(verbose.find(R"("depends":[],"spentby":[)" + quoted(child->hash()) + "]") != std::string::npos);
    CHECK(verbose.find(quoted(child->hash()) + R"(:{"fees":{"base":0.00000200,"modified":0.00000200},)") !=
        std::string::npos);
    CHECK(verbose.find(R"("depends":[)" + quoted(parent->hash()) + R"(],"spentby":[])") != std::string::npos);

    // Not verbose: the txids, as before.
    CHECK(request("[]") == request("[false]"));
    CHECK(request("[]").find(R"("result":[)") != std::string::npos);
    CHECK(request(R"(["yes"])") == node::rpc::build_error("14", -32602, "verbose must be a boolean"));
}
