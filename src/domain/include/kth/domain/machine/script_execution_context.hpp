// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_SCRIPT_EXECUTION_CONTEXT_HPP
#define KTH_DOMAIN_MACHINE_SCRIPT_EXECUTION_CONTEXT_HPP

#include <cstdint>
#include <optional>

#include <kth/domain/define.hpp>
#include <kth/domain/chain/transaction.hpp>

namespace kth::domain::machine {

/// An execution context for evaluating a script input. This provides access to transaction
/// and input information needed for Native Introspection opcodes.
struct KD_API script_execution_context {
    /// Construct a context for a specific input in a transaction
    script_execution_context(uint32_t input_index, chain::transaction const& transaction);

    /// Get the input number being evaluated
    uint32_t input_index() const;

    /// Get the transaction associated with this script evaluation context
    chain::transaction const& transaction() const;

    /// Get the total number of inputs in the transaction
    uint32_t input_count() const;

    /// Get the total number of outputs in the transaction
    uint32_t output_count() const;

    /// Get the transaction version
    uint32_t tx_version() const;

    /// Get the transaction locktime
    uint32_t tx_locktime() const;

private:
    uint32_t input_index_;
    chain::transaction const& transaction_;
};

} // namespace kth::domain::machine

#endif // KTH_DOMAIN_MACHINE_SCRIPT_EXECUTION_CONTEXT_HPP