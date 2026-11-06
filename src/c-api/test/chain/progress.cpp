// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#if defined(KTH_CURRENCY_BCH)
#include <chrono>
#include <future>
#include <utility>
#include <vector>

#include <kth/capi/chain/chain_query.h>
#include <kth/capi/conversions.hpp>
#include "../../../node/test/sync_harness.hpp"

namespace {
using progress_result = std::pair<kth_error_code_t, kth_block_progress_t>;
struct progress_callback {
    std::promise<progress_result> result;
};
void receive_progress(kth_chain_t, void* context, kth_error_code_t error, kth_block_progress_t value) {
    static_cast<progress_callback*>(context)->result.set_value({error, value});
}
}

TEST_CASE("C-API progress owns coherent tick metadata through sync and async calls", "[C-API chain][ticks]") {
    using namespace kth;
    // Contexts outlive the chain, including on assertion failure: closing the
    // fixture joins its executor before these borrowed callback inputs die.
    progress_callback tip_callback, unknown_callback;
    test::chain_fixture fixture("capi_tick_progress", /*notify_limit_hours*/ 0);
    constexpr uint32_t activation = 12;
    constexpr uint32_t base = 1700000000;
    fixture.settings().cantor_activation_time = base + (activation - 5) * 600;
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    kth_chain_t chain = &fixture.chain();
    kth_block_progress_t genesis{};
    REQUIRE(kth_chain_sync_chain_progress(chain, &genesis) == kth_ec_success);
    CHECK(genesis.height == 0);
    CHECK(genesis.ticks == 0);
    CHECK(genesis.target_time == 600);
    CHECK(genesis.next_target_time == 600);
    CHECK(genesis.confirmations == 1);
    CHECK(genesis.confirmations_ticks == 600);
    auto const genesis_hash = domain::chain::block::genesis_regtest().hash();
    CHECK(hash_to_cpp(genesis.block_hash.hash) == genesis_hash);

    std::vector<domain::chain::block> trunk;
    auto previous = genesis_hash;
    for (uint32_t height = 1; height <= 15; ++height) {
        auto const timestamp = height > activation
            ? base + activation * 600 + (height - activation) * 60
            : base + height * 600;
        auto const subsidy = uint64_t(height > activation ? 5 : 50) * 100000000;
        domain::chain::transaction::list txs{test::coinbase_tx(height, 0, subsidy)};
        domain::chain::block candidate(domain::chain::header{0x20000000, previous, null_hash,
            timestamp, test::regtest_bits, 0}, txs);
        auto const merkle = candidate.generate_merkle_root();
        bool solved = false;
        for (uint64_t nonce = 0; nonce <= max_uint32; ++nonce) {
            domain::chain::header header{0x20000000, previous, merkle, timestamp,
                test::regtest_bits, uint32_t(nonce)};
            if (header.is_valid_proof_of_work(domain::chain::hash(header), false)) {
                trunk.emplace_back(std::move(header), txs);
                solved = true;
                break;
            }
        }
        REQUIRE(solved);
        previous = trunk.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
    test::persist_headers(fixture, trunk, 1);
    test::connect_bodies(fixture, trunk, 1);
    kth_block_progress_t boundary{};
    auto const hash = to_hash_t(trunk[11].hash());
    REQUIRE(kth_chain_sync_block_progress(chain, &hash, &boundary) == kth_ec_success);
    CHECK(boundary.height == 12);
    CHECK(boundary.ticks == 7200);
    CHECK(boundary.target_time == 600);
    CHECK(boundary.next_target_time == 60);
    CHECK(boundary.confirmations == 4);
    CHECK(boundary.confirmations_ticks == 780);
    CHECK(hash_to_cpp(boundary.block_hash.hash) == trunk[11].hash());

    auto tip_result = tip_callback.result.get_future();
    kth_chain_async_chain_progress(chain, &tip_callback, receive_progress);
    REQUIRE(tip_result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    auto const [tip_error, tip] = tip_result.get();
    REQUIRE(tip_error == kth_ec_success);
    CHECK(tip.height == 15);
    CHECK(tip.ticks == 7380);
    CHECK(tip.target_time == 60);
    CHECK(tip.next_target_time == 60);
    CHECK(tip.confirmations == 1);
    CHECK(tip.confirmations_ticks == 60);
    CHECK(hash_to_cpp(tip.block_hash.hash) == trunk.back().hash());

    kth_hash_t const unknown{};
    kth_block_progress_t untouched = boundary;
    CHECK(kth_chain_sync_block_progress(chain, &unknown, &untouched) != kth_ec_success);
    CHECK(untouched.ticks == boundary.ticks);
    CHECK(untouched.height == boundary.height);
    CHECK(untouched.confirmations_ticks == boundary.confirmations_ticks);
    CHECK(untouched.confirmations == boundary.confirmations);
    CHECK(untouched.target_time == boundary.target_time);
    CHECK(untouched.next_target_time == boundary.next_target_time);
    CHECK(hash_to_cpp(untouched.block_hash.hash) == trunk[11].hash());
    auto unknown_result = unknown_callback.result.get_future();
    // The async signature preserves the existing (self, ctx, args, callback) order.
    kth_chain_async_block_progress(chain, &unknown_callback, &unknown, receive_progress);
    REQUIRE(unknown_result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    auto const [unknown_error, ignored] = unknown_result.get();
    CHECK(unknown_error != kth_ec_success);
}
#endif
