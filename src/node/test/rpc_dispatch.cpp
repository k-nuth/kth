// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

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
    std::uint64_t number = 0;
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
    std::ostringstream work;
    work << std::hex << std::setfill('0') << std::setw(64)
         << domain::chain::header::proof(genesis.header().bits());
    REQUIRE(text == work.str());
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
