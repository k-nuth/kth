// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Aggregate per-block SigChecks boundary of Bitcoin Cash Node test/functional/feature_block_sigchecks.py
// (commit 07576013c91ff4a3a74acd85f189c69121cdad1b), through blockchain::enforce_sigcheck_limits, the function
// validate_block_batch applies to every block of a batch.
// Original: Copyright (c) 2019-present The Bitcoin developers, MIT license.
//
// The original forces a block size limit of 8000 * 141 - 1 bytes, so the limit is 7999 SigChecks: a block of 10000
// SigChecks (50 transactions of 200) is rejected, a block of 8000 is rejected, and after raising the forced size by one
// byte, which makes the limit 8000, the 8000-SigCheck block is accepted. The derivation of the limit from the size
// (bchn_block_sigchecks_limit.cpp) is tested in the domain target; the limits are given here as the original computes them.
// Not ported: P2P submission, the bad-block and signature-cache log messages, and mempool/miner behavior.

#include <test_helpers.hpp>

#include <vector>

#include <kth/blockchain/validate/batch_validate.hpp>

using namespace kth;
using namespace kth::blockchain;

namespace {

constexpr uint64_t forced_size = 8000u * 141u - 1u;
constexpr uint64_t limit_at_forced_size = forced_size / 141u;              // 7999
constexpr uint64_t limit_one_byte_more = (forced_size + 1u) / 141u;        // 8000
constexpr uint64_t tx_limit = 3000;

// `txs` transactions of `per_tx` SigChecks in block 0
std::vector<sigcheck_entry> block_of(size_t txs, uint64_t per_tx) {
    std::vector<sigcheck_entry> entries;
    for (size_t t = 0; t < txs; ++t) entries.push_back({0, t, per_tx});
    return entries;
}

} // namespace

TEST_CASE("BCHN feature_block_sigchecks - the aggregate limit at the forced size", "[bchn_non_vm][sigchecks]") {
    REQUIRE(limit_at_forced_size == 7999u);
    REQUIRE(limit_one_byte_more == 8000u);

    // 10000 SigChecks, 50 transactions of 200
    CHECK(enforce_sigcheck_limits(block_of(50, 200), {limit_at_forced_size}, tx_limit) == error::block_sigchecks_limit);
    // 8000 SigChecks, 40 transactions of 200: one over the limit
    CHECK(enforce_sigcheck_limits(block_of(40, 200), {limit_at_forced_size}, tx_limit) == error::block_sigchecks_limit);
    // the same transactions with the limit one byte of size higher
    CHECK(enforce_sigcheck_limits(block_of(40, 200), {limit_one_byte_more}, tx_limit) == error::success);
    // control: the larger block stays rejected with the larger limit
    CHECK(enforce_sigcheck_limits(block_of(50, 200), {limit_one_byte_more}, tx_limit) == error::block_sigchecks_limit);
    // control: one SigCheck less than the limit passes at the forced size
    auto fewer = block_of(39, 200);
    fewer.push_back({0, 39, 199});
    CHECK(enforce_sigcheck_limits(fewer, {limit_at_forced_size}, tx_limit) == error::success);
}
