// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of Bitcoin Cash Node src/test/abla_test_vectors.cpp (test_all) over the
// vectors of src/test/data/abla_test_vectors, commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b.
// Original: Copyright (c) 2023 The Bitcoin developers, MIT license.
//
// 33 of the 40 vectors run here. The other 7 reproduce two defects of the
// production code and are tracked as failed_regression in
// test/bchn_non_vm/coverage.json, not adapted to pass:
//   - config1-test4-lookahead-{1,2}: abla::lookahead feeds the limit of the
//     current state as the next block size instead of the limit of the next one.
//   - config3-test2-random{2000,8196}, config4-test2-random{512,2000,8196}:
//     expect BCHN's 2GB cap on the ABLA limit (these vectors have no
//     -disable2GBLimit); abla::block_size_limit has no cap.

#include <test_helpers.hpp>

#include <kth/domain/chain/abla.hpp>

#include "abla_test_vectors_data.hpp"

#include <algorithm>

namespace abla = kth::domain::chain::abla;

namespace {

abla::config make_config(abla_vectors::config_t const& c) {
    abla::config config;
    config.epsilon0 = c.epsilon0;
    config.beta0 = c.beta0;
    config.zeta_xB7 = c.zeta;
    config.gamma_reciprocal = c.gamma_reciprocal;
    config.delta = c.delta;
    config.theta_reciprocal = c.theta_reciprocal;
    abla::set_max(config);
    return config;
}

abla::state make_state(uint64_t block_size, uint64_t control, uint64_t elastic) {
    abla::state s;
    s.block_size = block_size;
    s.control_block_size = control;
    s.elastic_buffer_size = elastic;
    return s;
}

void run_vector(abla_vectors::vector_t const& test) {
    auto const config = make_config(test.config);
    REQUIRE(abla::validate(config) == abla::config_validity::valid);
    uint64_t const n0 = test.config.n0;

    auto state = make_state(0, test.initial_state.epsilon, test.initial_state.beta);
    REQUIRE(abla::validate(state, config) == abla::state_validity::valid);
    uint64_t const initial_limit = test.initial_limit;
    uint64_t limit = initial_limit;
    uint64_t n = test.initial_state.n;
    abla_vectors::state_t tstate = test.initial_state;

    if (test.is_lookahead) {
        auto const& o = test.lookahead;
        REQUIRE(o.lookahead > 0);
        REQUIRE(limit == tstate.epsilon + tstate.beta);
        REQUIRE(abla::block_size_limit(state) == limit);
        REQUIRE(o.limit == o.state.epsilon + o.state.beta);
        REQUIRE(tstate.n + o.lookahead == o.state.n);

        tstate.n = std::max(n0, tstate.n);      // simulate "advancement" to the activation block
        if (tstate.n < o.state.n) {
            // initial block is "full"
            state = make_state(limit, tstate.epsilon, tstate.beta);
            auto const result = abla::lookahead(state, config, o.state.n - tstate.n);
            REQUIRE(result.has_value());
            CHECK(abla::block_size_limit(*result) == o.limit);
        }
        return;
    }

    uint64_t block_size = 0;
    uint64_t limit_next = 0;

    auto do_checks_and_advance = [&](bool past_the_end) {
        REQUIRE(n == tstate.n);
        state = make_state(block_size, state.control_block_size, state.elastic_buffer_size);
        CHECK(limit == abla::block_size_limit(state));
        CHECK(state.control_block_size == tstate.epsilon);
        CHECK(state.elastic_buffer_size == tstate.beta);
        if (n >= n0) {
            auto const next = abla::next(state, config, block_size);
            REQUIRE(next.has_value());
            if ( ! past_the_end) {
                CHECK(abla::block_size_limit(*next) == limit_next);
            }
            state = *next;
        } else if ( ! past_the_end) {
            CHECK(initial_limit == limit_next);
        }
        CHECK(abla::validate(state, config) == abla::state_validity::valid);
    };

    for (auto const& step : test.steps) {
        block_size = step.blocksize;
        limit_next = step.next_limit;
        do_checks_and_advance(false);
        tstate = step.next_state;
        limit = limit_next;
        ++n;
    }
    do_checks_and_advance(true);
}

} // namespace

// BCHN abla_test_vectors/test_all, one Catch2 section per vector file (33 of 40)
TEST_CASE("BCHN abla_test_vectors - test_all", "[bchn_non_vm][abla]") {
    auto const& all = abla_vectors::all();
    REQUIRE(all.size() == 33u);
    for (auto const& test : all) {
        DYNAMIC_SECTION(test.file) {
            run_vector(test);
        }
    }
}
