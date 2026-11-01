// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// BIP68 relative lock-times as Bitcoin Cash Node enforces them (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b): consensus/tx_verify.cpp CalculateSequenceLocks and
// EvaluateSequenceLocks, scenarios of test/functional/feature_bip68_sequence.py (disable flag,
// height locks, time locks, version gate), expressed on transaction::is_locked and on
// transaction::accept with the prevout populated the way the validator populates it.
// Original: Copyright (c) 2014-present The Bitcoin Core / Bitcoin developers, MIT license.
//
// BCHN: unlocked iff  prev_height + value - 1 < block_height          (height locks)
//                     coin_mtp + (value << 9) - 1 < prev_block_mtp    (time locks), where
//       coin_mtp is the median time past of the block BEFORE the one that holds the coin.
// Knuth takes `prevout.validation.height` and `prevout.validation.median_time_past`.

#if defined(KTH_CURRENCY_BCH)

#include <test_helpers.hpp>

#include <kth/domain/constants.hpp>

using namespace kth;
namespace chain = kth::domain::chain;

namespace {

constexpr uint32_t disable_flag = 1u << 31;
constexpr uint32_t type_flag = 1u << 22;
constexpr uint32_t coin_height = 1000;
constexpr uint32_t coin_mtp = 1'600'000'000;

chain::transaction spend_with_sequence(uint32_t sequence, uint32_t version = 2) {
    hash_digest h{};
    h.fill(0x61);
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{h, 1}, chain::script(data_chunk(3, 0x51), false), sequence);
    chain::output::list outputs;
    outputs.emplace_back(1000, chain::script(data_chunk(3, 0x51), false), chain::token_data_opt{});
    chain::transaction tx(version, 0, std::move(inputs), std::move(outputs));
    tx.inputs()[0].previous_output().validation.height = coin_height;
    tx.inputs()[0].previous_output().validation.median_time_past = coin_mtp;
    return tx;
}

} // namespace

// Height locks: a lock of N blocks is satisfied by the block at coin_height + N
TEST_CASE("BCHN feature_bip68_sequence - height lock boundaries", "[bchn_non_vm][sequence_lock]") {
    for (uint32_t value : {1u, 2u, 10u, 0xffffu}) {
        DYNAMIC_SECTION("lock of " << value << " blocks") {
            auto const tx = spend_with_sequence(value);
            CHECK(tx.is_locked(coin_height + value - 1, coin_mtp));       // one block early
            CHECK( ! tx.is_locked(coin_height + value, coin_mtp));        // exactly on time
            CHECK( ! tx.is_locked(coin_height + value + 1, coin_mtp));
        }
    }
}

// Sequence 0 is a lock of zero blocks: never locked
TEST_CASE("BCHN feature_bip68_sequence - zero height lock", "[bchn_non_vm][sequence_lock]") {
    CHECK( ! spend_with_sequence(0).is_locked(coin_height, coin_mtp));
}

// Time locks count 512-second units of median-time-past age
TEST_CASE("BCHN feature_bip68_sequence - time lock boundaries", "[bchn_non_vm][sequence_lock]") {
    for (uint32_t value : {1u, 2u, 100u, 0xffffu}) {
        DYNAMIC_SECTION("lock of " << value << " units of 512 seconds") {
            auto const tx = spend_with_sequence(type_flag | value);
            uint32_t const seconds = value << 9;
            // the height does not matter for a time lock
            CHECK(tx.is_locked(coin_height + 100000, coin_mtp + seconds - 1));
            CHECK( ! tx.is_locked(coin_height, coin_mtp + seconds));
            CHECK( ! tx.is_locked(coin_height, coin_mtp + seconds + 1));
        }
    }
}

// The disable flag switches the relative lock off, whatever the rest of the sequence says
TEST_CASE("BCHN feature_bip68_sequence - disable flag", "[bchn_non_vm][sequence_lock]") {
    CHECK( ! spend_with_sequence(disable_flag | 0xffffu).is_locked(coin_height, coin_mtp));
    CHECK( ! spend_with_sequence(disable_flag | type_flag | 0xffffu).is_locked(coin_height, coin_mtp));
    CHECK( ! spend_with_sequence(0xffffffffu).is_locked(coin_height, coin_mtp));
    CHECK(spend_with_sequence(0xffffu).is_locked(coin_height, coin_mtp));                // control: same lock without the flag
}

// Bits outside the 16-bit value and the type flag are ignored (BCHN masks with 0xffff)
TEST_CASE("BCHN feature_bip68_sequence - bits above the mask are ignored", "[bchn_non_vm][sequence_lock]") {
    CHECK(spend_with_sequence(0x00010000u | 5u).is_locked(coin_height + 4, coin_mtp));
    CHECK( ! spend_with_sequence(0x00010000u | 5u).is_locked(coin_height + 5, coin_mtp));
}

// BIP68 applies to version 2 and above only (BCHN compares the version as unsigned)
TEST_CASE("BCHN feature_bip68_sequence - transaction version gate", "[bchn_non_vm][sequence_lock]") {
    CHECK( ! spend_with_sequence(0xffffu, 1).is_locked(coin_height, coin_mtp));
    CHECK(spend_with_sequence(0xffffu, 2).is_locked(coin_height, coin_mtp));
    CHECK(spend_with_sequence(0xffffu, 0x7fffffffu).is_locked(coin_height, coin_mtp));
}

// Any locked input locks the transaction (BCHN takes the maximum of the per-input locks)
TEST_CASE("BCHN feature_bip68_sequence - one locked input locks the transaction", "[bchn_non_vm][sequence_lock]") {
    auto tx = spend_with_sequence(1);
    auto inputs = tx.inputs();
    inputs.emplace_back(chain::output_point{null_hash, 2}, chain::script(data_chunk(3, 0x51), false), 50);
    inputs.back().previous_output().validation.height = coin_height;
    inputs.back().previous_output().validation.median_time_past = coin_mtp;
    chain::transaction const two(2, 0, std::move(inputs), chain::output::list(tx.outputs()));
    CHECK(two.is_locked(coin_height + 10, coin_mtp));        // the 50-block lock is still pending
    CHECK( ! two.is_locked(coin_height + 50, coin_mtp));
}

// Through transaction::accept with BIP68 active, the rule reports sequence_locked
TEST_CASE("BCHN feature_bip68_sequence - accept reports sequence_locked only with the rule active", "[bchn_non_vm][sequence_lock]") {
    namespace upgrade = kth::domain::machine::upgrade;
    auto const with_csv = kth::domain::to_flags(upgrade::bch_pythagoras) | kth::domain::machine::script_flags::bip68_rule;
    auto const without_csv = kth::domain::to_flags(upgrade::bch_pythagoras);
    auto const tx = spend_with_sequence(500);
    tx.inputs()[0].previous_output().validation.cache = chain::output(10'000, chain::script(data_chunk(25, 0x51), false), chain::token_data_opt{});
    auto const run = [&](kth::domain::script_flags_t flags, size_t height) {
        return tx.accept(flags, height, coin_mtp, 1'000'000, false, false, false);
    };
    CHECK(run(with_csv, coin_height + 499) == error::sequence_locked);
    CHECK(run(with_csv, coin_height + 500) == error::success);
    CHECK(run(without_csv, coin_height + 499) == error::success);
}

#endif // KTH_CURRENCY_BCH
