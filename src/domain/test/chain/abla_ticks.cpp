// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <algorithm>
#include <cstdint>
#include <kth/domain/chain/abla.hpp>
#include "abla_ticks_vectors.hpp"

namespace {
namespace abla = kth::domain::chain::abla;

// Preserve the same public-state/transition scenario on the unmodified base:
// its API cannot represent the shorter interval, and these exact golden
// observations then fail. No vector or assertion is skipped on that base.
template <typename State>
void set_interval(State& state, uint64_t interval) {
    if constexpr (requires { state.block_interval = interval; }) {
        state.block_interval = interval;
    }
}

template <typename State>
auto transition(State const& state, abla::config const& config, uint64_t size, uint64_t interval) {
    if constexpr (requires { abla::next(state, config, size, interval); }) {
        return abla::next(state, config, size, interval);
    } else {
        return abla::next(state, config, size);
    }
}
} // namespace

TEST_CASE("ABLA lookahead preserves the state's target interval", "[abla][faster_blocks][lookahead]") {
    auto const cfg = abla::default_config(2'000'000, false);
    abla::state const short_state(cfg, 200'000, 60);
    // Exact factor-one pins from chipnet_full_60 / chipnet_ten_full_60.
    struct expected_state {
        size_t count;
        uint64_t epsilon, beta, size, limit;
    };
    for (auto const& expected : {
            expected_state{0, 1'000'000, 1'000'000, 200'000, 200'000},
            expected_state{1, 1'000'001, 1'000'008, 200'000, 200'000},
            expected_state{10, 1'000'010, 1'000'080, 200'008, 200'009}}) {
        CAPTURE(expected.count);
        auto const future = abla::lookahead(short_state, cfg, expected.count);
        REQUIRE(future.has_value());
        CHECK(future->block_interval == 60);
        CHECK(future->control_block_size == expected.epsilon);
        CHECK(future->elastic_buffer_size == expected.beta);
        CHECK(future->block_size == expected.size);
        CHECK(abla::block_size_limit(*future) == expected.limit);
    }
    auto const legacy = abla::lookahead(abla::state(cfg, 2'000'000), cfg, 1);
    REQUIRE(legacy.has_value());
    CHECK(legacy->block_interval == 600);
    CHECK(legacy->control_block_size == 1'000'013);
    CHECK(legacy->elastic_buffer_size == 1'000'104);
    CHECK(abla::block_size_limit(*legacy) == 2'000'117);
}

TEST_CASE("ABLA factor one state transitions match the CHIP golden vectors", "[abla][faster_blocks]") {
    for (auto const& vector : abla_test_vectors::transitions) {
        CAPTURE(vector.name);
        auto const config = abla::default_config(vector.base_size, vector.fixed);
        abla::state state(config, vector.before.size);
        state.control_block_size = vector.before.epsilon;
        state.elastic_buffer_size = vector.before.beta;
        set_interval(state, vector.before.interval);
        for (auto const& step : vector.steps) {
            auto const result = transition(state, config, step.size, step.interval);
            REQUIRE(result.has_value());
            state = *result;
            CHECK(state.block_size == step.size);
            CHECK(state.control_block_size == step.epsilon);
            CHECK(state.elastic_buffer_size == step.beta);
            CHECK(abla::block_size_limit(state) == step.limit);
        }
    }
}

TEST_CASE("ABLA factor one output limits match the CHIP golden vectors", "[abla][faster_blocks]") {
    for (auto const& vector : abla_test_vectors::limits) {
        CAPTURE(vector.name);
        abla::state state;
        state.control_block_size = vector.epsilon;
        state.elastic_buffer_size = vector.beta;
        set_interval(state, vector.interval);
        auto const raw_limit = abla::block_size_limit(state);
        // ABLA exposes its raw arithmetic limit; the existing chain-state
        // boundary owns the unchanged two-gigabyte post-filter cap.
        auto const limit = vector.disable_cap ? raw_limit : std::min(raw_limit, uint64_t(2'000'000'000));
        CHECK(limit == vector.expected);
    }
}

TEST_CASE("ABLA activation transition responds to the last old interval", "[abla][faster_blocks]") {
    auto const config = abla::default_config();
    abla::state const old_tip(config, 32'000'000);
    auto const first_short = transition(old_tip, config, 3'200'188, 60);
    REQUIRE(first_short.has_value());
    // The last 600-tick full block still contributes the complete 210-unit
    // control step; selecting the next interval must not reduce it to 21.
    CHECK(first_short->control_block_size == 16'000'210);
    CHECK(first_short->elastic_buffer_size == 16'001'679);
    CHECK(abla::block_size_limit(*first_short) == 3'200'188);
    auto const second_short = transition(*first_short, config, 3'200'207, 60);
    REQUIRE(second_short.has_value());
    CHECK(second_short->control_block_size == 16'000'231);
    CHECK(second_short->elastic_buffer_size == 16'001'847);
    CHECK(abla::block_size_limit(*second_short) == 3'200'207);
}

TEST_CASE("ABLA rejects invalid stored and next intervals", "[abla][faster_blocks]") {
    auto const config = abla::default_config();
    abla::state state(config, 0);
    CHECK_FALSE(transition(state, config, 0, 0).has_value());
    CHECK_FALSE(transition(state, config, 0, 601).has_value());
    set_interval(state, 0);
    CHECK_FALSE(transition(state, config, 0, 60).has_value());
    set_interval(state, 601);
    CHECK_FALSE(transition(state, config, 0, 60).has_value());
}
