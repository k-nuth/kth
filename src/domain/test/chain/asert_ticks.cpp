// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <string>
#include <string_view>
#include <kth/domain/chain/daa/aserti3_2d.hpp>
#include <kth/domain/chain/chain_state.hpp>
#include <limits>
#include "asert_ticks_vectors.hpp"

#if defined(KTH_CURRENCY_BCH)
namespace {
namespace daa = kth::domain::chain::daa;

kth::uint256_t target(char const* hex) {
    return kth::uint256_t(std::string("0x") + hex);
}

kth::uint256_t legacy_calculation(asert_test_vectors::vector const& vector) {
    // The original API spells ideal elapsed time as spacing * height_delta.
    // Preserve the exact ideal-time input even for a mixed-interval vector:
    // use one-tick units when the value is not divisible by600. There is no
    // interval-ratio argument on that base, the behavioral gap tested below.
    auto const tick_term = vector.tick_delta + int64_t(vector.anchor_interval);
    auto const spacing = tick_term % 600 == 0 ? uint32_t(600) : uint32_t(1);
    return daa::aserti3_2d(target(vector.anchor), spacing, vector.time_diff,
        tick_term / spacing, target(asert_test_vectors::pow_limit), vector.half_life);
}

kth::uint256_t calculation(asert_test_vectors::vector const& vector) {
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    return daa::aserti3_2d_ticks(target(vector.anchor), vector.time_diff,
        vector.tick_delta + int64_t(vector.anchor_interval), vector.anchor_interval,
        vector.next_interval, target(asert_test_vectors::pow_limit), vector.half_life);
#else
    // Keep the identical target observations runnable on the unmodified base
    // whose API cannot carry the anchor/next interval ratio. No row is skipped.
    return legacy_calculation(vector);
#endif
}
} // namespace

TEST_CASE("ASERT tick targets match the complete reference calculation vectors", "[asert][faster_blocks]") {
    for (auto const& vector : asert_test_vectors::calculation) {
        CAPTURE(vector.name);
        CHECK(calculation(vector) == target(vector.expected));
    }
}

TEST_CASE("ASERT uniform six hundred tick vectors retain exact legacy targets", "[asert][faster_blocks][legacy]") {
    for (auto const& vector : asert_test_vectors::calculation) {
        if (vector.anchor_interval != 600 || vector.next_interval != 600) {
            continue;
        }
        CAPTURE(vector.name);
        CHECK(legacy_calculation(vector) == target(vector.expected));
        CHECK(calculation(vector) == target(vector.expected));
    }
}

TEST_CASE("ASERT one minute targets preserve the exact interval ratio", "[asert][faster_blocks]") {
    kth::uint256_t original;
    kth::uint256_t shorter;
    bool found_original = false, found_shorter = false;
    for (auto const& vector : asert_test_vectors::calculation) {
        if (std::string_view(vector.name) == "equiv_1M_600s") {
            original = calculation(vector);
            found_original = true;
        } else if (std::string_view(vector.name) == "equiv_1M_60s") {
            shorter = calculation(vector);
            found_shorter = true;
        }
    }
    REQUIRE(found_original);
    REQUIRE(found_shorter);
    kth::uint256_t const expected_shorter = original * 10;
    CHECK(shorter == expected_shorter);
}

TEST_CASE("ASERT interval ratio overflow cannot wrap into the minimum target", "[asert][faster_blocks]") {
    bool found = false;
    for (auto const& vector : asert_test_vectors::calculation) {
        if (std::string_view(vector.name) == "ratio_guard_wrap_low") {
            auto const result = calculation(vector);
            CHECK(result == target(asert_test_vectors::pow_limit));
            CHECK(result != kth::uint256_t(1));
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("chain state ASERT wrapper matches reference timing and flag boundaries", "[asert][faster_blocks][asert_wrapper]") {
    // Fablous bf058dfa, tests/asert/asert.json wrapper rows. Shift the anchor
    // to an ASERT-active height, preserving each row's elapsed time and ticks.
    // Expected compact targets are source pins, not recomputed by this test.
    using namespace kth;
    using namespace kth::domain;
    using chain::chain_state;
    struct vector {
        char const* name;
        bool easy;
        uint32_t previous_time, next_time;
        uint64_t tick_delta, interval;
        uint32_t half_life, bits;
    };
    for (auto const& row : {
        vector{"wrapper_min_600_below", true, 600, 1799, 0, 600, 172800, 0x1a010000},
        vector{"wrapper_min_600_at", true, 600, 1800, 0, 600, 172800, 0x1a010000},
        vector{"wrapper_min_600_above", true, 600, 1801, 0, 600, 172800, 0x1d00ffff},
        vector{"wrapper_min_600_off", false, 600, 1801, 0, 600, 172800, 0x1a010000},
        vector{"wrapper_min_60_at", true, 600, 720, 0, 60, 172800, 0x1a0a0000},
        vector{"wrapper_min_60_above", true, 600, 721, 0, 60, 172800, 0x1d00ffff},
        vector{"wrapper_min_60_off", false, 600, 721, 0, 60, 172800, 0x1a0a0000},
        vector{"wrapper_plumb_tickdelta60", false, 660, 721, 60, 60, 172800, 0x1a0a0000},
        vector{"wrapper_anchor_nonzero_tick", false, 1200, 1260, 600, 60, 172800, 0x1a0a0000},
        vector{"wrapper_testnet_halflife_fast", false, 2400, 2460, 600, 600, 3600, 0x1a014287},
    }) {
        CAPTURE(row.name);
        constexpr size_t anchor = 1'000'000;
        constexpr uint32_t epoch = 1'700'000'000;
        auto const previous_height = anchor + (row.tick_delta == 0 ? 0u : 1u);
        auto const activation_height = row.tick_delta == 600 ? previous_height : anchor;
        chain_state::data values{};
        values.height = previous_height + 1;
        values.bits.self = 0x1a010000;
        values.version.self = 4;
        values.timestamp.self = epoch + row.next_time;
        values.timestamp.retarget = epoch;
        for (size_t i = 0; i < 11; ++i) {
            values.bits.ordered.push_back(0x1a010000);
            values.version.ordered.push_back(4);
            values.timestamp.ordered.push_back(epoch + row.previous_time);
        }
        values.faster_blocks_activation_height = row.interval == 60 ? activation_height : max_size_t;
        auto flags = std::to_underlying(machine::script_flags::retarget);
        if (row.easy) flags |= std::to_underlying(machine::script_flags::easy_blocks);
        chain_state::checkpoints const checkpoints{};
        chain_state const state(std::move(values), flags, checkpoints, kth::domain::config::network::mainnet,
            chain_state::assert_anchor_block_info_t{anchor, epoch, 0x1a010000},
            row.half_life, chain::abla::default_config(32'000'000, false),
            cantor_t{row.interval == 60 ? uint64_t(1) : std::numeric_limits<uint64_t>::max()});
        CHECK(state.block_interval() == row.interval);
        CHECK(state.work_required() == row.bits);
    }
}
#endif
