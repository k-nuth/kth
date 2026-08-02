// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <vector>

#include <kth/blockchain/validate/batch_validate.hpp>

using namespace kth;
using namespace kth::blockchain;

namespace {
// Per-batch transaction ordinals used to group each tx's inputs.
constexpr size_t tx1 = 0;
constexpr size_t tx2 = 1;
} // namespace

// =============================================================================
// enforce_sigcheck_limits — per-tx (max_tx_sigchecks) and per-block (ABLA) limits
// =============================================================================

TEST_CASE("enforce_sigcheck_limits: empty batch passes", "[sigchecks]") {
    CHECK(enforce_sigcheck_limits({}, {1000}, 3000) == error::success);
}

TEST_CASE("enforce_sigcheck_limits: within both limits passes", "[sigchecks]") {
    std::vector<sigcheck_entry> const e{
        {0, tx1, 100}, {0, tx1, 100},   // tx1 = 200
        {0, tx2, 300},                 // tx2 = 300  (block0 total = 500)
    };
    CHECK(enforce_sigcheck_limits(e, {1000}, 3000) == error::success);
}

TEST_CASE("enforce_sigcheck_limits: per-tx limit is the sum over a tx's inputs", "[sigchecks]") {
    // Two inputs of one tx summing exactly to the limit: allowed.
    CHECK(enforce_sigcheck_limits({{0, tx1, 1500}, {0, tx1, 1500}}, {1'000'000}, 3000)
        == error::success);
    // One more and the transaction is over the limit.
    CHECK(enforce_sigcheck_limits({{0, tx1, 1500}, {0, tx1, 1501}}, {1'000'000}, 3000)
        == error::transaction_sigchecks_limit);
}

TEST_CASE("enforce_sigcheck_limits: the per-tx counter resets between transactions", "[sigchecks]") {
    // Two separate txs each at the limit are fine — they are not summed together.
    CHECK(enforce_sigcheck_limits({{0, tx1, 3000}, {0, tx2, 3000}}, {1'000'000}, 3000)
        == error::success);
}

TEST_CASE("enforce_sigcheck_limits: per-block limit is the sum over a block's txs", "[sigchecks]") {
    // block limit 500; two txs summing exactly to 500: allowed.
    CHECK(enforce_sigcheck_limits({{0, tx1, 200}, {0, tx2, 300}}, {500}, 3000)
        == error::success);
    // 501 over the block limit.
    CHECK(enforce_sigcheck_limits({{0, tx1, 200}, {0, tx2, 301}}, {500}, 3000)
        == error::block_sigchecks_limit);
}

TEST_CASE("enforce_sigcheck_limits: each block has its own independent limit", "[sigchecks]") {
    std::vector<sigcheck_entry> const ok{
        {0, tx1, 500},   // block0 = 500 (limit 500)
        {1, tx2, 700},   // block1 = 700 (limit 700)
    };
    CHECK(enforce_sigcheck_limits(ok, {500, 700}, 3000) == error::success);

    std::vector<sigcheck_entry> const over{
        {0, tx1, 500},
        {1, tx2, 701},   // block1 over its own limit
    };
    CHECK(enforce_sigcheck_limits(over, {500, 700}, 3000) == error::block_sigchecks_limit);
}

TEST_CASE("enforce_sigcheck_limits: tx limit is checked before block limit", "[sigchecks]") {
    // A single tx over the per-tx limit fails with the tx error even though the
    // block limit is generous.
    CHECK(enforce_sigcheck_limits({{0, tx1, 3001}}, {1'000'000}, 3000)
        == error::transaction_sigchecks_limit);
}
