// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_HEADER_ORGANIZER_HPP
#define KTH_BLOCKCHAIN_HEADER_ORGANIZER_HPP

#include <atomic>
#include <cstddef>
#include <memory>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/domain.hpp>
#include <kth/domain/message/header.hpp>

namespace kth::blockchain {

/// Result of adding headers to the organizer
struct header_organize_result {
    code error;
    size_t headers_added{0};
    size_t index_size{0};
    size_t index_memory_bytes{0};
};

/// Header organizer for headers-first sync.
/// Validates headers and stores them in the header_index.
///
/// ARCHITECTURE:
/// -------------
/// The organizer validates headers as they arrive and stores them
/// in the header_index (SoA structure with O(1) hash lookup and
/// O(log n) ancestor lookup via skip pointers).
///
/// The header_index is owned by block_chain and passed by reference.
///
/// THREAD SAFETY:
/// --------------
/// Thread-safe through header_index's concurrent_flat_map.
struct KB_API header_organizer {
    using ptr = std::shared_ptr<header_organizer>;
    using index_t = header_index::index_t;

    /// Construct a header organizer.
    /// @param[in] index     Reference to the header index (owned by block_chain).
    /// @param[in] settings  Blockchain settings.
    /// @param[in] network   The network type.
    header_organizer(header_index& index, settings const& settings,
                     domain::config::network network);

    ~header_organizer() = default;

    // Non-copyable
    header_organizer(header_organizer const&) = delete;
    header_organizer& operator=(header_organizer const&) = delete;

    bool start();
    bool stop();

    /// Sync tip state from the header index.
    /// Call after header_index has been initialized (e.g., with genesis).
    void sync_tip();

    /// Add a batch of headers to the organizer.
    /// Validates all headers and adds valid ones to the index.
    /// @param[in] headers  The headers to add.
    /// @return Result with error code and statistics.
    [[nodiscard]]
    header_organize_result add_headers(domain::message::header::list const& headers);

    // =========================================================================
    // Index Access
    // =========================================================================

    /// Get the header index.
    [[nodiscard]]
    header_index& index() { return index_; }

    [[nodiscard]]
    header_index const& index() const { return index_; }

    /// Find a header by hash.
    [[nodiscard]]
    index_t find(hash_digest const& hash) const { return index_.find(hash); }

    /// Check if a header exists.
    [[nodiscard]]
    bool contains(hash_digest const& hash) const { return index_.contains(hash); }

    // =========================================================================
    // State queries
    // =========================================================================

    /// Get the current tip index.
    [[nodiscard]]
    index_t tip_index() const { return tip_index_; }

    /// Get the current header tip height.
    [[nodiscard]]
    int32_t header_height() const;

    /// Get the hash of the header tip.
    [[nodiscard]]
    hash_digest const& header_tip_hash() const { return tip_hash_; }

    /// Get the timestamp of the header tip.
    /// Used for BCHN-style progress calculation.
    [[nodiscard]]
    uint32_t tip_timestamp() const;

    /// Get the number of headers in the index.
    [[nodiscard]]
    size_t size() const { return index_.size(); }

    /// Get estimated memory usage.
    [[nodiscard]]
    size_t memory_usage() const { return index_.memory_usage(); }

protected:
    [[nodiscard]]
    bool stopped() const { return stopped_; }

private:
    // Validate a single header with full chain-state validation
    // Includes: PoW check, difficulty, checkpoint, version, MTP
    [[nodiscard]]
    code validate_full(domain::chain::header const& header,
                       hash_digest const& hash,
                       int32_t height,
                       header_index::index_t parent_idx) const;

    // Members
    header_index& index_;
    std::atomic<bool> stopped_{false};
    validate_header validator_;

    // Current tip state
    index_t tip_index_{header_index::null_index};
    hash_digest tip_hash_{null_hash};
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_HEADER_ORGANIZER_HPP
