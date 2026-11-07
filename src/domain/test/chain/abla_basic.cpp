// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of Bitcoin Cash Node src/test/abla_basic_tests.cpp
// (commit 07576013c91ff4a3a74acd85f189c69121cdad1b).
// Original: Copyright (c) 2023 The Bitcoin developers, MIT license.
//
// Cases not ported here (no Knuth equivalent API) are listed with their
// reason in test/bchn_non_vm/coverage.json.

#include <test_helpers.hpp>

#include <kth/domain/chain/abla.hpp>

#include <limits>

using namespace kth;
namespace abla = kth::domain::chain::abla;

namespace {

constexpr uint64_t b7 = abla::config::B7;

bool valid(abla::config const& c) {
    return abla::validate(c) == abla::config_validity::valid;
}

bool valid(abla::state const& s, abla::config const& c) {
    return abla::validate(s, c) == abla::state_validity::valid;
}

abla::state make_state(uint64_t block_size, uint64_t control, uint64_t elastic) {
    abla::state s;
    s.block_size = block_size;
    s.control_block_size = control;
    s.elastic_buffer_size = elastic;
    return s;
}

} // namespace

// BCHN abla_basic_tests/config_ctor
TEST_CASE("BCHN abla_basic_tests - config_ctor", "[bchn_non_vm][abla]") {
    abla::config const config;

    // default-constructed config is all 0's and is not valid
    REQUIRE( ! valid(config));
    REQUIRE(config.epsilon0 == 0);
    REQUIRE(config.beta0 == 0);
    REQUIRE(config.gamma_reciprocal == 0);
    REQUIRE(config.zeta_xB7 == 0);
    REQUIRE(config.theta_reciprocal == 0);
    REQUIRE(config.delta == 0);
    REQUIRE(config.epsilon_max == 0);
    REQUIRE(config.beta_max == 0);
}

// BCHN abla_basic_tests/config_isvalid
TEST_CASE("BCHN abla_basic_tests - config_isvalid", "[bchn_non_vm][abla]") {
    auto const config = abla::default_config();
    using v = abla::config_validity;

    REQUIRE(abla::validate(config) == v::valid);
    REQUIRE(abla::validate(abla::config{}) != v::valid);

    // epsilon0 > epsilonMax
    auto bad = config;
    bad.epsilon0 = config.epsilon_max + 1u;
    REQUIRE(abla::validate(bad) == v::error_epsilon_max);
    bad = config;
    bad.epsilon_max = config.epsilon0 - 1u;
    REQUIRE(abla::validate(bad) == v::error_epsilon_max);

    // beta0 > betaMax
    bad = config;
    bad.beta0 = config.beta_max + 1u;
    REQUIRE(abla::validate(bad) == v::error_beta_max);
    bad = config;
    bad.beta_max = config.beta0 - 1u;
    REQUIRE(abla::validate(bad) == v::error_beta_max);

    // zeta_xB7 out of range
    bad = config;
    bad.zeta_xB7 = abla::config::MIN_ZETA_XB7 - 1u;
    REQUIRE(abla::validate(bad) == v::error_zeta);
    bad = config;
    bad.zeta_xB7 = abla::config::MAX_ZETA_XB7 + 1u;
    REQUIRE(abla::validate(bad) == v::error_zeta);

    // gammaReciprocal out of range
    bad = config;
    bad.gamma_reciprocal = abla::config::MIN_GAMMA_RECIPROCAL - 1u;
    REQUIRE(abla::validate(bad) == v::error_gamma_reciprocal);
    bad = config;
    bad.gamma_reciprocal = abla::config::MAX_GAMMA_RECIPROCAL + 1u;
    REQUIRE(abla::validate(bad) == v::error_gamma_reciprocal);

    // delta out of range
    bad = config;
    bad.delta = abla::config::MIN_DELTA - 1u;
    REQUIRE(abla::validate(bad) == v::error_delta);
    bad = config;
    bad.delta = abla::config::MAX_DELTA + 1u;
    REQUIRE(abla::validate(bad) == v::error_delta);

    // thetaReciprocal out of range
    bad = config;
    bad.theta_reciprocal = abla::config::MIN_THETA_RECIPROCAL - 1u;
    REQUIRE(abla::validate(bad) == v::error_theta_reciprocal);
    bad = config;
    bad.theta_reciprocal = abla::config::MAX_THETA_RECIPROCAL + 1u;
    REQUIRE(abla::validate(bad) == v::error_theta_reciprocal);

    // epsilon0 sanity check w.r.t. gammaReciprocal
    bad = config;
    bad.epsilon0 = config.gamma_reciprocal * b7 / (config.zeta_xB7 - b7);
    // ok at the limit
    REQUIRE(abla::validate(bad) == v::valid);
    // below the limit it must fail
    bad.epsilon0--;
    REQUIRE(abla::validate(bad) == v::error_epsilon0);
}

// BCHN abla_basic_tests/config_setmax
TEST_CASE("BCHN abla_basic_tests - config_setmax", "[bchn_non_vm][abla]") {
    auto config = abla::default_config();

    REQUIRE(config.epsilon_max != 0u);
    REQUIRE(config.beta_max != 0u);

    config.epsilon_max = config.beta_max = 0u;
    REQUIRE( ! valid(config));

    // SetMax sets to some huge value
    abla::set_max(config);
    REQUIRE(valid(config));
    REQUIRE(config.epsilon_max > std::numeric_limits<uint32_t>::max());
    REQUIRE(config.beta_max > std::numeric_limits<uint32_t>::max());
}

// BCHN abla_basic_tests/state_ctors
TEST_CASE("BCHN abla_basic_tests - state_ctors", "[bchn_non_vm][abla]") {
    auto const config = abla::default_config();

    // default-constructed state is invalid and all zeroes
    abla::state const state0;
    REQUIRE( ! valid(state0, config));
    REQUIRE(state0.block_size == 0u);
    REQUIRE(state0.control_block_size == 0u);
    REQUIRE(state0.elastic_buffer_size == 0u);

    // non-default constructed state is valid and takes its defaults from the config
    abla::state const state(config, 12345u);
    REQUIRE(valid(state, config));
    REQUIRE(state.block_size == 12345u);
    REQUIRE(state.control_block_size == config.epsilon0);
    REQUIRE(state.elastic_buffer_size == config.beta0);
}

// BCHN abla_basic_tests/state_getblocksizelimit
// Only the uncapped half: Knuth has no 2GB cap on the ABLA limit, so the
// `disable2GBCap == false` assertions are tracked as a gap in the matrix.
TEST_CASE("BCHN abla_basic_tests - state_getblocksizelimit (uncapped)", "[bchn_non_vm][abla]") {
    auto const config = abla::default_config();
    abla::state state(config, 12345u);

    REQUIRE(valid(state, config));
    REQUIRE(abla::block_size_limit(state) == state.control_block_size + state.elastic_buffer_size);

    uint64_t const huge_cbs = config.epsilon_max / 3u;
    uint64_t const huge_ebs = config.beta_max / 3u;
    constexpr uint64_t max_consensus_block_size = uint64_t(2000) * 1000000u;
    REQUIRE(huge_cbs > max_consensus_block_size);
    REQUIRE(huge_ebs > max_consensus_block_size);
    state = make_state(6789u, huge_cbs, huge_ebs);
    REQUIRE(valid(state, config));
    REQUIRE(state.control_block_size == huge_cbs);
    REQUIRE(state.elastic_buffer_size == huge_ebs);
    REQUIRE(state.control_block_size + state.elastic_buffer_size > max_consensus_block_size);
    // disable2GBCap == true
    REQUIRE(abla::block_size_limit(state) == state.control_block_size + state.elastic_buffer_size);
}

// BCHN abla_basic_tests/state_algo_blocksize_exceeds_max_block_size
// Only the `disable2GBCap == true` iteration (Knuth has no 2GB cap).
TEST_CASE("BCHN abla_basic_tests - state_algo_blocksize_exceeds_max_block_size (uncapped)", "[bchn_non_vm][abla]") {
    auto const config = abla::default_config();
    uint64_t const huge_cbs = config.epsilon_max / 3u;
    uint64_t const huge_ebs = config.beta_max / 3u;
    auto const state = make_state(6789u, huge_cbs, huge_ebs);
    REQUIRE(valid(state, config));

    uint64_t const delta = 1'000'000'000u;
    uint64_t const exceeds_max = abla::block_size_limit(state) + delta;
    auto const over_saturated = make_state(exceeds_max, huge_cbs, huge_ebs);
    REQUIRE(valid(over_saturated, config));
    REQUIRE(exceeds_max == delta + over_saturated.control_block_size + over_saturated.elastic_buffer_size);

    auto const saturated = make_state(exceeds_max - delta, huge_cbs, huge_ebs);
    REQUIRE((over_saturated.block_size != saturated.block_size));
    auto const os_next = abla::next(over_saturated, config, 123u);
    auto const s_next = abla::next(saturated, config, 123u);
    REQUIRE(os_next.has_value());
    REQUIRE(s_next.has_value());
    // with the 2GB cap disabled the over-saturated block size is clamped to the limit
    REQUIRE(os_next->control_block_size == s_next->control_block_size);
    REQUIRE(os_next->elastic_buffer_size == s_next->elastic_buffer_size);
    REQUIRE(os_next->block_size == s_next->block_size);

    REQUIRE(abla::block_size_limit(*os_next) == abla::block_size_limit(*s_next));

    // The remainder of the BCHN case (u64max "overLimit" sizes compared against
    // the lookahead function) fails on the production lookahead; the full port is
    // kept as evidence and tracked as failed_regression
    // abla_lookahead_lags_one_block in test/bchn_non_vm/coverage.json.
}

// BCHN abla_basic_tests/state_isvalid
TEST_CASE("BCHN abla_basic_tests - state_isvalid", "[bchn_non_vm][abla]") {
    auto const config = abla::default_config();
    using v = abla::state_validity;

    abla::state state(config, 80);
    REQUIRE(abla::validate(state, config) == v::valid);

    // controlBlockSize < epsilon0
    auto const bad_cbs = config.epsilon0 - 1;
    state = make_state(80, bad_cbs, config.beta0);
    REQUIRE(state.control_block_size == bad_cbs);
    REQUIRE(abla::validate(state, config) == v::error_control_block_size);

    // controlBlockSize > epsilonMax
    auto const bad_cbs2 = config.epsilon_max + 1;
    state = make_state(80, bad_cbs2, config.beta0);
    REQUIRE(state.control_block_size == bad_cbs2);
    REQUIRE(abla::validate(state, config) == v::error_control_block_size);

    // elasticBufferSize < beta0
    auto const bad_ebs = config.beta0 - 1;
    state = make_state(80, config.epsilon0, bad_ebs);
    REQUIRE(state.elastic_buffer_size == bad_ebs);
    REQUIRE(abla::validate(state, config) == v::error_elastic_buffer_size);

    // elasticBufferSize > betaMax
    auto const bad_ebs2 = config.beta_max + 1;
    state = make_state(80, config.epsilon0, bad_ebs2);
    REQUIRE(state.elastic_buffer_size == bad_ebs2);
    REQUIRE(abla::validate(state, config) == v::error_elastic_buffer_size);
}

// BCHN abla_basic_tests/feature_fixedsize
TEST_CASE("BCHN abla_basic_tests - feature_fixedsize", "[bchn_non_vm][abla]") {
    auto const def_blk_sz = abla::DEFAULT_CONSENSUS_BLOCK_SIZE;
    auto const conf_normal = abla::default_config(def_blk_sz, false);
    auto const conf_fixed = abla::default_config(def_blk_sz, true);
    REQUIRE(valid(conf_normal));
    REQUIRE(valid(conf_fixed));
    // IsFixedSize() == (epsilon0 == epsilonMax && beta0 == betaMax)
    REQUIRE( ! (conf_normal.epsilon0 == conf_normal.epsilon_max && conf_normal.beta0 == conf_normal.beta_max));
    REQUIRE((conf_fixed.epsilon0 == conf_fixed.epsilon_max && conf_fixed.beta0 == conf_fixed.beta_max));

    abla::state const state_normal(conf_normal, 0);
    abla::state const state_fixed(conf_fixed, 0);
    REQUIRE(valid(state_normal, conf_normal));
    REQUIRE(valid(state_fixed, conf_fixed));

    // both start off with def_blk_sz as the limit
    REQUIRE(abla::block_size_limit(state_normal) == def_blk_sz);
    REQUIRE(abla::block_size_limit(state_fixed) == def_blk_sz);

    // the normal config grows if blocks are full
    auto const la_normal = abla::lookahead(state_normal, conf_normal, 2048);
    auto const la_fixed = abla::lookahead(state_fixed, conf_fixed, 2048);
    REQUIRE(la_normal.has_value());
    REQUIRE(la_fixed.has_value());
    REQUIRE(abla::block_size_limit(*la_normal) > def_blk_sz);
    // the fixed config never grows
    REQUIRE(abla::block_size_limit(*la_fixed) == def_blk_sz);
    REQUIRE(abla::block_size_limit(*la_normal) > abla::block_size_limit(*la_fixed));
}
