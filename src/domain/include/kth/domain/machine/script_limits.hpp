// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_SCRIPT_LIMITS_HPP
#define KTH_DOMAIN_MACHINE_SCRIPT_LIMITS_HPP

#include <cassert>
#include <cstdint>

#include <kth/domain/define.hpp>

namespace kth::domain::machine {

namespace may2025 {

// Some constants used by helper code.
namespace detail {

/// Bonus factor (7x) applied to hash iteration limits for non-standard transactions (block transactions).
constexpr unsigned int hash_iter_bonus_nonstd = 7u;

/// Multiplier for determining the total operational cost allowance per input byte.
constexpr unsigned int op_cost_per_input_byte = 800u;

/// Penalty factor for standard transactions, where hash operations cost 3x more.
constexpr unsigned int hash_cost_penalty_std = 3u;

/// Block size used by all supported hash operations (e.g., OP_HASH160, OP_HASH256).
constexpr unsigned int hash_block_size = 64u;

/// Fixed serialization overhead in bytes credited to each input script, as specified in the VM Limits CHIP.
constexpr unsigned int input_script_fixed_credit = 41u;

/// Calculates the maximum hash iteration limit for a given input.
/// The limit depends on whether standard rules are applied and the scriptSig size.
inline constexpr
uint64_t calculate_input_hash_iters_limit(bool standard, uint64_t script_sig_size) noexcept {
    auto const factor = standard ? 1u : detail::hash_iter_bonus_nonstd;
    uint64_t const result = ((script_sig_size + detail::input_script_fixed_credit) * factor) / 2u;
    assert(result >= 0);
    return result;
}

/// Calculates the maximum operational cost for an input based on its scriptSig size.
inline constexpr
uint64_t calculate_input_op_cost_limit(uint64_t script_sig_size) noexcept {
    uint64_t const result = (script_sig_size + detail::input_script_fixed_credit) * detail::op_cost_per_input_byte;
    assert(result >= 0);
    return result;
}

} // namespace detail

/// Determines the cost factor for hash iterations based on whether standard rules apply.
/// Returns 64 for non-standard transactions, or 192 for standard transactions.
// See: https://github.com/bitjson/bch-vm-limits/tree/master?tab=readme-ov-file#summary
inline constexpr
uint64_t hash_iter_op_cost_factor(bool standard) noexcept {
    return standard ?
        detail::hash_block_size * detail::hash_cost_penalty_std :
        detail::hash_block_size;
}

/// Calculates the number of hash iterations for a given message length and hash operation type.
/// Supports one-round and two-round hash operations.
inline constexpr
uint64_t calculate_hash_iters(uint32_t message_length, bool is_two_round_hash) noexcept {
    return is_two_round_hash + 1u + ((uint64_t(message_length) + 8u) / detail::hash_block_size);
}

/// Defines the execution limits for the virtual machine (VM) when running a specific script.
/// These limits are calculated based on the scriptSig size and whether the script is executed
/// in standard or non-standard mode. The class provides a structured way to enforce these limits
/// during script evaluation.
struct KD_API script_limits {
    script_limits(bool standard, uint64_t script_sig_size)
        : op_cost_limit_{detail::calculate_input_op_cost_limit(script_sig_size)}
        , hash_iters_limit_{detail::calculate_input_hash_iters_limit(standard, script_sig_size)}
    {}

    uint64_t op_cost_limit() const { return op_cost_limit_; }
    uint64_t hash_iters_limit() const { return hash_iters_limit_; }

private:
    uint64_t op_cost_limit_;
    uint64_t hash_iters_limit_;
};

} // namespace may2025
} // namespace kth::domain::machine

#endif // KTH_DOMAIN_MACHINE_SCRIPT_LIMITS_HPP
