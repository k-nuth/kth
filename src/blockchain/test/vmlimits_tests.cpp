// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/domain/machine/interpreter.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/metrics.hpp>
#include <kth/domain/machine/script_limits.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>

#ifdef WITH_CONSENSUS
#include <kth/consensus/export.hpp>
#endif

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace kth;
using namespace kth::domain;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

namespace {

using data_chunk = kth::data_chunk;
using data_stack = kth::data_stack;

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Construct a script from raw bytes.
script make_script(data_chunk const& bytes) {
    byte_reader reader(bytes);
    auto result = script::from_data(reader, false);
    return result ? std::move(*result) : script{};
}

// Construct a script from a human-readable string (e.g. "1 [pubkey] checksig").
script parse_script(std::string const& text) {
    script s;
    s.from_string(text);
    return s;
}

// ─── Native interpreter (kth) ────────────────────────────────────────────────

struct eval_result {
    code ec;
    int sig_checks;
    int64_t hash_iters;
    int64_t op_cost;                // base op_cost (without hash/sig composite)
    int64_t composite_op_cost_std;
    int64_t composite_op_cost_nonstd;
    data_stack stack;
};

eval_result eval_native(data_stack initial_stack, script const& scr, script_flags_t flags) {
    static chain::transaction const dummy_tx;
    program prog(scr, dummy_tx, 0, flags, std::move(initial_stack), max_uint64);
    auto const ec = interpreter::run(prog);
    auto& m = prog.get_metrics();

    int64_t const std_cost = m.op_cost()
        + m.hash_digest_iterations() * machine::may2025::hash_iter_op_cost_factor(true)
        + int64_t(m.sig_checks()) * ::kth::may2025::sig_check_cost_factor;
    int64_t const nonstd_cost = m.op_cost()
        + m.hash_digest_iterations() * machine::may2025::hash_iter_op_cost_factor(false)
        + int64_t(m.sig_checks()) * ::kth::may2025::sig_check_cost_factor;

    // Copy stack bottom-to-top (item(size-1) is bottom, item(0) is top).
    data_stack result_stack;
    for (size_t i = prog.size(); i > 0; --i) {
        result_stack.push_back(prog.item(i - 1));
    }

    return {
        ec.error,  // bridge op_result → code via error_code_t
        static_cast<int>(m.sig_checks()),
        static_cast<int64_t>(m.hash_digest_iterations()),
        static_cast<int64_t>(m.op_cost()),
        std_cost,
        nonstd_cost,
        std::move(result_stack),
    };
}

// ─── BCHN consensus interpreter ──────────────────────────────────────────────

#ifdef WITH_CONSENSUS

using namespace kth::consensus;

// Map kth forks to consensus verify_flags for BCHN.
unsigned int script_flags_to_verify_flags(script_flags_t active_flags, bool standard) {
    unsigned int flags = 0;
    if (active_flags & script_flags::bch_vm_limits) {
        flags |= verify_flags_enable_may2025;
        if (standard) {
            flags |= verify_flags_enable_vm_limits_standard;
        }
    }
    if (active_flags & script_flags::bch_minimaldata)          { flags |= verify_flags_minimaldata; }
    if (active_flags & script_flags::bch_64bit_integers)       { flags |= verify_flags_64_bit_integers; }
    if (active_flags & script_flags::bch_native_introspection) { flags |= verify_flags_native_introspection; }
    if (active_flags & script_flags::bch_p2sh_32)              { flags |= verify_flags_enable_p2sh_32; }
    if (active_flags & script_flags::bch_tokens)               { flags |= verify_flags_enable_tokens; }
    if (active_flags & script_flags::bch_enforce_sigchecks)    { flags |= verify_flags_enforce_sigchecks; }
    if (active_flags & script_flags::bch_input_sigchecks)      { flags |= verify_flags_input_sigchecks; }
    return flags;
}

struct consensus_result {
    verify_result_type result;
    int sig_checks;
    int64_t op_cost;
    int64_t hash_iters;
    data_stack stack;
};

consensus_result eval_consensus(data_stack initial_stack, script const& scr, unsigned int flags) {
    auto const script_bytes = scr.to_data(false);
    auto stack = initial_stack;
    script_eval_metrics metrics{};
    auto const result = eval_script_with_metrics(
        script_bytes.data(), script_bytes.size(),
        flags, stack, metrics);
    return {result, metrics.sig_checks, metrics.op_cost, metrics.hash_digest_iterations, std::move(stack)};
}

#endif // WITH_CONSENSUS

// ─── Unified check: native + consensus (when available) ──────────────────────

// Run kth native interpreter and check metrics against expected values.
// When WITH_CONSENSUS is defined, also run BCHN and cross-check both VMs.
void check_eval(data_stack const& initial, script const& scr, script_flags_t flags,
                std::optional<data_stack> const& expected_stack,
                int expected_sig_checks, int64_t expected_hash_iters,
                int64_t expected_op_cost_std,
                int64_t expected_op_cost_nonstd = -1) {

    if (expected_op_cost_nonstd < 0) expected_op_cost_nonstd = expected_op_cost_std;

    // --- kth native ---
    auto [ec, n_sc, n_hi, n_oc, n_std, n_nonstd, n_stack] = eval_native(initial, scr, flags);
    if (expected_stack) {
        CHECK(n_stack == *expected_stack);
    }
    CHECK(n_sc == expected_sig_checks);
    CHECK(n_hi == expected_hash_iters);
    CHECK(n_std == expected_op_cost_std);
    CHECK(n_nonstd == expected_op_cost_nonstd);

#ifdef WITH_CONSENSUS
    // --- BCHN consensus (standard) ---
    auto const flags_std = script_flags_to_verify_flags(flags, true);
    auto [c_result_s, c_sc_s, c_oc_s, c_hi_s, c_stack_s] = eval_consensus(initial, scr, flags_std);
    if (expected_stack) {
        CHECK(c_stack_s == *expected_stack);
    }
    CHECK(c_sc_s == expected_sig_checks);
    CHECK(c_hi_s == expected_hash_iters);
    int64_t const c_composite_std = c_oc_s + c_hi_s * 192 + int64_t(c_sc_s) * 26'000;
    CHECK(c_composite_std == expected_op_cost_std);

    // --- BCHN consensus (non-standard) ---
    auto const flags_nonstd = script_flags_to_verify_flags(flags, false);
    auto [c_result_n, c_sc_n, c_oc_n, c_hi_n, c_stack_n] = eval_consensus(initial, scr, flags_nonstd);
    if (expected_stack) {
        CHECK(c_stack_n == *expected_stack);
    }
    CHECK(c_sc_n == expected_sig_checks);
    CHECK(c_hi_n == expected_hash_iters);
    int64_t const c_composite_nonstd = c_oc_n + c_hi_n * 64 + int64_t(c_sc_n) * 26'000;
    CHECK(c_composite_nonstd == expected_op_cost_nonstd);

    // --- Cross-check: native vs consensus ---
    CHECK(n_sc == c_sc_s);
    CHECK(n_hi == c_hi_s);
    CHECK(n_oc == c_oc_s);
    CHECK(n_stack == c_stack_s);
#endif // WITH_CONSENSUS
}

} // namespace

// ─── Test: digest_iterations_sanity ──────────────────────────────────────────
// From: https://github.com/bitjson/bch-vm-limits, section: "Digest Iteration Count Test Vectors"

TEST_CASE("VM-Limits: digest iterations sanity", "[vm][vmlimits]") {
    struct test_vector {
        uint32_t msg_len;
        uint64_t expected_iters;
    };

    constexpr test_vector vectors[] = {
        {0, 1},
        {1, 1},
        {55, 1},
        {56, 2},
        {64, 2},
        {119, 2},
        {120, 3},
        {183, 3},
        {184, 4},
        {247, 4},
        {248, 5},
        {488, 8},
        {503, 8},
        {504, 9},
        {520, 9},
        {1015, 16},
        {1016, 17},
        {63928, 1000},
        {63991, 1000},
        {63992, 1001},
    };

    for (auto const& [msg_len, expected] : vectors) {
        CAPTURE(msg_len);
        CHECK(machine::may2025::calculate_hash_iters(msg_len, false) == expected);
        CHECK(machine::may2025::calculate_hash_iters(msg_len, true) == expected + 1);
    }
}

// ─── Test: individual opcode cost counts ─────────────────────────────────────
// Ported from BCHN's test_individual_opcode_counts.
// Tests that each opcode produces the expected sigChecks, hashIters, and opCost.
// Expected values come from BCHN's reference implementation.
//
// When WITH_CONSENSUS is defined, we also run through BCHN's interpreter
// and cross-check all metrics against the native interpreter.

TEST_CASE("VM-Limits: individual opcode costs — push operations", "[vm][vmlimits]") {
    // Mirrors BCHN test_individual_opcode_counts flags.
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_native_introspection
        | script_flags::bch_p2sh_32 | script_flags::bch_tokens
        | script_flags::bch_minimaldata | script_flags::bch_input_sigchecks
        | script_flags::bch_enforce_sigchecks;

    SECTION("OP_0")  { check_eval({}, parse_script("0"), flags, data_stack{{}}, 0, 0, 100); }
    SECTION("OP_1")  { check_eval({}, parse_script("1"), flags, data_stack{{0x01}}, 0, 0, 101); }
    SECTION("OP_2")  { check_eval({}, parse_script("2"), flags, data_stack{{0x02}}, 0, 0, 101); }
    SECTION("OP_16") { check_eval({}, parse_script("16"), flags, data_stack{{0x10}}, 0, 0, 101); }
    SECTION("push 1 byte") {
        data_chunk one_byte(1);
        check_eval({}, make_script({0x01, 0x00}), flags, data_stack{one_byte}, 0, 0, 101);
    }
    SECTION("push 75 bytes") {
        data_chunk seventy_five(75, 0xef);
        data_chunk push_75;
        push_75.push_back(75);
        push_75.insert(push_75.end(), seventy_five.begin(), seventy_five.end());
        check_eval({}, make_script(push_75), flags, data_stack{seventy_five}, 0, 0, 175);
    }
    SECTION("NOP") { check_eval({}, parse_script("nop"), flags, data_stack{}, 0, 0, 100); }
}

TEST_CASE("VM-Limits: individual opcode costs — stack manipulation", "[vm][vmlimits]") {
    // Mirrors BCHN test_individual_opcode_counts flags.
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_native_introspection
        | script_flags::bch_p2sh_32 | script_flags::bch_tokens
        | script_flags::bch_minimaldata | script_flags::bch_input_sigchecks
        | script_flags::bch_enforce_sigchecks;

    data_chunk const v1 = {0x01};
    data_chunk const v2 = {0x02};
    data_chunk const v3 = {0x03};
    data_chunk const v4 = {0x04};

    SECTION("2DROP") { check_eval({v1, v2}, parse_script("2drop"), flags, data_stack{}, 0, 0, 100); }
    SECTION("2DUP")  { check_eval({v1, v2}, parse_script("2dup"), flags, data_stack{v1, v2, v1, v2}, 0, 0, 102); }
    SECTION("3DUP")  { check_eval({v1, v2, v3}, parse_script("3dup"), flags, data_stack{v1, v2, v3, v1, v2, v3}, 0, 0, 103); }
    SECTION("2OVER") { check_eval({v1, v2, v3, v4}, parse_script("2over"), flags, data_stack{v1, v2, v3, v4, v1, v2}, 0, 0, 102); }
    SECTION("2SWAP") { check_eval({v1, v2, v3, v4}, parse_script("2swap"), flags, data_stack{v3, v4, v1, v2}, 0, 0, 100); }
    SECTION("DUP")   { check_eval(data_stack{{0xd0}}, parse_script("dup"), flags, data_stack{{0xd0}, {0xd0}}, 0, 0, 101); }
    SECTION("OVER")  { check_eval({v1, v2, v3}, parse_script("over"), flags, data_stack{v1, v2, v3, v2}, 0, 0, 101); }
    SECTION("ROT")   { check_eval({v1, v2, v3}, parse_script("rot"), flags, data_stack{v2, v3, v1}, 0, 0, 100); }
    SECTION("SWAP")  { check_eval({v1, v2, v3}, parse_script("swap"), flags, data_stack{v1, v3, v2}, 0, 0, 100); }
    SECTION("TUCK")  { check_eval({v2, v3}, parse_script("tuck"), flags, data_stack{v3, v2, v3}, 0, 0, 101); }
    SECTION("DROP")  { check_eval({v1}, parse_script("drop"), flags, data_stack{}, 0, 0, 100); }
    SECTION("NIP")   { check_eval({v1, v2, v3}, parse_script("nip"), flags, data_stack{v1, v3}, 0, 0, 100); }
    SECTION("DEPTH") {
        data_stack big(999, v1);
        auto expected = big;
        expected.push_back({0xe7, 0x03});
        check_eval(big, parse_script("depth"), flags, expected, 0, 0, 102);
    }
    SECTION("TOALTSTACK + FROMALTSTACK") {
        check_eval({v1}, parse_script("toaltstack fromaltstack"), flags, data_stack{v1}, 0, 0, 201);
    }
    SECTION("IFDUP false") { check_eval(data_stack{{}}, parse_script("ifdup"), flags, data_stack{{}}, 0, 0, 100); }
    SECTION("IFDUP true")  { check_eval({v2}, parse_script("ifdup"), flags, data_stack{v2, v2}, 0, 0, 101); }
}

TEST_CASE("VM-Limits: individual opcode costs — hash operations", "[vm][vmlimits]") {
    // Mirrors BCHN test_individual_opcode_counts flags.
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_native_introspection
        | script_flags::bch_p2sh_32 | script_flags::bch_tokens
        | script_flags::bch_minimaldata | script_flags::bch_input_sigchecks
        | script_flags::bch_enforce_sigchecks;
    data_chunk const blob100(100, 0xaa);

    // hash_iters for 100-byte message: single-round = 1 + (100+8)/64 = 2, two-round = 3
    SECTION("RIPEMD160") { check_eval(data_stack{blob100}, parse_script("ripemd160"), flags, std::nullopt, 0, 2, 120 + 192*2, 120 + 64*2); }
    SECTION("SHA1")      { check_eval(data_stack{blob100}, parse_script("sha1"),      flags, std::nullopt, 0, 2, 120 + 192*2, 120 + 64*2); }
    SECTION("SHA256")    { check_eval(data_stack{blob100}, parse_script("sha256"),    flags, std::nullopt, 0, 2, 132 + 192*2, 132 + 64*2); }
    SECTION("HASH160")   { check_eval(data_stack{blob100}, parse_script("hash160"),   flags, std::nullopt, 0, 3, 120 + 192*3, 120 + 64*3); }
    SECTION("HASH256")   { check_eval(data_stack{blob100}, parse_script("hash256"),   flags, std::nullopt, 0, 3, 132 + 192*3, 132 + 64*3); }
}

TEST_CASE("VM-Limits: individual opcode costs — arithmetic", "[vm][vmlimits]") {
    // Mirrors BCHN test_individual_opcode_counts flags.
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_native_introspection
        | script_flags::bch_p2sh_32 | script_flags::bch_tokens
        | script_flags::bch_minimaldata | script_flags::bch_input_sigchecks
        | script_flags::bch_enforce_sigchecks;

    SECTION("1ADD")      { check_eval(data_stack{{0x00, 0x02, 0x03, 0x42}}, parse_script("1add"), flags, data_stack{{0x01, 0x02, 0x03, 0x42}}, 0, 0, 100 + 4*2); }
    SECTION("1SUB")      { check_eval(data_stack{{0x01, 0x02, 0x03, 0x42}}, parse_script("1sub"), flags, data_stack{{0x00, 0x02, 0x03, 0x42}}, 0, 0, 100 + 4*2); }
    SECTION("NEGATE")    { check_eval(data_stack{{0x2a}}, parse_script("negate"), flags, data_stack{{0xaa}}, 0, 0, 100 + 1*2); }
    SECTION("ABS")       { check_eval(data_stack{{0xd2, 0x76, 0x86}}, parse_script("abs"), flags, data_stack{{0xd2, 0x76, 0x06}}, 0, 0, 100 + 3*2); }
    SECTION("NOT true")  { check_eval(data_stack{{}}, parse_script("not"), flags, data_stack{{0x01}}, 0, 0, 101); }
    SECTION("NOT false") { check_eval(data_stack{{0x01}}, parse_script("not"), flags, data_stack{{}}, 0, 0, 100); }
    SECTION("ADD")       { check_eval(data_stack{{0x88, 0x76, 0x0f}, {0xba, 0x7d}}, parse_script("add"), flags, data_stack{{0x42, 0xf4, 0x0f}}, 0, 0, 100 + 3*2); }
    SECTION("SUB")       { check_eval(data_stack{{0x88, 0x76, 0x0f}, {0xba, 0x7d}}, parse_script("sub"), flags, data_stack{{0xce, 0xf8, 0x0e}}, 0, 0, 100 + 3*2); }
    SECTION("BOOLAND true")  { check_eval(data_stack{{0x01}, {0x01}}, parse_script("booland"), flags, data_stack{{0x01}}, 0, 0, 101); }
    SECTION("BOOLAND false") { check_eval(data_stack{{0x01}, {}}, parse_script("booland"), flags, data_stack{{}}, 0, 0, 100); }
    SECTION("BOOLOR true")   { check_eval(data_stack{{0x01}, {}}, parse_script("boolor"), flags, data_stack{{0x01}}, 0, 0, 101); }
    SECTION("BOOLOR false")  { check_eval(data_stack{{}, {}}, parse_script("boolor"), flags, data_stack{{}}, 0, 0, 100); }
    SECTION("NUMEQUAL true")    { check_eval(data_stack{{0x12, 0x34}, {0x12, 0x34}}, parse_script("numequal"), flags, data_stack{{0x01}}, 0, 0, 101); }
    SECTION("NUMEQUAL false")   { check_eval(data_stack{{0x12, 0x34}, {0x02, 0x34}}, parse_script("numequal"), flags, data_stack{{}}, 0, 0, 100); }
    SECTION("LESSTHAN true")    { check_eval(data_stack{{0x02, 0x34}, {0x12, 0x34}}, parse_script("lessthan"), flags, data_stack{{0x01}}, 0, 0, 101); }
    SECTION("LESSTHAN false")   { check_eval(data_stack{{0x12, 0x34}, {0x02, 0x34}}, parse_script("lessthan"), flags, data_stack{{}}, 0, 0, 100); }
    SECTION("GREATERTHAN true") { check_eval(data_stack{{0x12, 0x34}, {0x02, 0x34}}, parse_script("greaterthan"), flags, data_stack{{0x01}}, 0, 0, 101); }
    SECTION("MAX") { check_eval(data_stack{{0x22, 0x34}, {0x12, 0x34}}, parse_script("max"), flags, data_stack{{0x22, 0x34}}, 0, 0, 100 + 2*2); }
    SECTION("MIN") { check_eval(data_stack{{0x22, 0x34}, {0x12, 0x34}}, parse_script("min"), flags, data_stack{{0x12, 0x34}}, 0, 0, 100 + 2*2); }
}

TEST_CASE("VM-Limits: individual opcode costs — splice operations", "[vm][vmlimits]") {
    // Mirrors BCHN test_individual_opcode_counts flags.
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_native_introspection
        | script_flags::bch_p2sh_32 | script_flags::bch_tokens
        | script_flags::bch_minimaldata | script_flags::bch_input_sigchecks
        | script_flags::bch_enforce_sigchecks;

    SECTION("CAT")   { check_eval(data_stack{{0xde, 0xad}, {0xbe, 0xef}}, parse_script("cat"), flags, data_stack{{0xde, 0xad, 0xbe, 0xef}}, 0, 0, 104); }
    SECTION("SPLIT") { check_eval(data_stack{{0xde, 0xad, 0xbe, 0xef}, {0x02}}, parse_script("split"), flags, data_stack{{0xde, 0xad}, {0xbe, 0xef}}, 0, 0, 104); }
    SECTION("SIZE")  {
        data_chunk const blob(127, 0xfa);
        check_eval(data_stack{blob}, parse_script("size"), flags, data_stack{blob, {0x7f}}, 0, 0, 101);
    }
    SECTION("REVERSEBYTES") { check_eval(data_stack{{0xab, 0xcd, 0xef, 0x01, 0x23, 0x45}}, parse_script("reversebytes"), flags, data_stack{{0x45, 0x23, 0x01, 0xef, 0xcd, 0xab}}, 0, 0, 106); }
}

TEST_CASE("VM-Limits: individual opcode costs — control flow", "[vm][vmlimits]") {
    // Mirrors BCHN test_individual_opcode_counts flags.
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_native_introspection
        | script_flags::bch_p2sh_32 | script_flags::bch_tokens
        | script_flags::bch_minimaldata | script_flags::bch_input_sigchecks
        | script_flags::bch_enforce_sigchecks;

    SECTION("IF branch not taken")    { check_eval(data_stack{{}}, parse_script("if 1 endif"), flags, data_stack{}, 0, 0, 300); }
    SECTION("IF branch taken")        { check_eval(data_stack{{0x01}}, parse_script("if [fafa] endif"), flags, data_stack{{0xfa, 0xfa}}, 0, 0, 302); }
    SECTION("IF/ELSE, else taken")    { check_eval(data_stack{{}}, parse_script("if else 1 endif"), flags, data_stack{{0x01}}, 0, 0, 401); }
    SECTION("NOTIF branch not taken") { check_eval(data_stack{{0x01}}, parse_script("notif 1 endif"), flags, data_stack{}, 0, 0, 300); }
    SECTION("VERIFY success")         { check_eval(data_stack{{0x01}}, parse_script("verify"), flags, data_stack{}, 0, 0, 100); }
    SECTION("CODESEPARATOR")          { check_eval(data_stack{{0xbb, 0xbb}}, parse_script("codeseparator"), flags, data_stack{{0xbb, 0xbb}}, 0, 0, 100); }
    SECTION("NOP1")                   { check_eval({}, parse_script("nop1"), flags, data_stack{}, 0, 0, 100); }
}

TEST_CASE("VM-Limits: individual opcode costs — bitwise operations", "[vm][vmlimits]") {
    // Mirrors BCHN test_individual_opcode_counts flags.
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_native_introspection
        | script_flags::bch_p2sh_32 | script_flags::bch_tokens
        | script_flags::bch_minimaldata | script_flags::bch_input_sigchecks
        | script_flags::bch_enforce_sigchecks;

    SECTION("AND") { check_eval(data_stack{{0xf0, 0x0f}, {0xba, 0xba}}, parse_script("and"), flags, data_stack{{0xb0, 0x0a}}, 0, 0, 102); }
    SECTION("OR")  { check_eval(data_stack{{0xf0, 0x0f}, {0xba, 0xba}}, parse_script("or"),  flags, data_stack{{0xfa, 0xbf}}, 0, 0, 102); }
    SECTION("XOR") { check_eval(data_stack{{0xf0, 0x0f}, {0xba, 0xba}}, parse_script("xor"), flags, data_stack{{0x4a, 0xb5}}, 0, 0, 102); }
    SECTION("EQUAL true")  { check_eval(data_stack{{0x12, 0x34}, {0x12, 0x34}}, parse_script("equal"), flags, data_stack{{0x01}}, 0, 0, 101); }
    SECTION("EQUAL false") { check_eval(data_stack{{0x12, 0x34}, {0x12, 0x35}}, parse_script("equal"), flags, data_stack{{}}, 0, 0, 100); }
    SECTION("EQUALVERIFY") { check_eval(data_stack{{0x12, 0x34}, {0x12, 0x34}}, parse_script("equalverify"), flags, data_stack{}, 0, 0, 101); }
}

// CHECKDATASIG with invalid signature: DER r=1,s=1 is not a valid ECDSA
// signature for this pubkey/message, so verification fails (pushes false).
// Cross-checks native vs BCHN consensus (both do real secp256k1 verification).
TEST_CASE("VM-Limits: CHECKDATASIG failed ECDSA verification", "[vm][vmlimits]") {
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_minimaldata
        | script_flags::bch_input_sigchecks | script_flags::bch_enforce_sigchecks;

    data_chunk const sig = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01};
    data_chunk const message = {0xde, 0xad, 0xbe, 0xef, 0x42};
    data_chunk const pubkey = {
        0x02,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
    };

    // Verification fails → pushes false. Metrics still tallied (non-empty sig).
    // NULLFAIL is not enabled here, so the script succeeds with false on stack.
    SECTION("CHECKDATASIG native vs consensus") {
        check_eval(data_stack{sig, message, pubkey}, parse_script("checkdatasig"), flags,
                   data_stack{{}}, 1, 1, 26292, 26164);
    }
}

// Verify that sig_checks and hash_iterations are tallied even when
// CHECKDATASIG verification fails (non-empty signature, wrong key).
// This catches the bug where metrics were only counted on success.
// Uses eval_native directly to test native metrics behavior.
TEST_CASE("VM-Limits: CHECKDATASIG metrics on failed verification", "[vm][vmlimits]") {
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_minimaldata
        | script_flags::bch_input_sigchecks | script_flags::bch_enforce_sigchecks;

    // A minimal valid DER signature (30 06 02 01 01 02 01 01) = r=1, s=1.
    data_chunk const fake_sig = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01};
    data_chunk const message = {0xde, 0xad, 0xbe, 0xef, 0x42};
    // 2*G — a valid curve point that won't match the fake sig for this message.
    data_chunk const pubkey = {
        0x02,
        0xc6, 0x04, 0x7f, 0x94, 0x41, 0xed, 0x7d, 0x6d,
        0x30, 0x45, 0x40, 0x6e, 0x95, 0xc0, 0x7c, 0xd8,
        0x5c, 0x77, 0x8e, 0x4b, 0x8c, 0xef, 0x3c, 0xa7,
        0xab, 0xac, 0x09, 0xb9, 0x5c, 0x70, 0x9e, 0xe5
    };

    SECTION("non-empty sig that fails: sig_checks must be 1") {
        auto const [ec, sig_checks, hash_iters, op_cost, std_cost, nonstd_cost, stack]
            = eval_native(data_stack{fake_sig, message, pubkey}, parse_script("checkdatasig"), flags);
        // Verification fails → pushes false
        CHECK(stack == data_stack{{}});
        // But sig_checks and hash_iters must still be tallied (matching BCHN).
        CHECK(sig_checks == 1);
        CHECK(hash_iters == 1);  // SHA256 of 5-byte message = 1 iteration
        CHECK(op_cost == 100);   // base cost + push of empty (0 bytes)
    }

    SECTION("empty sig: sig_checks must be 0") {
        auto const [ec, sig_checks, hash_iters, op_cost, std_cost, nonstd_cost, stack]
            = eval_native(data_stack{{}, message, pubkey}, parse_script("checkdatasig"), flags);
        CHECK(stack == data_stack{{}});
        CHECK(sig_checks == 0);
        CHECK(hash_iters == 0);
        CHECK(op_cost == 100);
    }
}

TEST_CASE("VM-Limits: OP_ROLL and OP_PICK cost", "[vm][vmlimits]") {
    // Mirrors BCHN test_individual_opcode_counts flags.
    auto const flags = script_flags::bch_vm_limits | script_flags::bch_bigint
        | script_flags::bch_64bit_integers | script_flags::bch_native_introspection
        | script_flags::bch_p2sh_32 | script_flags::bch_tokens
        | script_flags::bch_minimaldata | script_flags::bch_input_sigchecks
        | script_flags::bch_enforce_sigchecks;
    // Use same element sizes as BCHN reference: valtype(9), valtype(10), valtype(11)
    data_chunk const v9(9, 0x09);
    data_chunk const v10(10, 0x0a);
    data_chunk const v11(11, 0x0b);

    SECTION("PICK index 2") {
        // cost = 100 + picked_element_size = 100 + 9 = 109
        check_eval(data_stack{v9, v10, v11, {0x02}}, parse_script("pick"), flags, data_stack{v9, v10, v11, v9}, 0, 0, 109);
    }
    SECTION("ROLL index 2") {
        // cost = 100 + rolled_element_size + index = 100 + 9 + 2 = 111
        check_eval(data_stack{v9, v10, v11, {0x02}}, parse_script("roll"), flags, data_stack{v10, v11, v9}, 0, 0, 109 + 2);
    }
}
