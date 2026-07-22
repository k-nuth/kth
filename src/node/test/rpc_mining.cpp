// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <memory>
#include <vector>

#include <kth/domain.hpp>
#include <kth/node/rpc/mining.hpp>

using namespace kth;
using namespace kth::node::rpc;

namespace {

transaction_const_ptr make_tx(uint32_t locktime) {
    return std::make_shared<domain::message::transaction>(
        domain::chain::transaction{1u, locktime, {}, {}});
}

} // namespace

// Start Test Suite: rpc mining tests

TEST_CASE("render_mining_template serializes the GBT-light fields", "[rpc mining]") {
    blockchain::mining_template tmpl{
        /*version*/ 0x20000000u,
        /*previous_block_hash*/ null_hash,
        /*height*/ 5u,
        /*bits*/ 0x1d00ffffu,
        /*min_time*/ 1000u,
        /*current_time*/ 2000u,
        /*coinbase_value*/ 5000000000ULL,
        /*size_limit*/ 32000000u,
        /*sigchecks_limit*/ 226950u,
        /*selection*/ blockchain::block_template{}};

    REQUIRE(render_mining_template(tmpl, "testjob") ==
        R"({"version":536870912,)"
        R"("previousblockhash":"0000000000000000000000000000000000000000000000000000000000000000",)"
        R"("height":5,"coinbasevalue":5000000000,)"
        R"("target":"00000000ffff0000000000000000000000000000000000000000000000000000",)"
        R"("bits":"1d00ffff","mintime":1000,"curtime":2000,)"
        R"("sizelimit":32000000,"sigchecklimit":226950,)"
        R"("noncerange":"00000000ffffffff",)"
        R"("mutable":["time","transactions","prevblock"],)"
        R"("job_id":"testjob"})");
}

TEST_CASE("assemble_block puts the coinbase first then the job selection", "[rpc mining]") {
    domain::chain::transaction coinbase{1u, 0u, {}, {}};
    std::vector<transaction_const_ptr> job{make_tx(11), make_tx(22)};

    auto const block = assemble_block(domain::chain::header{}, coinbase, job);

    REQUIRE(block.transactions().size() == 3u);
    REQUIRE(block.transactions()[0].hash() == coinbase.hash());
    REQUIRE(block.transactions()[1].hash() == job[0]->hash());
    REQUIRE(block.transactions()[2].hash() == job[1]->hash());
}

TEST_CASE("assemble_block with an empty selection is coinbase-only", "[rpc mining]") {
    domain::chain::transaction coinbase{1u, 7u, {}, {}};
    auto const block = assemble_block(domain::chain::header{}, coinbase, {});
    REQUIRE(block.transactions().size() == 1u);
    REQUIRE(block.transactions()[0].hash() == coinbase.hash());
}

TEST_CASE("render_mining_info serializes the getmininginfo fields", "[rpc mining]") {
    blockchain::mining_info info{
        /*blocks*/ 42u,
        /*difficulty*/ 1.0,
        /*pooled_tx*/ 3u,
        /*chain*/ domain::config::network::mainnet};

    REQUIRE(render_mining_info(info) ==
        R"({"blocks":42,"difficulty":1.0,"pooledtx":3,"chain":"Mainnet","warnings":""})");
}
