// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// GetBlock/GetChainInfo against a REAL block_chain (header index, flat-file block
// store, UTXO-Z) holding real regtest blocks, through the production adapter
// make_chain_reader(block_chain&). Nothing about the chain is faked.

#include <test_helpers.hpp>

#include "sync_harness.hpp"
#include "grpc_test_support.hpp"

#include <kth/node/grpc_rpc/chain_handlers.hpp>

using namespace kth;
using namespace kth::test;
using namespace kth::node::grpc_rpc;
using kth::node::test::client;
namespace v1 = ::kth::rpc::v1;

namespace {

std::string bytes_of(hash_digest const& hash) {
    return std::string(reinterpret_cast<char const*>(hash.data()), hash.size());
}

std::string wire_of(domain::chain::block const& block) {
    auto const wire = to_data_chunk(block);
    return std::string(reinterpret_cast<char const*>(wire.data()), wire.size());
}

} // namespace

TEST_CASE("grpc GetBlock serves real connected blocks and refuses everything else", "[grpc real]") {
    test::chain_fixture fixture("grpc_real");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    constexpr uint32_t trunk_len = 3;
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    std::vector<domain::chain::block> trunk;
    auto prev = genesis.hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
        prev = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
    persist_headers(fixture, trunk, 1);
    connect_bodies(fixture, trunk, 1);

    // A header whose body never arrived, and a competing block at height 3 that is
    // only a side branch (same parent as the trunk's third block).
    auto const header_only = test::mine_block(trunk.back().hash(), 4, base_time + 4 * block_spacing, 0, {}, 0);
    auto const side = test::mine_block(trunk[1].hash(), 3, base_time + 3 * block_spacing, 9, {}, 0);
    REQUIRE(side.hash() != trunk[2].hash());
    REQUIRE(fixture.organizer().add_headers(headers_of({header_only})).headers_added == 1);
    persist_headers(fixture, {header_only}, 4);
    (void)fixture.organizer().add_headers(headers_of({side}));

    transport_options options;
    options.port = 0;
    options.user = "user";
    options.password = "secret";
    options.shutdown_grace = std::chrono::milliseconds(200);
    transport service(options, make_chain_handlers(make_chain_reader(chain),
        v1::NETWORK_REGTEST, chain.executor()));
    auto const port = service.start();
    REQUIRE(port.has_value());
    client c(*port);

    auto const info = c.chain_info();
    REQUIRE(info.status.ok());
    CHECK(info.response.network() == v1::NETWORK_REGTEST);
    CHECK(info.response.block_height() == trunk_len);
    CHECK(info.response.header_height() == 4);
    CHECK(info.response.best_block_hash() == bytes_of(trunk.back().hash()));

    // Every connected block comes back byte for byte as the native wire block.
    for (uint32_t i = 0; i < trunk_len; ++i) {
        auto const reply = c.block(bytes_of(trunk[i].hash()));
        INFO("height " << i + 1);
        REQUIRE(reply.status.ok());
        CHECK(reply.response.height() == i + 1);
        CHECK(reply.response.raw_block() == wire_of(trunk[i]));
    }
    auto const at_genesis = c.block(bytes_of(genesis.hash()));
    if (at_genesis.status.ok()) {   // whether genesis has a stored body is the store's business
        CHECK(at_genesis.response.raw_block() == wire_of(genesis));
    } else {
        CHECK(at_genesis.status.error_code() == grpc::StatusCode::NOT_FOUND);
    }

    // Not connected / not on the active chain / unknown: NOT_FOUND, never wrong bytes.
    CHECK(c.block(bytes_of(header_only.hash())).status.error_code() == grpc::StatusCode::NOT_FOUND);
    CHECK(c.block(bytes_of(side.hash())).status.error_code() == grpc::StatusCode::NOT_FOUND);
    CHECK(c.block(bytes_of(null_hash)).status.error_code() == grpc::StatusCode::NOT_FOUND);

    service.stop();
    service.join();
}
