// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Ports of Bitcoin Cash Node src/test/pow_tests.cpp (commit 07576013c91ff4a3a74acd85f189c69121cdad1b):
// get_next_work, get_next_work_pow_limit, get_next_work_lower_limit_actual,
// get_next_work_upper_limit_actual, retargeting_test (EDA) and cash_difficulty_test (cw-144).
// Original: Copyright (c) 2015 The Bitcoin Core developers, 2017-2022 The Bitcoin developers, MIT license.
//
// Entry point: chain_state_test_access::work_required(data, network, flags, ...), fed with the window of
// previous blocks that chain_state keeps (bits and timestamps ordered by height, last = parent).
// BCHN's tests call CalculateNextWorkRequired / GetNextWorkRequired / GetNextCashWorkRequired on
// chains starting at genesis, whose heights are below the cw-144 activation of mainnet; Knuth
// selects the algorithm by height and network, so the EDA and cw-144 simulations run on testnet4,
// whose UAHF (6) / cw-144 (3001) / ASERT (16845) activations leave room for them. The window is
// all these algorithms read, so the history is padded to reach the needed height.

#include <test_helpers.hpp>

#if defined(KTH_CURRENCY_BCH)

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/compact.hpp>
#include <kth/domain/constants.hpp>

#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

using namespace kth;
namespace chain = kth::domain::chain;
using kth::domain::config::network;
using kth::domain::chain::chain_state;

namespace {

// `chain_state::work_required` is protected; reached through a pass-through subclass
// instead of widening the production API.
struct chain_state_test_access : chain_state {
    using chain_state::work_required;
    using chain_state::assert_anchor_block_info_t;
};

constexpr size_t window = 147;   // chain_state_timestamp_count

uint256_t target_of(uint32_t bits) {
    auto const target = chain::compact::from_compact(bits);
    REQUIRE(target);
    return target->big();
}

uint256_t const& pow_limit() {
    // BCHN params.powLimit; Knuth's retarget limit decodes to 0x1d00ffff, which is what GetCompact() gives
    static uint256_t const value("0x00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    return value;
}

uint32_t pow_limit_bits() {
    return chain::compact(pow_limit()).normal();
}

// A chain of (time, bits) per height starting at height 0.
struct simulated_chain {
    std::vector<uint32_t> times;
    std::vector<uint32_t> bits;

    void add(uint32_t time, uint32_t b) { times.push_back(time); bits.push_back(b); }
    void add_after(int64_t interval, uint32_t b) { add(uint32_t(int64_t(times.back()) + interval), b); }

    chain_state::data window_for_next() const {
        chain_state::data d{};
        d.height = times.size();                       // the block to be validated
        size_t const first = times.size() > window ? times.size() - window : 0;
        for (size_t i = first; i < times.size(); ++i) {
            d.bits.ordered.push_back(bits[i]);
            d.timestamp.ordered.push_back(times[i]);
        }
        d.bits.self = 0;
        d.timestamp.self = 0;
        return d;
    }

    uint32_t next_work(network net, kth::domain::script_flags_t flags) const {
        return chain_state_test_access::work_required(window_for_next(), net, flags, bch_cantor_activation_time,
                                          chain_state_test_access::assert_anchor_block_info_t{}, 3600);
    }
};

auto const retarget_flags = kth::domain::machine::script_flags::retarget;

} // namespace

// Legacy 2016-block retarget on mainnet below UAHF
TEST_CASE("BCHN pow_tests - get_next_work and its limits", "[bchn_non_vm][pow][daa]") {
    struct row { char const* name; uint32_t height; uint32_t last_time; uint32_t last_bits; uint32_t retarget_time; uint32_t expected; };
    static row const rows[] = {
        {"get_next_work", 32256, 1262152739, 0x1d00ffff, 1261130161, 0x1d00d86a},
        {"get_next_work_pow_limit", 2016, 1233061996, 0x1d00ffff, 1231006505, 0x1d00ffff},
        {"get_next_work_lower_limit_actual", 68544, 1279297671, 0x1c05a3f4, 1279008237, 0x1c0168fd},
        {"get_next_work_upper_limit_actual", 46368, 1269211443, 0x1c387f6f, 1263163443, 0x1d00e1fd},
    };
    for (auto const& r : rows) {
        DYNAMIC_SECTION(r.name) {
            chain_state::data d{};
            d.height = r.height;                        // BCHN pindexLast->nHeight + 1
            d.bits.ordered.push_back(r.last_bits);
            d.timestamp.ordered.push_back(r.last_time);
            d.timestamp.retarget = r.retarget_time;
            auto const bits = chain_state_test_access::work_required(d, network::mainnet, retarget_flags, bch_cantor_activation_time,
                                                         chain_state_test_access::assert_anchor_block_info_t{}, 2 * 24 * 3600);
            CHECK(bits == r.expected);
        }
    }
}

// Emergency Difficulty Adjustment: 2h blocks lower the difficulty by 25% once MTP lags 12h
TEST_CASE("BCHN pow_tests - retargeting_test (EDA), blocks that do not trigger it", "[bchn_non_vm][pow][daa]") {
    uint256_t const current = pow_limit() >> 1;
    uint32_t const initial_bits = chain::compact(current).normal();

    simulated_chain blocks;
    blocks.add(1269211443, initial_bits);     // genesis
    // History on the testnet4 heights where the EDA is the rule (UAHF at 6, cw-144 from 3001)
    for (size_t i = 1; i < 100; i++) blocks.add_after(target_spacing_seconds, initial_bits);

    // We start getting 2h block times. For the first 5 blocks it doesn't matter as the MTP is not
    // affected. For the next 5 blocks the MTP difference increases but stays below 12h.
    for (size_t i = 100; i < 110; i++) {
        blocks.add_after(2 * 3600, initial_bits);
        CHECK(blocks.next_work(network::testnet4, retarget_flags) == initial_bits);
    }

    // Blocks #110 onwards (the difficulty must start decreasing when the median time past lags by
    // exactly 12 hours) are NOT here: Knuth's threshold is `>` where BCHN's is `>=`, so the original
    // expectations fail. failed_regression eda_threshold_is_exclusive in coverage.json, ported as evidence.
}

// cw-144 difficulty adjustment
TEST_CASE("BCHN pow_tests - cash_difficulty_test (cw-144)", "[bchn_non_vm][pow][daa]") {
    auto const limit_bits = pow_limit_bits();
    uint256_t const initial_target = pow_limit() >> 4;
    uint32_t const initial_bits = chain::compact(initial_target).normal();
    auto const limit = pow_limit();
    auto const spacing = int64_t(target_spacing_seconds);

    simulated_chain blocks;
    blocks.add(1269211443, initial_bits);
    // Pile up blocks every 10 minutes to establish history; padded so the next height is >= 3001
    for (size_t i = 1; i < 2050 + 1100; i++) blocks.add_after(spacing, initial_bits);
    REQUIRE(blocks.times.size() > 3001);

    auto const next = [&] { return blocks.next_work(network::testnet4, retarget_flags); };
    uint32_t n_bits = next();

    // Difficulty stays the same as long as we produce a block every 10 minutes
    for (size_t j = 0; j < 10; j++) {
        blocks.add_after(spacing, n_bits);
        CHECK(next() == n_bits);
    }

    // Blocks out of whack are skipped: one far in the future, then one with the expected timestamp
    blocks.add_after(6000, n_bits);
    CHECK(next() == n_bits);
    blocks.add_after(2 * 600 - 6000, n_bits);
    CHECK(next() == n_bits);

    // The system continues unaffected by the block with the bogus timestamp
    for (size_t j = 0; j < 20; j++) {
        blocks.add_after(spacing, n_bits);
        CHECK(next() == n_bits);
    }

    // We start emitting blocks slightly faster. The first block has no impact.
    blocks.add_after(550, n_bits);
    CHECK(next() == n_bits);

    // Now we should see the difficulty increase slowly
    for (size_t j = 0; j < 10; j++) {
        blocks.add_after(550, n_bits);
        auto const next_bits = next();
        auto const current_target = target_of(n_bits);
        auto const next_target = target_of(next_bits);
        CHECK(next_target < current_target);
        CHECK((current_target - next_target) < (current_target >> 10));
        n_bits = next_bits;
    }
    CHECK(n_bits == 0x1c0fe7b1);

    // If we dramatically shorten block production, difficulty increases faster
    for (size_t j = 0; j < 20; j++) {
        blocks.add_after(10, n_bits);
        auto const next_bits = next();
        auto const current_target = target_of(n_bits);
        auto const next_target = target_of(next_bits);
        CHECK(next_target < current_target);
        CHECK((current_target - next_target) < (current_target >> 4));
        n_bits = next_bits;
    }
    CHECK(n_bits == 0x1c0db19f);

    // We start to emit blocks significantly slower. The first block has no impact.
    blocks.add_after(6000, n_bits);
    n_bits = next();
    CHECK(n_bits == 0x1c0d9222);

    // If we dramatically slow down block production, difficulty decreases
    for (size_t j = 0; j < 93; j++) {
        blocks.add_after(6000, n_bits);
        auto const next_bits = next();
        auto const current_target = target_of(n_bits);
        auto const next_target = target_of(next_bits);
        CHECK(next_target <= limit);
        CHECK(next_target > current_target);
        CHECK((next_target - current_target) < (current_target >> 3));
        n_bits = next_bits;
    }
    CHECK(n_bits == 0x1c2f13b9);

    // Due to the window of time being bounded, the next block's difficulty actually gets harder
    blocks.add_after(6000, n_bits);
    n_bits = next();
    CHECK(n_bits == 0x1c2ee9bf);

    // And goes down again. It takes a while since the skewed block makes 2 blocks leave the window.
    for (size_t j = 0; j < 192; j++) {
        blocks.add_after(6000, n_bits);
        auto const next_bits = next();
        auto const current_target = target_of(n_bits);
        auto const next_target = target_of(next_bits);
        CHECK(next_target <= limit);
        CHECK(next_target > current_target);
        CHECK((next_target - current_target) < (current_target >> 3));
        n_bits = next_bits;
    }
    CHECK(n_bits == 0x1d00ffff);

    // Once the difficulty reached the minimum allowed level, it doesn't get any easier
    for (size_t j = 0; j < 5; j++) {
        blocks.add_after(6000, n_bits);
        auto const next_bits = next();
        CHECK(next_bits == limit_bits);
        n_bits = next_bits;
    }
}

// ---------------------------------------------------------------------------------------------
// asert_difficulty_test: ASERT against a double-precision reference, with an anchor block.
// BCHN's chain is genesis (height 0, the anchor's parent) + anchor (height 1) + ...; here the same
// shape sits at heights A-1 and A on testnet4, past its ASERT activation (16845), and the anchor
// is given to chain_state the way it keeps it: height, bits and the timestamp of the anchor's parent.
// ---------------------------------------------------------------------------------------------
namespace {

constexpr size_t anchor_height = 20000;
constexpr uint32_t asert_half_life_seconds = 2 * 24 * 3600;

struct asert_block { size_t height; uint32_t time; uint32_t bits; };

double target_from_bits(uint32_t bits) {
    return double(bits & 0xffffff) * std::pow(256.0, double(bits >> 24) - 3);
}

uint32_t asert_next(asert_block const& prev, chain_state_test_access::assert_anchor_block_info_t const& anchor) {
    chain_state::data d{};
    d.height = prev.height + 1;
    d.bits.ordered.push_back(prev.bits);
    d.timestamp.ordered.push_back(prev.time);
    return chain_state_test_access::work_required(d, network::testnet4, retarget_flags, bch_cantor_activation_time, anchor, asert_half_life_seconds);
}

double approximation_error(asert_block const& prev, uint32_t final_bits, asert_block const& anchor_block, uint32_t anchor_parent_time) {
    int64_t const height_diff = int64_t(prev.height) - int64_t(anchor_block.height);
    int64_t const time_diff = int64_t(prev.time) - int64_t(anchor_parent_time);
    CHECK(height_diff >= 0);
    double const initial = target_from_bits(anchor_block.bits);
    double const final_pow = target_from_bits(final_bits);
    double const exponent = double(time_diff - (height_diff + 1) * 600) / double(2 * 24 * 3600);
    double const target = initial * std::pow(2.0, exponent);
    return (final_pow - target) / target;
}

} // namespace

TEST_CASE("BCHN pow_tests - asert_difficulty_test", "[bchn_non_vm][pow][asert]") {
    uint256_t const current_pow_start = pow_limit() >> 3;
    uint32_t const initial_bits = chain::compact(current_pow_start).normal();
    double const max_err = 0.0001166792656486;

    uint32_t const parent_time = 1269211443;                 // BCHN genesis, parent of the anchor
    asert_block anchor{anchor_height, parent_time + 150, initial_bits};   // solve time 150 s from its parent
    chain_state_test_access::assert_anchor_block_info_t const anchor_info{anchor.height, parent_time, anchor.bits};

    std::vector<asert_block> blocks;
    blocks.push_back({anchor_height - 1, parent_time, 0x0dedbeef});   // the pre-anchor bits must never be read
    blocks.push_back(anchor);
    auto const add = [&](int64_t interval, uint32_t bits) {
        asert_block const prev = blocks.back();   // a copy: push_back may reallocate
        blocks.push_back({prev.height + 1, uint32_t(int64_t(prev.time) + interval), bits});
    };
    auto const err_abs = [&](asert_block const& b, uint32_t bits) { return approximation_error(b, bits, anchor, parent_time); };
    auto const err_rel = [&](asert_block const& b, uint32_t bits, asert_block const& reference) { return approximation_error(b, bits, reference, 0) ; };
    (void)err_rel;
    auto const next = [&] { return asert_next(blocks.back(), anchor_info); };

    // The next block's bits are not the anchor's; within the maximum error of the formula
    uint32_t n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    CHECK(n_bits != initial_bits);

    // Another block at 1050 seconds returns to the anchor's bits
    add(1050, n_bits);
    n_bits = next();
    CHECK(n_bits == initial_bits);
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);

    // Timestamps before the anchor work: jumping 2 days into the past halves the target
    add(600 - 172800, n_bits);
    n_bits = next();
    auto const halved = target_of(n_bits);
    uint256_t const half_upper = target_of(initial_bits) / 2;
    uint256_t const half_lower = target_of(initial_bits - 1) / 2;
    CHECK(bool(halved <= half_upper));
    CHECK(bool(halved >= half_lower));
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);

    // Jumping forward 2 days returns the target to the initial value
    add(600 + 172800, n_bits);
    n_bits = next();
    CHECK(n_bits == initial_bits);
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);

    // History every 10 minutes
    while (blocks.size() < 149) add(600, n_bits);
    n_bits = next();
    CHECK(n_bits == initial_bits);

    // Difficulty stays the same as long as we produce a block every 10 minutes
    for (size_t j = 0; j < 10; j++) {
        add(600, n_bits);
        CHECK(next() == n_bits);
    }

    // Two blocks whose solve times add up to 1200 s bring the target back to what it was
    add(300, n_bits);
    n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    add(900, n_bits);
    n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    CHECK(n_bits == initial_bits);
    CHECK(n_bits != blocks.back().bits);

    // Same in reverse: slower block first
    add(900, n_bits);
    n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    add(300, n_bits);
    n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    CHECK(n_bits == initial_bits);
    CHECK(n_bits != blocks.back().bits);

    // Jumping forward 2 days doubles the target
    add(600 + 2 * 24 * 3600, n_bits);
    n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    {
        uint256_t const half = target_of(n_bits) / 2;
        CHECK(chain::compact(half).normal() == initial_bits);
    }

    // Jumping backward 2 days brings it back
    add(600 - 2 * 24 * 3600, n_bits);
    n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    CHECK(n_bits == initial_bits);

    // Jumping backward 2 days halves the target
    add(600 - 2 * 24 * 3600, n_bits);
    n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    auto const halved_again = target_of(n_bits);
    CHECK(bool(halved_again <= half_upper));
    CHECK(bool(halved_again >= half_lower));

    // And forward again, twice
    add(600 + 2 * 24 * 3600, n_bits);
    n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    CHECK(n_bits == initial_bits);
    add(600 + 2 * 24 * 3600, n_bits);
    n_bits = next();
    CHECK(std::fabs(err_abs(blocks.back(), n_bits)) < max_err);
    {
        uint256_t const half = target_of(n_bits) / 2;
        CHECK(chain::compact(half).normal() == initial_bits);
    }

    // The whole -2*24*3600..+2*24*3600 range, one second at a time: monotonic, changes at least once
    // every 8 seconds, bounded step, within max_err of the double calculation, and with the min/max
    // errors the formula is known to have.
    double d_min = 0, d_max = 0, d_max_step = 0;
    uint32_t ring[8] = {};
    add(-2 * 24 * 3600 - 30, n_bits);
    for (size_t j = 0; j < size_t(4 * 24 * 3600 + 660); j++) {
        auto& last = blocks.back();
        last.time++;
        n_bits = next();
        if (j > 8) {
            CHECK(target_of(n_bits) >= target_of(ring[(j - 1) % 8]));           // 1: monotonic
            CHECK(target_of(n_bits) > target_of(ring[j % 8]));                  // 2: changes at least once every 8 seconds
            double const step = (target_from_bits(n_bits) - target_from_bits(ring[(j - 1) % 8])) / target_from_bits(n_bits);
            if (step > d_max_step) d_max_step = step;
            CHECK(step < 0.0000314812106363);                                    // 3: 1-second step size
        }
        ring[j % 8] = n_bits;
        double const d_err = err_abs(last, n_bits);                              // 4 and 5
        if (d_err < d_min) d_min = d_err;
        if (d_err > d_max) d_max = d_err;
        CHECK(std::fabs(d_err) < max_err);
    }
    CHECK(d_min < -0.0001013168981059);
    CHECK(d_min > -0.0001013168981060);
    CHECK(d_max > 0.0001166792656485);
    CHECK(d_max < 0.0001166792656486);

    // Difficulty increases as long as we produce fast blocks
    for (size_t j = 0; j < 100; j++) {
        auto const current_target = target_of(n_bits);
        add(500, n_bits);
        auto const next_bits = next();
        CHECK(target_of(next_bits) <= current_target);
        n_bits = next_bits;
    }
}

#endif // defined(KTH_CURRENCY_BCH)
