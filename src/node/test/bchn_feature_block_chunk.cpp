// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// feature_block.py b49 of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b): a block whose header
// merkle root does not match its transactions (bad-txnmrklroot) is refused. In the Knuth node that check is made on
// every downloaded chunk by block_chain::validate_chunk, which the fast validation task calls before the chunk is stored
// and connected; the harness of reorg_cycle.cpp starts at storage, so the chunk call is made directly.
// Original: Copyright (c) 2015-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.

#include "bchn_reorg_scenario.hpp"

using namespace kth;
using namespace kth::test;
using kth::test::bchn_reorg::scenario;

namespace {

// The same transactions under a header with a flipped merkle root that still satisfies proof of work.
domain::chain::block with_bad_merkle(domain::chain::block const& good) {
    auto bad_merkle = good.header().merkle();
    bad_merkle[0] ^= 0x01;
    for (uint32_t nonce = 0; nonce < 1'000'000; ++nonce) {
        domain::chain::header header{good.header().version(), good.header().previous_block_hash(), bad_merkle,
                                     good.header().timestamp(), regtest_bits, nonce};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), false)) {
            return domain::chain::block(header, domain::chain::transaction::list(good.transactions()));
        }
    }
    throw std::runtime_error("no nonce satisfied the regtest target");
}

code validate_chunk(scenario& s, std::vector<domain::chain::block> const& blocks, uint32_t start_height) {
    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    for (auto const& b : blocks) light.push_back(to_light(b));
    code result = error::unknown;
    ::asio::io_context ctx;
    ::asio::co_spawn(ctx, s.fixture.chain().validate_chunk(light, start_height),
        [&result](std::exception_ptr, code ec) { result = ec; });
    ctx.run_for(std::chrono::seconds(30));
    return result;
}

} // namespace

TEST_CASE("BCHN feature_block b49 - a block with a bad merkle root is refused", "[bchn_non_vm][bchn_reorg]") {
    scenario s("bchn_fb_b49");
    auto const good = s.next(s.trunk.back().hash(), 106, 1, 1);
    auto const bad = with_bad_merkle(good);
    REQUIRE(bad.generate_merkle_root() != bad.header().merkle());
    CHECK( ! validate_chunk(s, {good}, 106));             // control: the same transactions, true root
    CHECK(validate_chunk(s, {bad}, 106) == error::merkle_mismatch);
}
