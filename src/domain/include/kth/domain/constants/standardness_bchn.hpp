// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Constants of the Bitcoin Cash Node standardness policy.
// Port of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b)
// src/policy/policy.h and src/script/standard.h.
// Original: Copyright (c) 2009-2010 Satoshi Nakamoto,
//           Copyright (c) 2009-2016 The Bitcoin Core developers,
//           Copyright (c) 2020-2025 The Bitcoin developers, MIT license.

#ifndef KTH_DOMAIN_CONSTANTS_STANDARDNESS_BCHN_HPP_
#define KTH_DOMAIN_CONSTANTS_STANDARDNESS_BCHN_HPP_

#include <cstddef>
#include <cstdint>

namespace kth::domain::policy {

/// BCHN MAX_STANDARD_TX_SIZE: the maximum size of a transaction we relay/mine.
inline constexpr size_t bchn_max_standard_tx_size = 100'000;

/// BCHN MAX_TX_IN_SCRIPT_SIG_SIZE_LEGACY: scriptSig size limit before upgrade 12
/// (afterwards the limit is the maximum script size).
inline constexpr size_t bchn_max_tx_in_script_sig_size_legacy = 1650;

/// BCHN MAX_OP_RETURN_RELAY: aggregate bytes of all OP_RETURN scriptPubKeys of a transaction.
inline constexpr uint32_t bchn_max_op_return_relay = 223;

/// BCHN DUST_RELAY_TX_FEE, in satoshis per kilobyte.
inline constexpr uint64_t bchn_dust_relay_tx_fee = 1000;

/// BCHN DEFAULT_BYTES_PER_SIGCHECK.
inline constexpr uint32_t bchn_default_bytes_per_sigcheck = 50;

/// BCHN CTransaction::MIN_STANDARD_VERSION / MAX_STANDARD_VERSION.
inline constexpr uint32_t bchn_min_standard_version = 1;
inline constexpr uint32_t bchn_max_standard_version = 2;

/// BCHN GetDustThreshold: size of the input that would spend the output (32 + 4 + 1 + 107 + 4).
inline constexpr size_t bchn_dust_spend_input_size = 148;

/// BCHN MAX_SCRIPT_SIZE (script/vm_limits.h): scriptSig limit after upgrade 12 and unspendable threshold.
inline constexpr size_t bchn_max_script_size = 10'000;

/// BCHN MAX_P2S_SCRIPT_SIZE.
inline constexpr size_t bchn_max_p2s_script_size = 201;

/// BCHN IsStandard: bare multisig is standard up to x-of-3.
inline constexpr size_t bchn_max_standard_multisig_keys = 3;

} // namespace kth::domain::policy

#endif // KTH_DOMAIN_CONSTANTS_STANDARDNESS_BCHN_HPP_
