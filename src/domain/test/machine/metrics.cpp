// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/domain/machine/metrics.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/domain/machine/script_limits.hpp>

using namespace kth;
using namespace kth::domain::machine;

namespace {

// Constants from `script_limits.hpp` reproduced here so the assertions
// below spell out the expected values instead of depending on the
// implementation's private constants.
constexpr uint64_t hash_iter_factor_consensus = 64;     // non-standard
constexpr uint64_t hash_iter_factor_standard  = 192;    // 64 * 3
constexpr uint64_t sig_check_cost_factor_val  = 26'000; // may2025::sig_check_cost_factor

// Handy anchor for script_limits computations: `op_cost_limit = (n + 41) * 800`,
// `hash_iters_limit = ((n + 41) * factor) / 2` with factor ∈ {1 consensus, 7 non-standard}.
constexpr uint64_t kScriptSigSize = 100; // → op_cost_limit = 141 * 800 = 112'800

} // namespace

// ---------------------------------------------------------------------------
// Default construction
// ---------------------------------------------------------------------------

TEST_CASE("default metrics: all counters zero, no script limits", "[metrics]") {
    metrics m;
    REQUIRE(m.sig_checks() == 0);
    REQUIRE(m.op_cost() == 0);
    REQUIRE(m.hash_digest_iterations() == 0);
    REQUIRE(m.has_valid_script_limits() == false);
}

TEST_CASE("without script_limits, every is_over_*_limit is false", "[metrics]") {
    // Before `set_script_limits` is called, the limit predicates must
    // short-circuit on the absent `script_limits_` — otherwise a caller
    // that forgot to initialise limits would silently over-report limit
    // violations based on an uninitialised ceiling.
    metrics m;
    m.add_op_cost(1'000'000'000);
    m.add_hash_iterations(1 << 20, true);
    REQUIRE(m.is_over_op_cost_limit() == false);
    REQUIRE(m.is_over_op_cost_limit(0) == false);
    REQUIRE(m.is_over_hash_iters_limit() == false);
}

// ---------------------------------------------------------------------------
// add_op_cost / add_push_op (semantic alias)
// ---------------------------------------------------------------------------

TEST_CASE("add_op_cost accumulates", "[metrics]") {
    metrics m;
    m.add_op_cost(10);
    m.add_op_cost(20);
    m.add_op_cost(100);
    REQUIRE(m.op_cost() == 130);
}

TEST_CASE("add_push_op is an alias of add_op_cost", "[metrics]") {
    // `add_push_op` is the BCHN-TallyPushOp-shaped alias that exists for
    // documentation purposes in the interpreter. It must be
    // byte-for-byte equivalent to `add_op_cost` — verify by comparing
    // two fresh instances driven through equivalent sequences.
    metrics via_push;
    via_push.add_push_op(5);
    via_push.add_push_op(17);

    metrics via_op_cost;
    via_op_cost.add_op_cost(5);
    via_op_cost.add_op_cost(17);

    REQUIRE(via_push.op_cost() == via_op_cost.op_cost());
    REQUIRE(via_push.op_cost() == 22);
}

// ---------------------------------------------------------------------------
// add_hash_iterations
// ---------------------------------------------------------------------------

TEST_CASE("add_hash_iterations: one-round hasher on short message", "[metrics]") {
    // Formula (script_limits.hpp): iters = is_two_round + 1 + ((msg_len + 8) / 64)
    // For msg_len = 0, one-round: 0 + 1 + (8 / 64) = 1
    metrics m;
    m.add_hash_iterations(0, false);
    REQUIRE(m.hash_digest_iterations() == 1);
}

TEST_CASE("add_hash_iterations: two-round hasher on short message", "[metrics]") {
    // For msg_len = 0, two-round: 1 + 1 + (8 / 64) = 2
    metrics m;
    m.add_hash_iterations(0, true);
    REQUIRE(m.hash_digest_iterations() == 2);
}

TEST_CASE("add_hash_iterations: one-round hasher on one block of message", "[metrics]") {
    // For msg_len = 56 (just enough to fill 64 bytes with the 8-byte
    // length suffix), one-round: 0 + 1 + ((56 + 8) / 64) = 2.
    metrics m;
    m.add_hash_iterations(56, false);
    REQUIRE(m.hash_digest_iterations() == 2);
}

TEST_CASE("add_hash_iterations accumulates across calls", "[metrics]") {
    metrics m;
    m.add_hash_iterations(0, false);   // 1
    m.add_hash_iterations(0, true);    // 2
    m.add_hash_iterations(56, false);  // 2
    REQUIRE(m.hash_digest_iterations() == 5);
}

// ---------------------------------------------------------------------------
// add_sig_checks
// ---------------------------------------------------------------------------

TEST_CASE("add_sig_checks accumulates", "[metrics]") {
    metrics m;
    m.add_sig_checks(1);
    m.add_sig_checks(1);
    m.add_sig_checks(15);    // e.g. 15-key multisig
    REQUIRE(m.sig_checks() == 17);
}

// ---------------------------------------------------------------------------
// composite_op_cost — bool overload (native interpreter path)
// ---------------------------------------------------------------------------

TEST_CASE("composite_op_cost on zero metrics is zero", "[metrics]") {
    metrics m;
    REQUIRE(m.composite_op_cost(false) == 0);
    REQUIRE(m.composite_op_cost(true) == 0);
}

TEST_CASE("composite_op_cost contribution from sig_checks only", "[metrics]") {
    // Isolating the sig-check contribution: 3 sig checks with
    // everything else zero → 3 * 26'000 = 78'000. Factor for
    // hash iters is moot because hash_digest_iterations_ is 0.
    metrics m;
    m.add_sig_checks(3);
    REQUIRE(m.composite_op_cost(false) == 3 * sig_check_cost_factor_val);
    REQUIRE(m.composite_op_cost(true)  == 3 * sig_check_cost_factor_val);
}

TEST_CASE("composite_op_cost contribution from hash iters varies with standard flag", "[metrics]") {
    metrics m;
    m.add_hash_iterations(0, false);  // +1 hash_digest_iteration
    REQUIRE(m.composite_op_cost(false) == 1 * hash_iter_factor_consensus); // 64
    REQUIRE(m.composite_op_cost(true)  == 1 * hash_iter_factor_standard);  // 192
}

TEST_CASE("composite_op_cost composes op_cost + hash_iters + sig_checks", "[metrics]") {
    // All three contributors at once. Consensus (standard==false)
    // factor is 64, so: 500 (op_cost) + 2 * 64 (hash iters) + 1 * 26'000
    // (sig_checks) = 500 + 128 + 26'000 = 26'628.
    metrics m;
    m.add_op_cost(500);
    m.add_hash_iterations(0, true);   // +2 (two-round on empty msg)
    m.add_sig_checks(1);
    REQUIRE(m.composite_op_cost(false)
            == 500 + 2 * hash_iter_factor_consensus + 1 * sig_check_cost_factor_val);
}

// ---------------------------------------------------------------------------
// composite_op_cost — script_flags_t overload
// ---------------------------------------------------------------------------

TEST_CASE("composite_op_cost(flags) routes through is_vm_limits_standard", "[metrics]") {
    // With `bch_vm_limits_standard` in the flag set, the
    // `script_flags_t`-taking overload must agree with
    // `composite_op_cost(true)`. Without the flag, it must agree with
    // `composite_op_cost(false)`.
    metrics m;
    m.add_hash_iterations(0, false);

    REQUIRE(m.composite_op_cost(script_flags::bch_vm_limits_standard)
            == m.composite_op_cost(true));
    REQUIRE(m.composite_op_cost(script_flags_t{0})
            == m.composite_op_cost(false));
}

// ---------------------------------------------------------------------------
// is_vm_limits_standard (free function)
// ---------------------------------------------------------------------------

TEST_CASE("is_vm_limits_standard detects the policy bit", "[metrics][flags]") {
    REQUIRE(is_vm_limits_standard(0) == false);
    REQUIRE(is_vm_limits_standard(script_flags::bch_vm_limits_standard) == true);
    // Any unrelated bit set on its own must not spuriously flip the flag.
    REQUIRE(is_vm_limits_standard(script_flags::bch_bitwise_ops) == false);
    // But combined with the real flag, the predicate stays true.
    REQUIRE(is_vm_limits_standard(script_flags::bch_vm_limits_standard
                                | script_flags::bch_bitwise_ops) == true);
}

// ---------------------------------------------------------------------------
// set_script_limits / set_native_script_limits
// ---------------------------------------------------------------------------

TEST_CASE("set_script_limits activates the ceiling derived from flags+scriptsig", "[metrics]") {
    metrics m;
    REQUIRE(m.has_valid_script_limits() == false);

    m.set_script_limits(script_flags_t{0}, kScriptSigSize);
    REQUIRE(m.has_valid_script_limits() == true);

    // op_cost_limit on a consensus-mode 100-byte scriptSig:
    // (100 + 41) * 800 = 112'800.
    auto const& limits = m.get_script_limits();
    REQUIRE(limits.has_value());
    REQUIRE(limits->op_cost_limit() == 112'800);
}

TEST_CASE("set_native_script_limits picks factor 192 vs 64 based on 'standard' bool", "[metrics]") {
    metrics consensus;
    consensus.set_native_script_limits(false, kScriptSigSize);
    consensus.add_hash_iterations(0, false);  // +1 iter

    metrics standard;
    standard.set_native_script_limits(true, kScriptSigSize);
    standard.add_hash_iterations(0, false);   // +1 iter

    // Same input, different factor: standard weighs the iter 3x.
    REQUIRE(consensus.composite_op_cost(false) == hash_iter_factor_consensus);
    REQUIRE(standard.composite_op_cost(true)   == hash_iter_factor_standard);
}

// ---------------------------------------------------------------------------
// is_over_op_cost_limit / is_over_hash_iters_limit
// ---------------------------------------------------------------------------

TEST_CASE("is_over_op_cost_limit crosses when the composite exceeds the ceiling", "[metrics]") {
    metrics m;
    m.set_native_script_limits(false, kScriptSigSize);
    // op_cost_limit here is 112'800. Push op_cost right below it via
    // `add_op_cost` only (no iters, no sig checks → composite == op_cost_).
    m.add_op_cost(112'800);
    REQUIRE(m.is_over_op_cost_limit() == false);

    // One more unit tips us over.
    m.add_op_cost(1);
    REQUIRE(m.is_over_op_cost_limit() == true);
}

TEST_CASE("is_over_hash_iters_limit crosses when iters exceed the ceiling", "[metrics]") {
    // hash_iters_limit on non-standard (bonus 7x) 100-byte scriptSig:
    // ((100 + 41) * 7) / 2 = 987 / 2 = 493.
    metrics m;
    m.set_native_script_limits(false, kScriptSigSize);
    // `add_hash_iterations(msg_len=0, two_round=true)` adds 2 per call.
    // To reach exactly 494 iterations we call add_hash_iterations 247
    // times — but that's churn. Drive the internal counter directly
    // via two-round calls to cross 493 in one step: 247 two-round
    // calls = 494 iters. Easier: one call with a large msg_len.
    //
    // For msg_len = 30'000, one-round: 0 + 1 + (30'008 / 64) = 470.
    // We want 494 iters to cross 493. Go bigger: msg_len = 31'552,
    // one-round: 0 + 1 + (31'560 / 64) = 494. Crosses.
    m.add_hash_iterations(30'000, false);   // +470
    REQUIRE(m.is_over_hash_iters_limit() == false);
    m.add_hash_iterations(30'000, false);   // +470 → 940 total
    REQUIRE(m.is_over_hash_iters_limit() == true);
}
