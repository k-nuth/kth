// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The size-to-SigChecks relation of Bitcoin Cash Node's test/functional/feature_block_sigchecks.py (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b): the number of SigChecks a block may carry is the block size limit in
// force divided by BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO (141), rounded down, so a limit one byte below a multiple of
// 141 allows one SigCheck less ("just below a multiple, to test edge case").
// Original: Copyright (c) 2020-present The Bitcoin Developers, MIT license.
//
// Only that arithmetic is here, on chain_state::dynamic_max_block_sigchecks and the ABLA limit it follows. The
// enforcement of the limit over a batch of blocks is blockchain/test/batch_sigchecks*.cpp (existing), and the scripts
// that produce SigChecks are VM and signatures.

#include <test_helpers.hpp>

#if defined(KTH_CURRENCY_BCH)

#include <kth/domain/chain/chain_state.hpp>

using namespace kth;
namespace chain = kth::domain::chain;
namespace abla = kth::domain::chain::abla;

namespace {

chain::chain_state make_state(uint64_t abla_limit) {
    // regtest: Upgrade 10 (ABLA) is active at every height
    chain::chain_state::data values{};
    values.height = 1000;
    values.hash = null_hash;
    values.allow_collisions_hash = null_hash;
    for (size_t i = 0; i < 147; ++i) {
        values.bits.ordered.push_back(0x207fffff);
        values.version.ordered.push_back(4);
        values.timestamp.ordered.push_back(1'700'000'000 + uint32_t(i));
    }
    values.bits.self = 0x207fffff;
    values.version.self = 4;
    values.timestamp.self = 1'700'000'200;
    values.timestamp.retarget = 1'700'000'000;
    values.abla_state.block_size = 0;
    values.abla_state.control_block_size = abla_limit / 2;
    values.abla_state.elastic_buffer_size = abla_limit - abla_limit / 2;
    return chain::chain_state(std::move(values), domain::script_flags_t(0), {}, domain::config::network::regtest,
        chain::chain_state::assert_anchor_block_info_t{}, 3600, abla::default_config(32'000'000, false), bch_cantor_activation_time);
}

} // namespace

TEST_CASE("BCHN feature_block_sigchecks - sigchecks limit is the size limit over 141, rounded down", "[bchn_non_vm][sigchecks]") {
    REQUIRE(block_maxbytes_maxsigchecks_ratio == 141u);
    for (uint64_t multiple : {uint64_t(300'000), uint64_t(400'000), uint64_t(1'000'000)}) {
        uint64_t const limit = multiple * 141;
        DYNAMIC_SECTION("size limit around " << limit) {
            // the static limit (32 MB) is the floor of the dynamic one: stay above it
            REQUIRE(limit > 32'000'000);
            CHECK(make_state(limit - 1).dynamic_max_block_size() == limit - 1);
            CHECK(make_state(limit - 1).dynamic_max_block_sigchecks() == multiple - 1);   // just below a multiple
            CHECK(make_state(limit).dynamic_max_block_sigchecks() == multiple);           // one byte more: one SigCheck more
            CHECK(make_state(limit + 140).dynamic_max_block_sigchecks() == multiple);
        }
    }
}

#endif // defined(KTH_CURRENCY_BCH)
