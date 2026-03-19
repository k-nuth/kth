// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_METRICS_HPP
#define KTH_DOMAIN_MACHINE_METRICS_HPP

#include <cstdint>
#include <optional>

#include <kth/domain/constants/common.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/script_limits.hpp>

namespace kth::domain::machine {

inline constexpr
bool is_vm_limits_standard(uint32_t script_flags) {
    // This constant is defined in consensus lib, but we also need it here.
    constexpr uint32_t verify_flags_enable_vm_limits_standard = (1U << 29); //SCRIPT_VM_LIMITS_STANDARD
    return script_flags & verify_flags_enable_vm_limits_standard;
}

struct KD_API metrics {
    using script_limits_opt_t = std::optional<may2025::script_limits>;

    // Getters
    uint32_t sig_checks() const {
        return sig_checks_;
    }

    uint64_t op_cost() const {
        return op_cost_;
    }

    uint64_t hash_digest_iterations() const {
        return hash_digest_iterations_;
    }

    // Setters
    void add_op_cost(uint32_t cost) {
        op_cost_ += int64_t(cost);
    }

    void add_hash_iterations(uint32_t message_length, bool is_two_round_hash /* set to true iff OP_HASH256 or OP_HASH160 */) {
        hash_digest_iterations_ += may2025::calculate_hash_iters(message_length, is_two_round_hash);
    }

    void add_sig_checks(int n_checks) {
        sig_checks_ += n_checks;
    }

    // Checks
    bool is_over_op_cost_limit(uint32_t script_flags) const {
        return script_limits && composite_op_cost(script_flags) > script_limits->op_cost_limit();
    }

    // Non-standard (block validation) op_cost check for the native interpreter.
    bool is_over_op_cost_limit() const {
        return script_limits && composite_op_cost(false) > script_limits->op_cost_limit();
    }

    bool is_over_hash_iters_limit() const {
        return script_limits && hash_digest_iterations() > script_limits->hash_iters_limit();
    }

    bool has_valid_script_limits() const {
        return script_limits.has_value();
    }

    void set_script_limits(uint32_t script_flags, uint64_t script_sig_size) {
        script_limits.emplace(is_vm_limits_standard(script_flags), script_sig_size);
    }

    // For the native interpreter (uses bool standard directly, not consensus flags).
    void set_native_script_limits(bool standard, uint64_t script_sig_size) {
        script_limits.emplace(standard, script_sig_size);
    }

    script_limits_opt_t const& get_script_limits() const {
        return script_limits;
    }

    // Returns the composite value that is: nOpCost + nHashDigestIterators * {192 or 64} + nSigChecks * 26,000
    // Consensus code uses a 64 for the hashing iter cost, standard/relay code uses the more restrictive cost of 192.
    uint64_t composite_op_cost(uint32_t script_flags) const {
        return composite_op_cost(is_vm_limits_standard(script_flags));
    }

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
    script_limits_opt_t script_limits;

    uint64_t get_hash_iter_cost_factor(uint32_t script_flags) const;
};

} // namespace kth::domain::machine

#endif // KTH_DOMAIN_MACHINE_METRICS_HPP
