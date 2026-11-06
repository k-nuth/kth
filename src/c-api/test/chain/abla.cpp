// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// C-API smoke tests for `kth::domain::chain::abla` — BCH 2024 May
// Elastic Block Adjustment Algorithm. Exercises the two POD-handle
// classes (`config`, `state`), the field accessors, and the
// namespace-module free functions including the optional-by-value
// returners (`next` / `lookahead`).
//
// The numeric expectations match the algorithm's documented behaviour
// for the default config + a single-step transition; they're not
// vs-spec-vector comparisons, only round-trip + monotonicity checks.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>

#include <kth/capi/domain/chain/abla.h>
#include <kth/capi/domain/chain/abla_config.h>
#include <kth/capi/domain/chain/abla_state.h>
#include <kth/capi/chain/abla_config_validity.h>
#include <kth/capi/chain/abla_state_validity.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// config: default factory + validation
// ---------------------------------------------------------------------------

TEST_CASE("C-API Abla - default_config builds a valid config", "[C-API Abla]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000u, 0);
    REQUIRE(cfg != NULL);
    REQUIRE(kth_domain_chain_abla_validate_config(cfg) == kth_abla_config_validity_valid);
    kth_domain_chain_abla_config_destruct(cfg);
}

TEST_CASE("C-API Abla - default_config field accessors match input", "[C-API Abla]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000u, 0);
    REQUIRE(cfg != NULL);
    // From the algorithm spec: epsilon0 = beta0 = default_block_size / 2.
    REQUIRE(kth_domain_chain_abla_config_epsilon0(cfg) == 16000000u);
    REQUIRE(kth_domain_chain_abla_config_beta0(cfg) == 16000000u);
    REQUIRE(kth_domain_chain_abla_config_zeta_xB7(cfg) == 192u);
    REQUIRE(kth_domain_chain_abla_config_delta(cfg) == 10u);
    REQUIRE(kth_domain_chain_abla_config_epsilon_max(cfg) > 0u);
    REQUIRE(kth_domain_chain_abla_config_beta_max(cfg) > 0u);
    kth_domain_chain_abla_config_destruct(cfg);
}

TEST_CASE("C-API Abla - fixed_size config has epsilon_max == epsilon0", "[C-API Abla]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(8000000u, 1);
    REQUIRE(cfg != NULL);
    REQUIRE(kth_domain_chain_abla_config_epsilon_max(cfg) == kth_domain_chain_abla_config_epsilon0(cfg));
    REQUIRE(kth_domain_chain_abla_config_beta_max(cfg) == kth_domain_chain_abla_config_beta0(cfg));
    kth_domain_chain_abla_config_destruct(cfg);
}

TEST_CASE("C-API Abla - set_max recomputes epsilon_max / beta_max", "[C-API Abla]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000u, 1);
    REQUIRE(cfg != NULL);
    // Fixed-size start: epsilon_max == epsilon0.
    kth_size_t const initial_epsilon_max = kth_domain_chain_abla_config_epsilon_max(cfg);
    REQUIRE(initial_epsilon_max == kth_domain_chain_abla_config_epsilon0(cfg));
    kth_domain_chain_abla_set_max(cfg);
    // After set_max: epsilon_max grows to the 64-bit-safe ceiling.
    REQUIRE(kth_domain_chain_abla_config_epsilon_max(cfg) > initial_epsilon_max);
    REQUIRE(kth_domain_chain_abla_validate_config(cfg) == kth_abla_config_validity_valid);
    kth_domain_chain_abla_config_destruct(cfg);
}

// ---------------------------------------------------------------------------
// state: construction + accessors
// ---------------------------------------------------------------------------

TEST_CASE("C-API Abla - default state has zeros", "[C-API Abla]") {
    kth_abla_state_mut_t st = kth_domain_chain_abla_state_construct_default();
    REQUIRE(st != NULL);
    REQUIRE(kth_domain_chain_abla_state_block_size(st) == 0u);
    REQUIRE(kth_domain_chain_abla_state_control_block_size(st) == 0u);
    REQUIRE(kth_domain_chain_abla_state_elastic_buffer_size(st) == 0u);
    kth_domain_chain_abla_state_destruct(st);
}

TEST_CASE("C-API Abla - state field ctor seeds from config", "[C-API Abla]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000u, 0);
    kth_abla_state_mut_t st = kth_domain_chain_abla_state_construct(cfg, 1000000u);
    REQUIRE(st != NULL);
    REQUIRE(kth_domain_chain_abla_state_block_size(st) == 1000000u);
    REQUIRE(kth_domain_chain_abla_state_control_block_size(st) == kth_domain_chain_abla_config_epsilon0(cfg));
    REQUIRE(kth_domain_chain_abla_state_elastic_buffer_size(st) == kth_domain_chain_abla_config_beta0(cfg));
    REQUIRE(kth_domain_chain_abla_validate_state(st, cfg) == kth_abla_state_validity_valid);

    kth_domain_chain_abla_state_destruct(st);
    kth_domain_chain_abla_config_destruct(cfg);
}

TEST_CASE("C-API Abla - block_size_limit equals control + buffer", "[C-API Abla]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000u, 0);
    kth_abla_state_mut_t st = kth_domain_chain_abla_state_construct(cfg, 0);
    REQUIRE(kth_domain_chain_abla_block_size_limit(st)
            == kth_domain_chain_abla_state_control_block_size(st)
             + kth_domain_chain_abla_state_elastic_buffer_size(st));
    kth_domain_chain_abla_state_destruct(st);
    kth_domain_chain_abla_config_destruct(cfg);
}

// ---------------------------------------------------------------------------
// next / lookahead: optional-by-value return path
// ---------------------------------------------------------------------------

TEST_CASE("C-API Abla - next on quiet network keeps block size at floor", "[C-API Abla]") {
    // With block_size = epsilon0 / 2, amplified_size = zeta * x / B7 = 1.5 * x;
    // still below epsilon0, so the algo decreases control_block_size towards
    // the floor (`max(., epsilon0)`), keeping it at epsilon0 exactly.
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000u, 0);
    kth_abla_state_mut_t st = kth_domain_chain_abla_state_construct(cfg, 8000000u);
    kth_abla_state_mut_t next_st = kth_domain_chain_abla_next(st, cfg, 8000000u);
    REQUIRE(next_st != NULL);
    REQUIRE(kth_domain_chain_abla_state_control_block_size(next_st)
            == kth_domain_chain_abla_config_epsilon0(cfg));
    kth_domain_chain_abla_state_destruct(next_st);
    kth_domain_chain_abla_state_destruct(st);
    kth_domain_chain_abla_config_destruct(cfg);
}

TEST_CASE("C-API Abla - next on full block grows control_block_size", "[C-API Abla]") {
    // The algorithm reacts to the CURRENT block's size, not the
    // `next_block_size` argument — that one only seeds the future
    // state's `block_size` field for the *next* round. Construct
    // the state with `block_size = limit` (signalling the previous
    // block was full) so `next()` sees `amplified_size > epsilon0`
    // and takes the increase branch.
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000u, 0);
    kth_abla_state_mut_t st0 = kth_domain_chain_abla_state_construct(cfg, 0);
    uint64_t const initial_limit = kth_domain_chain_abla_block_size_limit(st0);
    kth_domain_chain_abla_state_destruct(st0);

    kth_abla_state_mut_t st = kth_domain_chain_abla_state_construct(cfg, initial_limit);
    kth_abla_state_mut_t next_st = kth_domain_chain_abla_next(st, cfg, initial_limit);
    REQUIRE(next_st != NULL);
    REQUIRE(kth_domain_chain_abla_state_control_block_size(next_st)
            > kth_domain_chain_abla_state_control_block_size(st));
    REQUIRE(kth_domain_chain_abla_block_size_limit(next_st) > initial_limit);
    kth_domain_chain_abla_state_destruct(next_st);
    kth_domain_chain_abla_state_destruct(st);
    kth_domain_chain_abla_config_destruct(cfg);
}

TEST_CASE("C-API Abla - lookahead 10 full blocks grows limit monotonically", "[C-API Abla]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000u, 0);
    kth_abla_state_mut_t st = kth_domain_chain_abla_state_construct(cfg, 0);
    uint64_t const initial_limit = kth_domain_chain_abla_block_size_limit(st);
    kth_abla_state_mut_t future = kth_domain_chain_abla_lookahead(st, cfg, 10);
    REQUIRE(future != NULL);
    REQUIRE(kth_domain_chain_abla_block_size_limit(future) > initial_limit);
    kth_domain_chain_abla_state_destruct(future);
    kth_domain_chain_abla_state_destruct(st);
    kth_domain_chain_abla_config_destruct(cfg);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Abla - validate_config null aborts",
          "[C-API Abla][precondition]") {
    KTH_EXPECT_ABORT(kth_domain_chain_abla_validate_config(NULL));
}

TEST_CASE("C-API Abla - next null state aborts",
          "[C-API Abla][precondition]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000u, 0);
    KTH_EXPECT_ABORT(kth_domain_chain_abla_next(NULL, cfg, 0));
    kth_domain_chain_abla_config_destruct(cfg);
}

TEST_CASE("C-API Abla - state_construct null config aborts",
          "[C-API Abla][precondition]") {
    KTH_EXPECT_ABORT(kth_domain_chain_abla_state_construct(NULL, 0));
}

#if defined(KTH_CURRENCY_BCH)
#include <kth/capi/domain/chain/block_ticks.h>

TEST_CASE("C-API ABLA retains and updates the target interval", "[C-API Abla][ticks]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000, 0);
    REQUIRE(cfg != NULL);
    kth_abla_state_mut_t legacy = kth_domain_chain_abla_state_construct(cfg, 0);
    REQUIRE(legacy != NULL);
    CHECK(kth_domain_chain_abla_block_size_limit(legacy) == 32000000);
    kth_abla_state_mut_t short_state = kth_domain_chain_abla_state_construct_with_interval(cfg, 0, 60);
    REQUIRE(short_state != NULL);
    CHECK(kth_domain_chain_abla_state_block_interval(short_state) == 60);
    kth_domain_chain_abla_state_set_block_interval(legacy, 60);
    CHECK(kth_domain_chain_abla_block_size_limit(short_state) == 3200000);
    CHECK(kth_domain_chain_abla_block_size_limit(legacy) == 3200000);
    kth_domain_chain_abla_state_destruct(short_state);
    kth_domain_chain_abla_state_destruct(legacy);
    kth_domain_chain_abla_config_destruct(cfg);
}

TEST_CASE("C-API ABLA carries the next interval and checks limit overflow", "[C-API Abla][ticks]") {
    kth_abla_config_mut_t cfg = kth_domain_chain_abla_default_config(32000000, 0);
    REQUIRE(cfg != NULL);
    kth_abla_state_mut_t previous = kth_domain_chain_abla_state_construct(cfg, 0);
    REQUIRE(previous != NULL);
    kth_abla_state_mut_t next = kth_domain_chain_abla_next_with_interval(previous, cfg, 0, 60);
    REQUIRE(next != NULL);
    CHECK(kth_domain_chain_abla_state_block_interval(previous) == 600);
    CHECK(kth_domain_chain_abla_state_block_interval(next) == 60);
    uint64_t limit = 17;
    REQUIRE(kth_domain_chain_abla_block_size_limit_with_interval(previous, 60, &limit));
    CHECK(limit == kth_domain_chain_abla_block_size_limit(next));
    kth_abla_state_mut_t future = kth_domain_chain_abla_lookahead(next, cfg, 1);
    REQUIRE(future != NULL);
    CHECK(kth_domain_chain_abla_state_block_interval(future) == 60);
    CHECK(kth_abla_state_validity_valid == 0);
    CHECK(kth_abla_state_validity_error_control_block_size == 1);
    CHECK(kth_abla_state_validity_error_elastic_buffer_size == 2);
    CHECK(kth_abla_state_validity_error_block_interval == 3);
    kth_domain_chain_abla_state_set_block_interval(next, 0);
    CHECK(kth_domain_chain_abla_validate_state(next, cfg) == kth_abla_state_validity_error_block_interval);
    kth_domain_chain_abla_state_set_control_block_size(previous, UINT64_MAX);
    kth_domain_chain_abla_state_set_elastic_buffer_size(previous, 0);
    limit = 17;
    CHECK_FALSE(kth_domain_chain_abla_block_size_limit_with_interval(previous, 601, &limit));
    CHECK(limit == 17);
    kth_domain_chain_abla_state_destruct(future);
    kth_domain_chain_abla_state_destruct(next);
    kth_domain_chain_abla_state_destruct(previous);
    kth_domain_chain_abla_config_destruct(cfg);
}
#endif
