// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef KTH_DOMAIN_POLICY_STANDARDNESS_HPP
#define KTH_DOMAIN_POLICY_STANDARDNESS_HPP

#include <cstdint>
#include <limits>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/constants.hpp>

#if defined(KTH_CURRENCY_BCH)
namespace kth::domain::policy {

/// Knuth's configurable local policy. Initially no restriction beyond consensus.
/// These functions do not replace contextual, UTXO or script consensus checks.
struct transaction_standardness_parameters {
    size_t maximum_transaction_size = max_tx_size;
    size_t maximum_scriptsig_size = max_script_size;
    size_t maximum_output_script_size = max_tx_size;
    uint32_t minimum_version = 0;
    uint32_t maximum_version = std::numeric_limits<uint32_t>::max();
    uint64_t minimum_output_value = 0;
    size_t maximum_datacarrier_bytes = max_tx_size;
    bool require_push_only = false;
};

[[nodiscard]] KD_API code check_transaction_standardness(
    chain::transaction const& tx, transaction_standardness_parameters const& parameters = {});
[[nodiscard]] KD_API code check_script_standardness(
    chain::script const& script, transaction_standardness_parameters const& parameters = {});
[[nodiscard]] KD_API code check_input_standardness(
    chain::transaction const& tx, transaction_standardness_parameters const& parameters = {});

} // namespace kth::domain::policy
#endif
#endif
