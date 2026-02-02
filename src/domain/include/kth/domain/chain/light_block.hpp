// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_LIGHT_BLOCK_HPP
#define KTH_DOMAIN_CHAIN_LIGHT_BLOCK_HPP

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <kth/domain/chain/header.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

/// Lightweight block representation for fast-mode validation.
///
/// Stores:
///   - Parsed header (80 bytes)
///   - Complete raw block bytes (for disk storage)
///   - Transaction start offsets (length derived from next offset)
///
/// This avoids:
///   - Creating transaction objects (inputs, outputs, scripts)
///   - Memory allocations for complex structures
///   - Mutex overhead from lazy hash computation
///   - Re-serialization for hashing
///
/// Merkle root is computed on-demand by hashing raw tx bytes directly.
struct light_block {
    // Deserialization.
    //-------------------------------------------------------------------------

    /// Parse block minimally for fast-mode validation.
    /// Parses only the header and finds transaction boundaries.
    /// Does NOT deserialize transactions into objects.
    /// Stores complete raw bytes for disk storage.
    static
    expect<light_block> from_data(byte_reader& reader, bool wire = true);

    // Properties.
    //-------------------------------------------------------------------------

    /// Get header.
    [[nodiscard]]
    chain::header const& header() const { return header_; }

    /// Get raw block data for disk storage.
    [[nodiscard]]
    data_chunk const& raw_data() const { return raw_data_; }

    /// Get serialized size of the block.
    [[nodiscard]]
    size_t serialized_size(bool /*wire*/ = true) const { return raw_data_.size(); }

    /// Get number of transactions.
    [[nodiscard]]
    size_t tx_count() const { return tx_offsets_.size(); }

    /// Get length of transaction at index.
    /// Length is derived from next offset (or end of data for last tx).
    [[nodiscard]]
    uint32_t tx_length(size_t index) const;

    /// Get raw bytes for a specific transaction.
    [[nodiscard]]
    byte_span tx_bytes(size_t index) const;

    // Validation.
    //-------------------------------------------------------------------------

    /// Generate merkle root by hashing raw transaction bytes.
    /// Uses Bitcoin Core's optimized SHA256D64 (SIMD accelerated).
    [[nodiscard]]
    hash_digest generate_merkle_root() const;

    /// Check if merkle root matches header.
    [[nodiscard]]
    bool is_valid_merkle_root() const {
        return generate_merkle_root() == header_.merkle();
    }

    chain::header header_;                 // Only header is fully parsed
    data_chunk raw_data_;                  // Complete raw block bytes
    std::vector<uint32_t> tx_offsets_;     // Start offset of each tx in raw_data
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_LIGHT_BLOCK_HPP
