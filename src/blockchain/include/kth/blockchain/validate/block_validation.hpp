// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_BLOCK_VALIDATION_HPP
#define KTH_BLOCKCHAIN_VALIDATE_BLOCK_VALIDATION_HPP

#include <cstdint>

#include <kth/blockchain/validate/validation_store.hpp>
#include <kth/domain.hpp>
#include <kth/infrastructure/error.hpp>

namespace kth::blockchain {

/// Transient validation/tracing state for a block being organized.
///
/// This used to live as a `mutable` member on `domain::chain::block`, which
/// mixed consensus/wire data with validator working state and forced the
/// value type to hand-write its comparison. It now lives here, owned by the
/// validator via block_validation_store, keyed by block hash.
struct block_validation {
    uint64_t originator = 0;
    code error = error::not_found;
    domain::chain::chain_state::ptr state = nullptr;

    // Simulate organization and instead just validate the block.
    bool simulate = false;
};

/// Owns the per-block validation state keyed by block hash. See
/// validation_store for the concurrency/lifetime contract.
using block_validation_store = validation_store<block_validation>;

} // namespace kth::blockchain

#endif
