// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of the standardness policy of Bitcoin Cash Node
// (commit 07576013c91ff4a3a74acd85f189c69121cdad1b):
// src/policy/policy.{h,cpp} and Solver() of src/script/standard.cpp.
// Original: Copyright (c) 2009-2010 Satoshi Nakamoto,
//           Copyright (c) 2009-2016 The Bitcoin Core developers,
//           Copyright (c) 2020-2025 The Bitcoin developers, MIT license.
//
// This is an independent policy, used for comparison against the BCHN node.
// It does not touch `script::output_pattern()`, which is used by consensus and history.

#ifndef KTH_DOMAIN_POLICY_STANDARDNESS_BCHN_HPP_
#define KTH_DOMAIN_POLICY_STANDARDNESS_BCHN_HPP_

#if defined(KTH_CURRENCY_BCH)
#include <cstddef>
#include <cstdint>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/constants/standardness_bchn.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/infrastructure/error.hpp>

namespace kth::domain::policy {

/// Global BCHN configuration read by the policy (-datacarriersize, -dustrelayfee,
/// -permitbaremultisig, -bytespersigcheck).
struct bchn_standardness_parameters {
    uint32_t maximum_datacarrier_bytes = bchn_max_op_return_relay;
    uint64_t dust_relay_fee_per_kb = bchn_dust_relay_tx_fee;
    bool permit_bare_multisig = true;
    uint32_t bytes_per_sigcheck = bchn_default_bytes_per_sigcheck;
};

/// BCHN txnouttype, as classified by Solver().
enum class bchn_script_type {
    nonstandard,
    pubkey,
    pubkeyhash,
    scripthash,
    multisig,
    null_data,
    script
};

/// Result of the BCHN Solver(): the template plus, for multisig, the m and n of the script.
struct bchn_solution {
    bchn_script_type type = bchn_script_type::nonstandard;
    uint8_t required = 0;   ///< m, only for multisig
    uint8_t keys = 0;       ///< n, only for multisig
};

/// BCHN MANDATORY_SCRIPT_VERIFY_FLAGS / STANDARD_SCRIPT_VERIFY_FLAGS expressed in Knuth script flags.
/// BCHN's STANDARD set does not include MINIMALIF, so neither does this one.
KD_API
script_flags_t bchn_mandatory_script_flags();

KD_API
script_flags_t bchn_standard_script_flags();

/// BCHN GetMemPoolScriptFlags: the flags of the next block, plus the standard flags and, if
/// `require_standard` and the VM limits are active, the stricter VM limits flag.
KD_API
script_flags_t get_standard_script_flags_bchn(script_flags_t consensus_flags, bool require_standard = true);

/// BCHN Solver(). Works on the raw bytes of the script.
[[nodiscard]] KD_API
bchn_solution solve_script_bchn(chain::script const& script, script_flags_t flags);

/// BCHN GetDustThreshold / IsDust.
[[nodiscard]] KD_API
uint64_t get_dust_threshold_bchn(chain::output const& output, uint64_t dust_relay_fee_per_kb = bchn_dust_relay_tx_fee);

[[nodiscard]] KD_API
bool is_dust_bchn(chain::output const& output, uint64_t dust_relay_fee_per_kb = bchn_dust_relay_tx_fee);

/// BCHN GetVirtualTransactionSize / GetVirtualTransactionInputSize: max(size, sigchecks * bytes_per_sigcheck).
[[nodiscard]] KD_API
int64_t get_virtual_transaction_size_bchn(int64_t size, int64_t sigchecks, uint32_t bytes_per_sigcheck = bchn_default_bytes_per_sigcheck);

[[nodiscard]] KD_API
int64_t get_virtual_transaction_size_bchn(chain::transaction const& tx, int64_t sigchecks, uint32_t bytes_per_sigcheck = bchn_default_bytes_per_sigcheck);

[[nodiscard]] KD_API
int64_t get_virtual_transaction_input_size_bchn(chain::input const& input, int64_t sigchecks, uint32_t bytes_per_sigcheck = bchn_default_bytes_per_sigcheck);

/// BCHN IsStandard(scriptPubKey). `error::standardness_scriptpubkey` if it is not standard.
[[nodiscard]] KD_API
code check_script_standardness_bchn(chain::script const& script, script_flags_t flags);

/// BCHN IsStandardTx. The reasons are those of BCHN: version, tx-size, scriptsig-size,
/// scriptsig-not-pushonly, txn-tokens-before-activation, scriptpubkey, bare-multisig, dust,
/// oversize-op-return.
[[nodiscard]] KD_API
code check_transaction_standardness_bchn(chain::transaction const& tx, script_flags_t flags,
                                         bchn_standardness_parameters const& parameters = {});

/// BCHN AreInputsStandard. Reads the previous outputs from `input.previous_output().validation.cache`;
/// `error::missing_previous_output` if one is not populated. Coinbases succeed.
[[nodiscard]] KD_API
code check_input_standardness_bchn(chain::transaction const& tx, script_flags_t flags);

} // namespace kth::domain::policy

#endif // KTH_CURRENCY_BCH
#endif // KTH_DOMAIN_POLICY_STANDARDNESS_BCHN_HPP_
