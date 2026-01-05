// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONSTANTS_COMMON_HPP_
#define KTH_DOMAIN_CONSTANTS_COMMON_HPP_

#include <cstddef>
#include <cstdint>

// #include <kth/domain/define.hpp>
// #include <kth/domain/version.hpp>

// #include <kth/infrastructure/compat.hpp>
// #include <kth/infrastructure/config/checkpoint.hpp>
// #include <kth/infrastructure/constants.hpp>
// #include <kth/infrastructure/math/hash.hpp>
// #include <kth/infrastructure/message/network_address.hpp>

namespace kth {

// Consensus sentinels.
//-----------------------------------------------------------------------------

constexpr uint32_t no_previous_output = max_uint32;
constexpr uint32_t max_input_sequence = max_uint32;
constexpr uint64_t sighash_null_value = max_uint64;

// Script/interpreter constants.
//-----------------------------------------------------------------------------

// Consensus
constexpr size_t max_counted_ops = 201;
constexpr size_t max_stack_size = 1000;
constexpr size_t max_script_size = 10000;
// constexpr size_t max_push_data_size = 520;
constexpr size_t max_push_data_size_legacy = 520;
constexpr size_t max_script_public_keys = 20;
constexpr size_t multisig_default_sigops = 20;
constexpr size_t max_number_size_32_bits = 4;
constexpr size_t max_number_size_64_bits = 8;
constexpr size_t max_check_locktime_verify_number_size = 5;
constexpr size_t max_check_sequence_verify_number_size = 5;

// The below constants are used after activation of the May 2025 upgrade (Targeted VM Limits CHIP)
namespace may2025 {

    // Maximum number of bytes pushable to the stack
    constexpr size_t max_push_data_size = max_script_size;      // BCHN: MAX_SCRIPT_ELEMENT_SIZE
    // Base cost for each executed opcode; no opcodes incur a cost less than this, but some may incur more.
    constexpr size_t opcode_cost = 100u;                        // BCHN: OPCODE_COST
    // Conditional stack depth limit (max depth of OP_IF and friends)
    constexpr size_t max_conditional_stack_depth = 100u;        // BCHN: MAX_CONDITIONAL_STACK_DEPTH
    // Each sigcheck done by an input adds this amount to the total op cost
    constexpr uint64_t sig_check_cost_factor = 26'000u;         // BCHN: SIG_CHECK_COST_FACTOR
}

// Policy.
constexpr size_t max_null_data_size = 80;

// Various validation constants.
//-----------------------------------------------------------------------------

constexpr size_t min_coinbase_size = 2;
constexpr size_t max_coinbase_size = 100;
constexpr size_t median_time_past_interval = 11;
constexpr size_t coinbase_maturity = 100;
constexpr size_t locktime_threshold = 500000000;

// Derived.
constexpr size_t max_sigops_factor = 50;
constexpr size_t one_million_bytes_block = 1000000;
constexpr size_t coinbase_reserved_size = 20000;
constexpr size_t sigops_per_million_bytes = 20000;

// Relative locktime constants.
//-----------------------------------------------------------------------------

constexpr size_t relative_locktime_min_version = 2;
constexpr size_t relative_locktime_seconds_shift = 9;
constexpr uint32_t relative_locktime_mask = 0x0000ffff;
constexpr uint32_t relative_locktime_disabled = 0x80000000;
constexpr uint32_t relative_locktime_time_locked = 0x00400000;

// Timespan constants.
//-----------------------------------------------------------------------------

constexpr uint32_t retargeting_factor = 4;
constexpr uint32_t timestamp_future_seconds = 2 * 60 * 60;
constexpr uint32_t easy_spacing_factor = 2;
constexpr uint32_t easy_spacing_seconds = easy_spacing_factor * target_spacing_seconds;

// The upper and lower bounds for the retargeting timespan.
constexpr uint32_t min_timespan = target_timespan_seconds / retargeting_factor;
constexpr uint32_t max_timespan = target_timespan_seconds * retargeting_factor;

// Bitcoin:  The target number of blocks for 2 weeks of work (2016 blocks).
// Litecoin: The target number of blocks for 3.5 days of work (2016 blocks).
constexpr size_t retargeting_interval = target_timespan_seconds / target_spacing_seconds;

// Fork constants.
//-----------------------------------------------------------------------------

// Consensus rule change activation and enforcement parameters.
constexpr size_t first_version = 1;
constexpr size_t bip34_version = 2;
constexpr size_t bip66_version = 3;
constexpr size_t bip65_version = 4;

// Network protocol constants.
//-----------------------------------------------------------------------------

// Explicit size.
constexpr size_t command_size = 12;

// Explicit limits.
constexpr size_t max_address = 1000;
constexpr size_t max_filter_add = 520;
constexpr size_t max_filter_functions = 50;
constexpr size_t max_filter_load = 36000;
constexpr size_t max_get_blocks = 500;
constexpr size_t max_get_headers = 2000;
constexpr size_t max_get_data = 50000;
constexpr size_t max_inventory = 50000;

constexpr size_t max_payload_size = 33554432;
/**
 * The minimum safe length of a seed in bits (multiple of 8).
 */
constexpr size_t minimum_seed_bits = 128;

/**
 * The minimum safe length of a seed in bytes (16).
 */
constexpr size_t minimum_seed_size = minimum_seed_bits / byte_bits;

// Effective limit given a 32 bit chain height boundary: 10 + log2(2^32) + 1.
constexpr size_t max_locator = 43;


// Currency unit constants (uint64_t).
//-----------------------------------------------------------------------------

constexpr uint64_t satoshi_per_bitcoin = 100000000;
constexpr uint64_t initial_block_subsidy_bitcoin = 50;
constexpr uint64_t recursive_money = 0x02540be3f5;

} // namespace kth

#endif // KTH_DOMAIN_CONSTANTS_COMMON_HPP_
