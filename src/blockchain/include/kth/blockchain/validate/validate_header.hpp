// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_HEADER_HPP
#define KTH_BLOCKCHAIN_VALIDATE_HEADER_HPP

#include <cstddef>
#include <cstdint>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

/// Header validation for headers-first sync.
/// This class performs header validation without requiring full chain state,
/// making it suitable for initial block download (IBD) where we validate
/// headers before downloading full blocks.
///
/// Validation levels:
/// 1. check() - Context-free checks (PoW, timestamp not too far in future)
/// 2. accept() - Chain-context checks (checkpoints, chain continuity)
///
/// Note: Difficulty validation (work_required) is NOT done here because it
/// requires building full chain_state with historical data. During IBD,
/// difficulty is validated when blocks are organized.
struct KB_API validate_header {
    using checkpoint = infrastructure::config::checkpoint;
    using checkpoint_list = infrastructure::config::checkpoint::list;

    /// Construct a header validator.
    /// @param[in] settings  Blockchain settings (checkpoints, forks).
    /// @param[in] network   The network type (mainnet, testnet, etc).
    validate_header(settings const& settings, domain::config::network network);

    /// Context-free validation (PoW + timestamp).
    /// Validates:
    /// - Proof of work is valid for the claimed difficulty (bits)
    /// - Timestamp is not too far in the future (2 hours)
    ///
    /// @param[in] header  The header to validate.
    /// @return error::success or the validation error.
    [[nodiscard]]
    code check(domain::chain::header const& header) const;

    /// Chain-context validation (checkpoints + continuity).
    /// Validates:
    /// - Header hash matches checkpoint at this height (if one exists)
    /// - Header does not conflict with any checkpoint
    /// - Previous block hash matches expected (chain continuity)
    ///
    /// @param[in] header       The header to validate.
    /// @param[in] height       The height of this header.
    /// @param[in] previous     Expected previous block hash.
    /// @return error::success or the validation error.
    [[nodiscard]]
    code accept(domain::chain::header const& header, size_t height,
                hash_digest const& previous) const;

    /// Combined check + accept for convenience.
    /// @param[in] header       The header to validate.
    /// @param[in] height       The height of this header.
    /// @param[in] previous     Expected previous block hash.
    /// @return error::success or the first validation error.
    [[nodiscard]]
    code validate(domain::chain::header const& header, size_t height,
                  hash_digest const& previous) const;

    /// Check if a height is under checkpoint protection.
    /// Headers under checkpoint can skip some validation.
    [[nodiscard]]
    bool is_under_checkpoint(size_t height) const;

    /// Check if height has a checkpoint defined.
    [[nodiscard]]
    bool is_checkpoint_height(size_t height) const;

    /// Get the checkpoint hash at a given height (if exists).
    [[nodiscard]]
    std::optional<hash_digest> checkpoint_hash(size_t height) const;

private:
    /// Check if header hash conflicts with a checkpoint at this height.
    [[nodiscard]]
    bool is_checkpoint_conflict(hash_digest const& hash, size_t height) const;

    /// Get the highest checkpoint height.
    [[nodiscard]]
    size_t last_checkpoint_height() const;

    // Configuration
    checkpoint_list const checkpoints_;
    domain::config::network const network_;
    bool const retarget_;  // Whether PoW retargeting is enabled (mainnet=true)
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_VALIDATE_HEADER_HPP
