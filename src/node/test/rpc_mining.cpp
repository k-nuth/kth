// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/domain.hpp>
#include <kth/node/rpc/mining.hpp>

using namespace kth;
using namespace kth::node::rpc;

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

TEST_CASE("render_mining_info serializes the getmininginfo fields", "[rpc mining]") {
    // Designated rather than positional: the three flags are all `bool`, so a
    // reorder of the struct would keep compiling and silently re-label them.
    blockchain::mining_info info{
        .blocks = 42u,
        .difficulty = 1.0,
        .pooled_tx = 3u,
        .chain = domain::config::network::mainnet,
        .transition_in_progress = false,
        .caught_up = true,
        .fresh = true};

    REQUIRE(render_mining_info(info) ==
        R"({"blocks":42,"difficulty":1.0,"pooledtx":3,"chain":"Mainnet",)"
        R"("transitioninprogress":false,"caughtup":true,"fresh":true,"warnings":""})");
}

TEST_CASE("render_mining_info reports the three refusal reasons apart", "[rpc mining]") {
    // Why mining work is being refused is what an operator reads here, and the
    // three have different remedies: a transition clears in milliseconds, a node
    // that is behind is downloading, and a node that is caught up but stale has
    // a connectivity or a clock problem. One boolean could not tell them apart.
    blockchain::mining_info info{
        .blocks = 7u,
        .difficulty = 2.5,
        .pooled_tx = 0u,
        .chain = domain::config::network::regtest,
        .transition_in_progress = true,
        .caught_up = false,
        .fresh = false};

    REQUIRE(render_mining_info(info) ==
        R"({"blocks":7,"difficulty":2.5,"pooledtx":0,"chain":"Regtest",)"
        R"("transitioninprogress":true,"caughtup":false,"fresh":false,"warnings":""})");
}

TEST_CASE("render_mining_info reports a caught-up but stale tip", "[rpc mining]") {
    // The two operational flags are independent, and the cases above do not show
    // it: both hold them equal, so a renderer that emitted `caught_up` for the
    // `fresh` key would pass either one. This is the node that reached its
    // headers and is sitting on a tip older than the configured age.
    blockchain::mining_info info{
        .blocks = 9u,
        .difficulty = 1.0,
        .pooled_tx = 1u,
        .chain = domain::config::network::regtest,
        .transition_in_progress = false,
        .caught_up = true,
        .fresh = false};

    REQUIRE(render_mining_info(info) ==
        R"({"blocks":9,"difficulty":1.0,"pooledtx":1,"chain":"Regtest",)"
        R"("transitioninprogress":false,"caughtup":true,"fresh":false,"warnings":""})");
}
