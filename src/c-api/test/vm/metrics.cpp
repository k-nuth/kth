// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is named .cpp solely so it can use Catch2 (which is C++).
// Everything inside the test bodies is plain C: no namespaces, no
// templates, no <chrono>, no std::*, no auto, no references, no constexpr.
// Only Catch2's TEST_CASE / REQUIRE macros are C++. The point is that
// these tests must exercise the C-API exactly the way a C consumer would.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>

#include <kth/capi/chain/script_flags.h>
#include <kth/capi/primitives.h>
#include <kth/capi/vm/metrics.h>
#include <kth/capi/vm/program.h>
#include <kth/capi/chain/script.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Constants (mirrors of `script_limits.hpp`)
// ---------------------------------------------------------------------------

// Reproduced here so the assertions spell out the expected values
// instead of depending on the implementation's private constants.
static uint64_t const kHashIterFactorConsensus = 64;     // non-standard
static uint64_t const kHashIterFactorStandard  = 192;    // 64 * 3
static uint64_t const kSigCheckCostFactor      = 26000;  // may2025::sig_check_cost_factor

// Handy anchor for script_limits computations:
//   op_cost_limit   = (n + 41) * 800
//   hash_iters_limit = ((n + 41) * factor) / 2, factor ∈ {1 consensus, 7 non-standard}
// With n = 100: op_cost_limit = 112'800, hash_iters_limit (non-std) = 493.
static uint64_t const kScriptSigSize = 100;

// ---------------------------------------------------------------------------
// Fixture — a freshly-constructed `metrics` via `program.get_metrics()`
// ---------------------------------------------------------------------------

// metrics has no explicit C-API constructor. We obtain a standalone,
// owned `kth_metrics_mut_t` by constructing a dummy program and copying
// its default-initialised metrics (copy returns an owned handle).
static kth_metrics_mut_t fresh_metrics(void) {
    uint8_t const one_push[1] = { 0x51 };
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        one_push, sizeof(one_push), 0, &script);
    REQUIRE(ec == kth_ec_success);
    kth_program_mut_t prog = kth_vm_program_construct_from_script(script);
    REQUIRE(prog != NULL);

    // `get_metrics` returns a borrowed view; copy it into an owned
    // handle so the caller can release `prog` + `script` here.
    kth_metrics_const_t borrowed = kth_vm_program_get_metrics(prog);
    REQUIRE(borrowed != NULL);
    kth_metrics_mut_t owned = kth_vm_metrics_copy(borrowed);
    REQUIRE(owned != NULL);

    kth_vm_program_destruct(prog);
    kth_chain_script_destruct(script);
    return owned;
}

// ---------------------------------------------------------------------------
// Default construction
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - default metrics: all counters zero, no script limits",
          "[C-API Metrics][lifecycle]") {
    kth_metrics_mut_t m = fresh_metrics();
    REQUIRE(kth_vm_metrics_sig_checks(m) == 0);
    REQUIRE(kth_vm_metrics_op_cost(m) == 0);
    REQUIRE(kth_vm_metrics_hash_digest_iterations(m) == 0);
    REQUIRE(kth_vm_metrics_has_valid_script_limits(m) == 0);
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - without script_limits, every is_over_*_limit is false",
          "[C-API Metrics][limits]") {
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_op_cost(m, 1000000000);
    kth_vm_metrics_add_hash_iterations(m, 1 << 20, 1);
    REQUIRE(kth_vm_metrics_is_over_op_cost_limit_simple(m) == 0);
    REQUIRE(kth_vm_metrics_is_over_op_cost_limit(m, 0) == 0);
    REQUIRE(kth_vm_metrics_is_over_hash_iters_limit(m) == 0);
    kth_vm_metrics_destruct(m);
}

// ---------------------------------------------------------------------------
// destruct(NULL) is a no-op
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - destruct(NULL) is a no-op",
          "[C-API Metrics][lifecycle]") {
    kth_vm_metrics_destruct(NULL);
}

// ---------------------------------------------------------------------------
// copy produces an independent metrics
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - copy produces an independent metrics",
          "[C-API Metrics][lifecycle]") {
    kth_metrics_mut_t a = fresh_metrics();
    kth_vm_metrics_add_op_cost(a, 500);

    kth_metrics_mut_t b = kth_vm_metrics_copy(a);
    REQUIRE(b != NULL);
    REQUIRE(kth_vm_metrics_op_cost(b) == 500);

    // Mutating the copy must not bleed back into the original.
    kth_vm_metrics_add_op_cost(b, 100);
    REQUIRE(kth_vm_metrics_op_cost(a) == 500);
    REQUIRE(kth_vm_metrics_op_cost(b) == 600);

    kth_vm_metrics_destruct(b);
    kth_vm_metrics_destruct(a);
}

// ---------------------------------------------------------------------------
// add_op_cost / add_push_op
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - add_op_cost accumulates",
          "[C-API Metrics][counters]") {
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_op_cost(m, 10);
    kth_vm_metrics_add_op_cost(m, 20);
    kth_vm_metrics_add_op_cost(m, 100);
    REQUIRE(kth_vm_metrics_op_cost(m) == 130);
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - add_push_op is an alias of add_op_cost",
          "[C-API Metrics][counters]") {
    // `add_push_op` is the BCHN-TallyPushOp-shaped alias. It must be
    // byte-for-byte equivalent to `add_op_cost`.
    kth_metrics_mut_t via_push = fresh_metrics();
    kth_vm_metrics_add_push_op(via_push, 5);
    kth_vm_metrics_add_push_op(via_push, 17);

    kth_metrics_mut_t via_op_cost = fresh_metrics();
    kth_vm_metrics_add_op_cost(via_op_cost, 5);
    kth_vm_metrics_add_op_cost(via_op_cost, 17);

    REQUIRE(kth_vm_metrics_op_cost(via_push) == kth_vm_metrics_op_cost(via_op_cost));
    REQUIRE(kth_vm_metrics_op_cost(via_push) == 22);

    kth_vm_metrics_destruct(via_op_cost);
    kth_vm_metrics_destruct(via_push);
}

// ---------------------------------------------------------------------------
// add_hash_iterations
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - add_hash_iterations: one-round on short message",
          "[C-API Metrics][hash]") {
    // iters = is_two_round + 1 + ((msg_len + 8) / 64)
    // msg_len = 0, one-round: 0 + 1 + 0 = 1
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_hash_iterations(m, 0, 0);
    REQUIRE(kth_vm_metrics_hash_digest_iterations(m) == 1);
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - add_hash_iterations: two-round on short message",
          "[C-API Metrics][hash]") {
    // msg_len = 0, two-round: 1 + 1 + 0 = 2
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_hash_iterations(m, 0, 1);
    REQUIRE(kth_vm_metrics_hash_digest_iterations(m) == 2);
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - add_hash_iterations: one-round on one block",
          "[C-API Metrics][hash]") {
    // msg_len = 56, one-round: 0 + 1 + ((56 + 8) / 64) = 2
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_hash_iterations(m, 56, 0);
    REQUIRE(kth_vm_metrics_hash_digest_iterations(m) == 2);
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - add_hash_iterations accumulates across calls",
          "[C-API Metrics][hash]") {
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_hash_iterations(m, 0, 0);    // 1
    kth_vm_metrics_add_hash_iterations(m, 0, 1);    // 2
    kth_vm_metrics_add_hash_iterations(m, 56, 0);   // 2
    REQUIRE(kth_vm_metrics_hash_digest_iterations(m) == 5);
    kth_vm_metrics_destruct(m);
}

// ---------------------------------------------------------------------------
// add_sig_checks
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - add_sig_checks accumulates",
          "[C-API Metrics][counters]") {
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_sig_checks(m, 1);
    kth_vm_metrics_add_sig_checks(m, 1);
    kth_vm_metrics_add_sig_checks(m, 15);    // e.g. 15-key multisig
    REQUIRE(kth_vm_metrics_sig_checks(m) == 17);
    kth_vm_metrics_destruct(m);
}

// ---------------------------------------------------------------------------
// composite_op_cost — bool overload (native interpreter path)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - composite_op_cost on zero metrics is zero",
          "[C-API Metrics][composite]") {
    kth_metrics_mut_t m = fresh_metrics();
    REQUIRE(kth_vm_metrics_composite_op_cost_bool(m, 0) == 0);
    REQUIRE(kth_vm_metrics_composite_op_cost_bool(m, 1) == 0);
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - composite_op_cost: sig_checks contribution only",
          "[C-API Metrics][composite]") {
    // 3 sig checks alone → 3 * 26'000 = 78'000. Standard/non-standard
    // bit is moot because hash_digest_iterations_ is 0.
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_sig_checks(m, 3);
    REQUIRE(kth_vm_metrics_composite_op_cost_bool(m, 0) == 3 * kSigCheckCostFactor);
    REQUIRE(kth_vm_metrics_composite_op_cost_bool(m, 1) == 3 * kSigCheckCostFactor);
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - composite_op_cost: hash iters weighted by 'standard'",
          "[C-API Metrics][composite]") {
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_hash_iterations(m, 0, 0);  // +1 iter
    REQUIRE(kth_vm_metrics_composite_op_cost_bool(m, 0) == 1 * kHashIterFactorConsensus); // 64
    REQUIRE(kth_vm_metrics_composite_op_cost_bool(m, 1) == 1 * kHashIterFactorStandard);  // 192
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - composite_op_cost composes op_cost + hash_iters + sig_checks",
          "[C-API Metrics][composite]") {
    // Consensus (standard==false) factor is 64, so:
    //   500 (op_cost) + 2 * 64 (hash iters) + 1 * 26'000 (sig_checks)
    //   = 500 + 128 + 26'000 = 26'628.
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_op_cost(m, 500);
    kth_vm_metrics_add_hash_iterations(m, 0, 1);   // +2 (two-round on empty msg)
    kth_vm_metrics_add_sig_checks(m, 1);
    REQUIRE(kth_vm_metrics_composite_op_cost_bool(m, 0)
            == 500 + 2 * kHashIterFactorConsensus + 1 * kSigCheckCostFactor);
    kth_vm_metrics_destruct(m);
}

// ---------------------------------------------------------------------------
// composite_op_cost — script_flags_t overload
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - composite_op_cost(flags) routes through is_vm_limits_standard",
          "[C-API Metrics][composite][flags]") {
    // With `bch_vm_limits_standard` in the flag set, the
    // script_flags_t-taking overload agrees with composite_op_cost(true).
    // Without the flag, it agrees with composite_op_cost(false).
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_add_hash_iterations(m, 0, 0);

    REQUIRE(kth_vm_metrics_composite_op_cost_script_flags(
                m, kth_script_flags_bch_vm_limits_standard)
            == kth_vm_metrics_composite_op_cost_bool(m, 1));
    REQUIRE(kth_vm_metrics_composite_op_cost_script_flags(m, 0)
            == kth_vm_metrics_composite_op_cost_bool(m, 0));
    kth_vm_metrics_destruct(m);
}

// ---------------------------------------------------------------------------
// set_script_limits / set_native_script_limits
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - set_script_limits activates has_valid_script_limits",
          "[C-API Metrics][limits]") {
    kth_metrics_mut_t m = fresh_metrics();
    REQUIRE(kth_vm_metrics_has_valid_script_limits(m) == 0);

    kth_vm_metrics_set_script_limits(m, 0, kScriptSigSize);
    REQUIRE(kth_vm_metrics_has_valid_script_limits(m) != 0);
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - set_native_script_limits picks the hash_iters ceiling from 'standard'",
          "[C-API Metrics][limits]") {
    // hash_iters_limit = ((n + 41) * factor) / 2, with factor = 1 for
    // standard and factor = 7 for non-standard (consensus). At n=100:
    //   standard     → (141 * 1) / 2 = 70
    //   non-standard → (141 * 7) / 2 = 493
    // A single `add_hash_iterations(msg_len=0, two_round=true)` adds 2
    // iters, below both ceilings. To cross only the standard ceiling we
    // pump the iters to a value between 70 and 493: msg_len=5000,
    // one-round → 1 + (5008/64) = 79, crossing 70 but not 493.
    kth_metrics_mut_t consensus = fresh_metrics();
    kth_vm_metrics_set_native_script_limits(consensus, 0, kScriptSigSize);

    kth_metrics_mut_t standard = fresh_metrics();
    kth_vm_metrics_set_native_script_limits(standard, 1, kScriptSigSize);

    kth_vm_metrics_add_hash_iterations(consensus, 5000, 0);  // +79 iters
    kth_vm_metrics_add_hash_iterations(standard, 5000, 0);   // +79 iters

    // Same counter value, different stored ceiling: only the `standard`
    // fixture crosses, proving `set_native_script_limits` persists the
    // 'standard' bit into the stored limits.
    REQUIRE(kth_vm_metrics_is_over_hash_iters_limit(consensus) == 0);
    REQUIRE(kth_vm_metrics_is_over_hash_iters_limit(standard)  != 0);

    kth_vm_metrics_destruct(standard);
    kth_vm_metrics_destruct(consensus);
}

// ---------------------------------------------------------------------------
// is_over_*_limit crossing tests
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - is_over_op_cost_limit crosses when composite > ceiling",
          "[C-API Metrics][limits]") {
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_set_native_script_limits(m, 0, kScriptSigSize);
    // op_cost_limit here is 112'800. Push op_cost right below it via
    // `add_op_cost` only (no iters, no sig checks → composite == op_cost_).
    kth_vm_metrics_add_op_cost(m, 112800);
    REQUIRE(kth_vm_metrics_is_over_op_cost_limit_simple(m) == 0);

    // One more unit tips us over.
    kth_vm_metrics_add_op_cost(m, 1);
    REQUIRE(kth_vm_metrics_is_over_op_cost_limit_simple(m) != 0);
    kth_vm_metrics_destruct(m);
}

TEST_CASE("C-API Metrics - is_over_hash_iters_limit crosses when iters > ceiling",
          "[C-API Metrics][limits]") {
    // hash_iters_limit on non-standard 100-byte scriptSig: 493.
    // For msg_len = 30'000, one-round: 0 + 1 + (30'008 / 64) = 469.
    // Two calls → 938, crossing 493.
    kth_metrics_mut_t m = fresh_metrics();
    kth_vm_metrics_set_native_script_limits(m, 0, kScriptSigSize);
    kth_vm_metrics_add_hash_iterations(m, 30000, 0);   // +469
    REQUIRE(kth_vm_metrics_is_over_hash_iters_limit(m) == 0);
    kth_vm_metrics_add_hash_iterations(m, 30000, 0);   // +469 → 938 total
    REQUIRE(kth_vm_metrics_is_over_hash_iters_limit(m) != 0);
    kth_vm_metrics_destruct(m);
}

// ---------------------------------------------------------------------------
// Preconditions — const getters abort on NULL
// ---------------------------------------------------------------------------

TEST_CASE("C-API Metrics - sig_checks null aborts",
          "[C-API Metrics][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_metrics_sig_checks(NULL));
}

TEST_CASE("C-API Metrics - op_cost null aborts",
          "[C-API Metrics][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_metrics_op_cost(NULL));
}

TEST_CASE("C-API Metrics - hash_digest_iterations null aborts",
          "[C-API Metrics][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_metrics_hash_digest_iterations(NULL));
}

TEST_CASE("C-API Metrics - copy null aborts",
          "[C-API Metrics][precondition]") {
    KTH_EXPECT_ABORT(kth_vm_metrics_copy(NULL));
}
