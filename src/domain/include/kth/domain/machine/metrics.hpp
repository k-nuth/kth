// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_METRICS_HPP
#define KTH_DOMAIN_MACHINE_METRICS_HPP

#include <cstdint>
#include <optional>

#include <kth/domain/constants/common.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/domain/machine/script_limits.hpp>

namespace kth::domain::machine {

// True when the active flag set requests the stricter "standard"
// (relay-policy) costing for the May-2025 VM limits: per BCH's
// CHIP-2021-05-vm-limits, hash-iteration cost is 3x higher in
// standard/relay mode than in consensus (block-validation) mode.
// The flag is meaningful only when `bch_vm_limits` is also active —
// it has no effect on pre-Galois scripts. Parameter name is `flags`
// (not `script_flags`) to avoid shadowing the enum name.
inline constexpr
bool is_vm_limits_standard(script_flags_t flags) {
    return (flags & script_flags::bch_vm_limits_standard) != 0;
}

struct KD_API metrics {
    using script_limits_opt_t = std::optional<may2025::script_limits>;

    // Getters
    [[nodiscard]]
    uint32_t sig_checks() const {
        return sig_checks_;
    }

    [[nodiscard]]
    uint64_t op_cost() const {
        return op_cost_;
    }

    [[nodiscard]]
    uint64_t hash_digest_iterations() const {
        return hash_digest_iterations_;
    }

    // Setters / tallyers
    void add_op_cost(uint32_t cost) {
        op_cost_ += cost;
    }

    // Semantic alias of `add_op_cost` for PUSH operations, where the
    // tallied cost is the pushed stack item's length. Matches BCHN's
    // `TallyPushOp` — splitting the two names makes call sites in the
    // interpreter self-documenting (you can tell at a glance whether
    // a given tally is an opcode-dispatch cost or a push cost).
    void add_push_op(uint32_t stack_item_length) {
        op_cost_ += stack_item_length;
    }

    void add_hash_iterations(uint32_t message_length, bool is_two_round_hash /* set to true iff OP_HASH256 or OP_HASH160 */) {
        hash_digest_iterations_ += may2025::calculate_hash_iters(message_length, is_two_round_hash);
    }

    // `n_checks` is always non-negative in practice (every caller
    // passes `1` or a validated push-derived key count). Using
    // `uint32_t` here matches the field type and removes the
    // signed/unsigned mixing that the original `add_sig_checks(int)`
    // signature introduced.
    void add_sig_checks(uint32_t n_checks) {
        sig_checks_ += n_checks;
    }

    // Checks
    [[nodiscard]]
    bool is_over_op_cost_limit(script_flags_t flags) const {
        return script_limits_ && composite_op_cost(flags) > script_limits_->op_cost_limit();
    }

    // Non-standard (block validation) op_cost check for the native interpreter.
    [[nodiscard]]
    bool is_over_op_cost_limit() const {
        return script_limits_ && composite_op_cost(false) > script_limits_->op_cost_limit();
    }

    [[nodiscard]]
    bool is_over_hash_iters_limit() const {
        return script_limits_ && hash_digest_iterations() > script_limits_->hash_iters_limit();
    }

    [[nodiscard]]
    bool has_valid_script_limits() const {
        return script_limits_.has_value();
    }

    void set_script_limits(script_flags_t flags, uint64_t script_sig_size) {
        script_limits_.emplace(is_vm_limits_standard(flags), script_sig_size);
    }

    // For the native interpreter (uses bool standard directly, not consensus flags).
    void set_native_script_limits(bool standard, uint64_t script_sig_size) {
        script_limits_.emplace(standard, script_sig_size);
    }

    [[nodiscard]]
    script_limits_opt_t const& get_script_limits() const {
        return script_limits_;
    }

    // Returns the composite value that is: nOpCost + nHashDigestIterators * {192 or 64} + nSigChecks * 26,000
    // Consensus code uses a 64 for the hashing iter cost, standard/relay code uses the more restrictive cost of 192.
    [[nodiscard]]
    uint64_t composite_op_cost(script_flags_t flags) const {
        return composite_op_cost(is_vm_limits_standard(flags));
    }

    [[nodiscard]]
    uint64_t composite_op_cost(bool standard) const {
        uint64_t const factor = may2025::hash_iter_op_cost_factor(standard);
        return op_cost_
               + hash_digest_iterations_ * factor
               + uint64_t(sig_checks_) * ::kth::may2025::sig_check_cost_factor;
    }

private:
    uint32_t sig_checks_ = 0;

    /** CHIP-2021-05-vm-limits: Targeted Virtual Machine Limits */
    uint64_t op_cost_ = 0;
    uint64_t hash_digest_iterations_ = 0;
    script_limits_opt_t script_limits_;
};

} // namespace kth::domain::machine

#endif // KTH_DOMAIN_MACHINE_METRICS_HPP
