// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_TRANSACTION_VALIDATION_HPP
#define KTH_BLOCKCHAIN_VALIDATE_TRANSACTION_VALIDATION_HPP

#include <kth/blockchain/validate/validation_store.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

/// Transient validation state for a transaction being validated (mempool or
/// inside a block). This used to live as a `mutable` member on
/// domain::chain::transaction, mixing consensus/wire data with validator
/// working state; it now lives here, owned by the validator, keyed by tx hash.
struct transaction_validation {
    domain::chain::chain_state::ptr state = nullptr;

    // The transaction is an unspent duplicate (BIP30).
    bool duplicate = false;

    // The unconfirmed tx is validated at the block's current fork state.
    bool current = false;

    // Simulate organization and instead just validate the transaction.
    bool simulate = false;

    // The transaction was validated before its insertion in the mempool.
    bool validated = false;
};

/// Owns the per-transaction validation state keyed by transaction hash. See
/// validation_store for the surface and the lifetime/concurrency contract.
using transaction_validation_store = validation_store<transaction_validation>;

} // namespace kth::blockchain

#endif
