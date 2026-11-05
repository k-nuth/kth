// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CHIP-2025-03 Faster Blocks: tick schedule, subsidy, locks, maturity, ASERT
// and the activation discovery, exercised through the public behaviour of
// chain_state / block / transaction rather than through helper internals only.

#include <test_helpers.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include <kth/domain/chain/abla.hpp>
#include <kth/domain/chain/block_ticks.hpp>
#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/compact.hpp>
#include <kth/domain/chain/daa/aserti3_2d.hpp>
#include <kth/domain/chain/faster_blocks.hpp>
#include <kth/domain/constants/bch.hpp>

using namespace kth;
using namespace kd;
using kd::chain::block_ticks;

namespace {

constexpr uint64_t h_a = 1'003'670;     // the CHIP's illustrative mainnet H_a

block_ticks const& activated() {
    static auto const ticks = block_ticks::activated_at(h_a);
    return ticks;
}

} // namespace

// -----------------------------------------------------------------------------
// block_ticks
// -----------------------------------------------------------------------------

TEST_CASE("block_ticks uniform schedule is height * 600", "[block_ticks]") {
    block_ticks const ticks;
    REQUIRE(ticks.height_to_tick(0) == 0);
    REQUIRE(ticks.height_to_tick(1) == 600);
    REQUIRE(ticks.height_to_tick(123456) == 123456ull * 600);
    REQUIRE(ticks.block_interval(0) == 600);
    REQUIRE(ticks.block_interval(1) == 600);
    REQUIRE(ticks.next_block_interval(0) == 600);
    REQUIRE(ticks.tick_to_height(599) == 0);
    REQUIRE(ticks.tick_to_height(600) == 1);
    REQUIRE(ticks.tick_to_height(601) == 1);
}

TEST_CASE("block_ticks matches the CHIP figure around H_a", "[block_ticks]") {
    // H_a is the LAST 600-tick block; H_a + 1 is the first 60-tick one.
    REQUIRE(activated().height_to_tick(h_a - 1) == 602'201'400);
    REQUIRE(activated().height_to_tick(h_a) == 602'202'000);
    REQUIRE(activated().height_to_tick(h_a + 1) == 602'202'060);
    REQUIRE(activated().height_to_tick(h_a + 2) == 602'202'120);

    REQUIRE(activated().block_interval(h_a) == 600);
    REQUIRE(activated().block_interval(h_a + 1) == 60);
    REQUIRE(activated().next_block_interval(h_a) == 60);
    REQUIRE(activated().next_block_interval(h_a - 1) == 600);
}

TEST_CASE("block_ticks candidate tick comes from the schedule, not tip + tip interval", "[block_ticks]") {
    // The tip is H_a: extrapolating its own 600 would overshoot by 540.
    REQUIRE(activated().next_block_tick(h_a) == 602'202'060);
    REQUIRE(activated().next_block_tick(h_a) != activated().height_to_tick(h_a) + activated().block_interval(h_a));
    REQUIRE(activated().next_block_tick(h_a - 1) == 602'202'000);
}

TEST_CASE("block_ticks tick_to_height is the greatest height whose tick fits", "[block_ticks]") {
    auto const& t = activated();
    REQUIRE(t.tick_to_height(602'202'000) == h_a);
    REQUIRE(t.tick_to_height(602'202'059) == h_a);
    REQUIRE(t.tick_to_height(602'202'060) == h_a + 1);
    REQUIRE(t.tick_to_height(602'202'119) == h_a + 1);
    REQUIRE(t.tick_to_height(602'202'120) == h_a + 2);
    REQUIRE(t.tick_to_height(602'201'999) == h_a - 1);
}

TEST_CASE("block_ticks activation at genesis sets block 1's interval", "[block_ticks]") {
    auto const t = block_ticks::activated_at(0);
    REQUIRE(t.block_interval(0) == 600);
    REQUIRE(t.block_interval(1) == 60);
    REQUIRE(t.height_to_tick(1) == 60);
}

TEST_CASE("block_ticks saturates instead of wrapping", "[block_ticks]") {
    block_ticks const uniform;
    REQUIRE(uniform.height_to_tick(chain::max_tick_height) == chain::max_tick);
    REQUIRE(uniform.height_to_tick(chain::max_tick_height + 1) == chain::max_tick);
    REQUIRE(uniform.height_to_tick(UINT64_MAX) == chain::max_tick);
    REQUIRE(uniform.tick_to_height(UINT64_MAX) == chain::max_tick_height);
}

TEST_CASE("block_ticks::make enforces the schedule constraints", "[block_ticks]") {
    REQUIRE(block_ticks::make({}).has_value());
    REQUIRE(block_ticks::make({{100, 60}}).has_value());
    REQUIRE(block_ticks::make({{100, 60}, {1000, 30}}).has_value());   // 100*600 + 900*60 = 114000, % 600 == 0
    REQUIRE( ! block_ticks::make({{100, 0}}));                          // tau out of range
    REQUIRE( ! block_ticks::make({{100, 601}}));
    REQUIRE( ! block_ticks::make({{100, 7}}));                          // does not divide 600
    REQUIRE( ! block_ticks::make({{100, 60}, {100, 30}}));              // not strictly ascending
    REQUIRE( ! block_ticks::make({{100, 60}, {50, 30}}));
    REQUIRE( ! block_ticks::make({{100, 60}, {105, 30}}));              // tick(105) = 60000 + 300, not on the grid
}

// -----------------------------------------------------------------------------
// Activation discovery (branch-local H_a)
// -----------------------------------------------------------------------------

namespace {

std::optional<uint32_t> at(std::vector<uint32_t> const& ts, size_t h) {
    if (h >= ts.size()) return std::nullopt;
    return ts[h];
}

} // namespace

TEST_CASE("find_faster_blocks_activation_height returns the first height whose own MTP reaches the time", "[faster_blocks]") {
    // Timestamps 1000, 1010, ... so MTP(h) is the median of the last 11.
    std::vector<uint32_t> ts;
    for (uint32_t i = 0; i < 100; ++i) ts.push_back(1000 + i * 10);
    auto lookup = [&](size_t h) { return at(ts, h); };

    // MTP(h) == ts[h - 5] for h >= 10. Activation at 1300 -> MTP(h) >= 1300
    // first when h - 5 == 30, i.e. h == 35.
    auto const found = chain::find_faster_blocks_activation_height(99, 1300, lookup);
    REQUIRE(found.has_value());
    REQUIRE(*found == 35);

    // Seen from a shorter branch the same ancestors give the same answer.
    REQUIRE(*chain::find_faster_blocks_activation_height(40, 1300, lookup) == 35);
    REQUIRE(*chain::find_faster_blocks_activation_height(35, 1300, lookup) == 35);

    // Not reached at the parent: nothing to find.
    REQUIRE( ! chain::find_faster_blocks_activation_height(34, 1300, lookup));
    // An unreadable ancestor is a failure, not a guess.
    REQUIRE( ! chain::find_faster_blocks_activation_height(120, 1300, lookup));
}

TEST_CASE("find_faster_blocks_activation_height differs between branches that diverge before the activation", "[faster_blocks]") {
    // Six timestamps on the divergent branch must reach the MTP threshold
    // before the original branch reaches it at height 35.
    std::vector<uint32_t> common;
    for (uint32_t i = 0; i < 20; ++i) common.push_back(1000 + i * 10);

    auto fast = common;     // branch A keeps the cadence
    auto slow = common;     // branch B stalls for a while
    for (uint32_t i = 20; i < 80; ++i) fast.push_back(1000 + i * 10);
    for (uint32_t i = 20; i < 80; ++i) slow.push_back(1000 + i * 10 + 400);

    auto fa = [&](size_t h) { return at(fast, h); };
    auto fb = [&](size_t h) { return at(slow, h); };
    auto const a = chain::find_faster_blocks_activation_height(79, 1300, fa);
    auto const b = chain::find_faster_blocks_activation_height(79, 1300, fb);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(*a == 35);
    REQUIRE(*b == 25);
    REQUIRE(*b < *a);   // the slower branch's timestamps are later, so it activates sooner in height
}

#if defined(KTH_CURRENCY_BCH)

// -----------------------------------------------------------------------------
// ABLA per-unit-time scaling
// -----------------------------------------------------------------------------

TEST_CASE("abla: the 600-tick transition is the pre-CHIP transition", "[faster_blocks][abla]") {
    auto const cfg = chain::abla::default_config(32'000'000, false);
    chain::abla::state st(cfg, 0);
    for (uint64_t size : {uint64_t(0), uint64_t(1'000'000), uint64_t(16'000'000), uint64_t(32'000'000)}) {
        st.block_size = size;
        auto const legacy = chain::abla::next(st, cfg, size);
        auto const scaled = chain::abla::next(st, cfg, size, 600);
        REQUIRE(legacy.has_value());
        REQUIRE(scaled.has_value());
        REQUIRE(legacy->control_block_size == scaled->control_block_size);
        REQUIRE(legacy->elastic_buffer_size == scaled->elastic_buffer_size);
        REQUIRE(legacy->block_size == scaled->block_size);
        REQUIRE(chain::abla::block_size_limit(*scaled, 600) == chain::abla::block_size_limit(*scaled));
    }
}

TEST_CASE("abla: a 60-tick block sees a tenth of the limit and a tenth of the rates", "[faster_blocks][abla]") {
    auto const cfg = chain::abla::default_config(32'000'000, false);

    chain::abla::state st(cfg, 0);
    REQUIRE(chain::abla::block_size_limit(st, 60) == chain::abla::block_size_limit(st) / 10);

    // A full 60-tick block (a tenth of the 600-tick limit) is a FULL block in
    // rate terms, so the control size moves by a tenth of what a full 600-tick
    // block moves it, truncated once.
    auto const limit600 = chain::abla::block_size_limit(st);
    st.block_size = limit600;
    auto const full600 = chain::abla::next(st, cfg, limit600, 600);
    st.block_size = limit600 / 10;
    st.block_interval = 60; // transition responds to the previous block's interval
    auto const full60 = chain::abla::next(st, cfg, limit600 / 10, 60);
    REQUIRE(full600.has_value());
    REQUIRE(full60.has_value());

    auto const rise600 = full600->control_block_size - cfg.epsilon0;
    auto const rise60 = full60->control_block_size - cfg.epsilon0;
    REQUIRE(rise600 > 0);
    REQUIRE(rise60 > 0);
    REQUIRE(rise60 <= rise600 / 10 + 1);
    REQUIRE(rise60 + 1 >= rise600 / 10);

    // Block sizes beyond the interval's limit are clamped to it (not to the 600 limit).
    chain::abla::state over(cfg, 0, 60);
    over.block_size = limit600;                                // 10x what a 60-tick block may carry
    auto const clamped = chain::abla::next(over, cfg, 0, 60);
    REQUIRE(clamped.has_value());
    REQUIRE(clamped->control_block_size == full60->control_block_size);
}

// -----------------------------------------------------------------------------
// chain_state: schedule, subsidy, locks, maturity and ASERT
// -----------------------------------------------------------------------------

namespace {

constexpr uint32_t activation_time = 1'810'382'400;

chain::chain_state::data data_at(size_t height, uint32_t mtp_base, size_t activation_height) {
    chain::chain_state::data values{};
    values.height = height;
    values.hash = null_hash;
    values.allow_collisions_hash = null_hash;
    values.bits.self = 0x1d00ffff;
    values.version.self = 4;
    values.timestamp.self = mtp_base + 1000;
    values.timestamp.retarget = mtp_base;
    for (size_t i = 0; i < 11; ++i) {
        values.bits.ordered.push_back(0x1d00ffff);
        values.version.ordered.push_back(4);
        values.timestamp.ordered.push_back(mtp_base + uint32_t(i));
    }
    values.faster_blocks_activation_height = activation_height;
    return values;
}

chain::chain_state make_state(chain::chain_state::data values, kth::cantor_t time = kth::cantor_t(activation_time)) {
    static chain::chain_state::checkpoints const none{};
    return chain::chain_state(std::move(values), std::to_underlying(domain::machine::script_flags::retarget), none,
        domain::config::network::mainnet,
        chain::chain_state::assert_anchor_block_info_t{
            kth::mainnet_asert_anchor_block_height,
            kth::mainnet_asert_anchor_block_ancestor_time,
            kth::mainnet_asert_anchor_block_bits},
        2ull * 24 * 60 * 60, chain::abla::default_config(32'000'000, false), time);
}

} // namespace

TEST_CASE("chain_state keeps the uniform schedule while the parent's MTP is below the activation time", "[faster_blocks]") {
    auto const state = make_state(data_at(1'000'000, activation_time - 5000, max_size_t));
    REQUIRE( ! state.is_faster_blocks_active());
    REQUIRE(state.ticks().schedule().empty());
    REQUIRE(state.tick() == 1'000'000ull * 600);
    REQUIRE(state.block_interval() == 600);
}

TEST_CASE("chain_state: H_a is the last 600-tick block and the next is the first 60-tick one", "[faster_blocks]") {
    // Parent MTP has reached the time; H_a was discovered at 999'999, which is
    // the parent of the candidate at 1'000'000, so the candidate is block H_a + 1.
    auto const first = make_state(data_at(1'000'000, activation_time + 5, 999'999));
    REQUIRE(first.is_faster_blocks_active());
    REQUIRE(first.block_interval() == 60);
    REQUIRE(first.parent_tick() == 999'999ull * 600);
    REQUIRE(first.tick() == 999'999ull * 600 + 60);

    // Ten blocks later the candidate is H_a + 11.
    auto const later = make_state(data_at(1'000'010, activation_time + 5, 999'999));
    REQUIRE(later.tick() == 999'999ull * 600 + 11 * 60);

    // The candidate can also sit AT H_a (parent H_a - 1 not yet activated): uniform.
    auto const at_ha = make_state(data_at(999'999, activation_time - 5000, max_size_t));
    REQUIRE(at_ha.block_interval() == 600);
    REQUIRE(at_ha.tick() == 999'999ull * 600);
}

TEST_CASE("chain_state: two branches with different H_a have different ticks at the same height", "[faster_blocks]") {
    auto const early = make_state(data_at(1'000'100, activation_time + 5, 999'990));
    auto const late = make_state(data_at(1'000'100, activation_time + 5, 1'000'050));
    REQUIRE(early.tick() != late.tick());
    REQUIRE(early.tick() == 999'990ull * 600 + 110 * 60);
    REQUIRE(late.tick() == 1'000'050ull * 600 + 50 * 60);
}

TEST_CASE("chain_state with no activation configured never leaves the uniform schedule", "[faster_blocks]") {
    auto const state = make_state(data_at(1'000'000, activation_time + 999'999, 5), kth::cantor_t{UINT64_MAX});
    REQUIRE( ! state.is_faster_blocks_active());
    REQUIRE(state.tick() == 1'000'000ull * 600);
}

TEST_CASE("subsidy is ((50 BCH) >> halvings) * tau / 600 with the halving keyed to the 600-tick chunk", "[faster_blocks]") {
    auto const uniform = block_ticks{};
    // Uniform: identical to the legacy height form at every height.
    for (size_t h : {size_t(0), size_t(1), size_t(209'999), size_t(210'000), size_t(419'999), size_t(420'000), size_t(13'440'000)}) {
        REQUIRE(chain::block::subsidy(uniform, h, true) == chain::block::subsidy(h, true));
    }

    // 60-tick blocks earn a tenth.
    auto const t = block_ticks::activated_at(209'990);
    REQUIRE(chain::block::subsidy(t, 209'990, true) == 50 * 100'000'000ull);        // H_a still 600-tick
    REQUIRE(chain::block::subsidy(t, 209'991, true) == 5 * 100'000'000ull);

    // The last era-0 chunk ends at tick209999*600, reached at H_a+90.
    // The NEXT short block already belongs to chunk210000 (ceil, not floor),
    // so all ten blocks of that chunk earn the halved reward.
    REQUIRE(chain::block::subsidy(t, 209'990 + 90, true) == 5 * 100'000'000ull);
    REQUIRE(chain::block::subsidy(t, 209'990 + 91, true) == 25 * 100'000'000ull / 10);
    REQUIRE(chain::block::subsidy(t, 209'990 + 99, true) == 25 * 100'000'000ull / 10);
    REQUIRE(chain::block::subsidy(t, 209'990 + 100, true) == 25 * 100'000'000ull / 10);
    REQUIRE(chain::block::subsidy(t, 209'990 + 50, true) == 5 * 100'000'000ull);
}

// Regression for the nine-block overpayment reported in the CHIP discussion:
// https://bitcoincashresearch.org/t/chip-2025-03-faster-blocks-for-bitcoin-cash/1513/211
TEST_CASE("twenty short blocks preserve the two payments around the next halving", "[faster_blocks][faster_subsidy]") {
    constexpr size_t activation = 1'049'998;
    auto const ticks = block_ticks::activated_at(activation);
    REQUIRE(chain::block::subsidy(1'049'999, true) == 312'500'000);
    REQUIRE(chain::block::subsidy(1'050'000, true) == 156'250'000);
    uint64_t total = 0;
    for (size_t installment = 1; installment <= 20; ++installment) {
        CAPTURE(installment);
        auto const reward = chain::block::subsidy(ticks, activation + installment, true);
        CHECK(reward == (installment <= 10 ? 31'250'000 : 15'625'000));
        total += reward;
    }
    CHECK(total == 468'750'000); // 4.6875 BCH; the historical floor bug pays 609,375,000.
}

TEST_CASE("a thirty-seven-satoshi legacy payment yields ten three-satoshi installments", "[faster_blocks][faster_subsidy]") {
    auto const ticks = block_ticks::activated_at(5'669'999);
    REQUIRE(chain::block::subsidy(5'670'000, true) == 37);
    uint64_t total = 0;
    for (size_t installment = 1; installment <= 10; ++installment) {
        CAPTURE(installment);
        auto const reward = chain::block::subsidy(ticks, 5'669'999 + installment, true);
        CHECK(reward == 3);
        total += reward;
    }
    CHECK(total == 30); // Seven satoshis are intentionally not redistributed.
}

TEST_CASE("short-block subsidies reach zero without restarting a later era", "[faster_blocks][faster_subsidy]") {
    constexpr size_t activation = 6'089'998;
    auto const ticks = block_ticks::activated_at(activation);
    REQUIRE(chain::block::subsidy(6'089'999, true) == 18);
    REQUIRE(chain::block::subsidy(6'090'000, true) == 9);
    uint64_t total = 0;
    for (size_t installment = 1; installment <= 20; ++installment) {
        CAPTURE(installment);
        auto const reward = chain::block::subsidy(ticks, activation + installment, true);
        CHECK(reward == (installment <= 10 ? 1 : 0));
        total += reward;
    }
    CHECK(total == 10);
    // The original whole-block schedule reaches zero in era 33. The shift
    // guard must also keep zero at and beyond era 64 on a shorter schedule.
    for (size_t const era : {size_t(33), size_t(64), size_t(65)}) {
        CAPTURE(era);
        auto const boundary = era * 210'000;
        auto const later = block_ticks::activated_at(boundary - 1);
        CHECK(chain::block::subsidy(boundary, true) == 0);
        CHECK(chain::block::subsidy(later, boundary, true) == 0);
        CHECK(chain::block::subsidy(later, boundary + 9, true) == 0);
    }
}

TEST_CASE("height-mode locktime is final iff T(block) > 600 * locktime", "[faster_blocks]") {
    auto const t = block_ticks::activated_at(1000);     // T(1000) = 600000, T(1001) = 600060
    chain::input::list in;
    in.emplace_back(chain::output_point{null_hash, 0}, chain::script{}, 0);   // non-final sequence
    chain::output::list out;
    out.emplace_back(0, chain::script{}, chain::token_data_opt{});

    chain::transaction tx(1, 1001, in, out);
    // Uniform 600: final from height 1002 (T > 600 * 1001 = 600600).
    REQUIRE( ! tx.is_final(block_ticks{}, 1001, 0));
    REQUIRE(tx.is_final(block_ticks{}, 1002, 0));
    // With H_a = 1000: T(1001) = 600060 < 600600, and ten blocks later it is still short.
    REQUIRE( ! tx.is_final(t, 1002, 0));
    REQUIRE( ! tx.is_final(t, 1010, 0));
    REQUIRE(tx.is_final(t, 1011, 0));                    // T = 600660 > 600600; no quotient rounding
    // The legacy height wrapper stays the uniform schedule.
    REQUIRE(tx.is_final(1002, 0));
}

TEST_CASE("coinbase maturity is 60000 ticks, not 100 blocks, across the activation", "[faster_blocks]") {
    auto const t = block_ticks::activated_at(110);
    chain::output_point prevout{null_hash, 0};
    prevout.validation.coinbase = true;
    prevout.validation.height = 11;                     // T(11) = 6600

    // Legacy count says 100 blocks (111 - 11) is mature; in ticks it is 59460 < 60000.
    REQUIRE( ! prevout.is_mature(t, 111));
    REQUIRE(prevout.is_mature(block_ticks{}, 111));
    // T(b) - 6600 >= 60000 <=> b >= 110 + 10 = 120.
    REQUIRE( ! prevout.is_mature(t, 119));
    REQUIRE(prevout.is_mature(t, 120));
}

TEST_CASE("relative sequence lock is 600 * n ticks across the activation", "[faster_blocks]") {
    auto const t = block_ticks::activated_at(110);
    chain::input in{chain::output_point{null_hash, 0}, chain::script{}, 20};     // height-mode n = 20
    in.previous_output().validation.height = 100;                                  // T(100) = 60000
    // Needs T(b) >= 60000 + 12000 = 72000 -> b = 110 + (72000 - 66000) / 60 = 210.
    REQUIRE(in.is_locked(t, 209, 0));
    REQUIRE( ! in.is_locked(t, 210, 0));
    // Uniform: 20 blocks.
    REQUIRE(in.is_locked(block_ticks{}, 119, 0));
    REQUIRE( ! in.is_locked(block_ticks{}, 120, 0));
}

TEST_CASE("mainnet ASERT: on-schedule 60-tick blocks target ten times the anchor target", "[faster_blocks]") {
    // Exponent zero: time_diff == tick_delta + anchor interval. Then only the
    // anchor_interval / next_interval = 10 ratio remains.
    constexpr size_t anchor = kth::mainnet_asert_anchor_block_height;
    constexpr size_t activation = anchor + 100;          // H_a
    constexpr size_t candidate = activation + 11;        // 11th 60-tick block
    auto const tick_term = (activation * 600 + 10 * 60) - anchor * 600 + 600;   // T(prev) - T(anchor) + 600

    auto values = data_at(candidate, activation_time + 5, activation);
    values.timestamp.ordered.back() = uint32_t(kth::mainnet_asert_anchor_block_ancestor_time + tick_term);
    values.timestamp.self = values.timestamp.ordered.back() + 60;
    values.bits.ordered.back() = kth::mainnet_asert_anchor_block_bits;

    auto const state = make_state(std::move(values), kth::cantor_t(0));

    auto const anchor_target = chain::compact::from_compact(kth::mainnet_asert_anchor_block_bits).value();
    auto const expected = chain::compact(anchor_target.big() * 10).normal();
    REQUIRE(state.is_faster_blocks_active());
    REQUIRE(state.work_required() == expected);
}

TEST_CASE("mainnet ASERT uses the anchor's own shortened interval when activation precedes it", "[faster_blocks][asert_anchor_ticks]") {
    constexpr size_t anchor = kth::mainnet_asert_anchor_block_height;
    constexpr size_t activation = anchor - 100;
    constexpr size_t candidate = anchor + 11;
    // Both anchor and candidate have60-tick intervals. T(parent)-T(anchor)
    // is10*60 and the anchor supplies another60, so an on-schedule chain
    // retains the anchor target (the interval ratio is60/60, not600/60).
    auto values = data_at(candidate, activation_time + 5, activation);
    values.timestamp.ordered.back() = uint32_t(kth::mainnet_asert_anchor_block_ancestor_time + 11 * 60);
    values.timestamp.self = values.timestamp.ordered.back() + 60;
    values.bits.ordered.back() = kth::mainnet_asert_anchor_block_bits;
    auto const state = make_state(std::move(values), kth::cantor_t(0));
    REQUIRE(state.is_faster_blocks_active());
    REQUIRE(state.ticks().block_interval(anchor) == 60);
    CHECK(state.work_required() == kth::mainnet_asert_anchor_block_bits);
}

TEST_CASE("mainnet ASERT: before activation the result is the legacy uniform-600 value", "[faster_blocks]") {
    constexpr size_t anchor = kth::mainnet_asert_anchor_block_height;
    constexpr size_t candidate = anchor + 1000;
    constexpr uint32_t parent_time = uint32_t(kth::mainnet_asert_anchor_block_ancestor_time + 1000 * 600 + 4321);

    auto values = data_at(candidate, activation_time - 100000, max_size_t);
    values.timestamp.ordered.back() = parent_time;
    values.bits.ordered.back() = kth::mainnet_asert_anchor_block_bits;
    auto const state = make_state(std::move(values));
    REQUIRE( ! state.is_faster_blocks_active());

    // The pre-CHIP formula, evaluated directly: exponent in 600 * heights.
    auto const pow_limit = chain::compact::from_compact(kth::retarget_proof_of_work_limit).value();
    auto const anchor_target = chain::compact::from_compact(kth::mainnet_asert_anchor_block_bits).value();
    auto const legacy = chain::daa::aserti3_2d(anchor_target, 600, int64_t(parent_time) - int64_t(kth::mainnet_asert_anchor_block_ancestor_time),
        int64_t(candidate - anchor), pow_limit, 2u * 24 * 60 * 60);
    REQUIRE(state.work_required() == chain::compact(legacy).normal());
}

#endif // KTH_CURRENCY_BCH

TEST_CASE("tick schedules compare by their complete interval history", "[faster_blocks][ticks]") {
    CHECK(block_ticks{} == block_ticks{});
    CHECK(block_ticks::activated_at(100) == block_ticks::activated_at(100));
    CHECK_FALSE(block_ticks::activated_at(100) == block_ticks::activated_at(101));
    CHECK_FALSE(block_ticks{} == block_ticks::activated_at(100));
}
