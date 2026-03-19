// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_HEADER_INDEX_HPP
#define KTH_BLOCKCHAIN_HEADER_INDEX_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include <boost/unordered/concurrent_flat_map.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

// =============================================================================
// Header Index - Global index of all known block headers
//
// Equivalent to BCHN's mapBlockIndex - stores metadata for ALL known headers,
// not just the active chain. This enables:
//   - O(1) lookup by hash
//   - O(log n) ancestor lookup via skip pointers
//   - Fork tracking and reorganization
//   - Headers-first sync
//
// Memory layout (SoA - Structure of Arrays):
// For ~1.18M blocks (default capacity):
//   prev_block_hashes: 37.7 MB (1,179,648 × 32 bytes)
//   parent_indices:     4.7 MB (1,179,648 × 4 bytes)
//   skip_indices:       4.7 MB
//   heights:            4.7 MB
//   chain_works:        9.4 MB (1,179,648 × 8 bytes)
//   statuses:           4.7 MB
//   versions:           4.7 MB
//   merkle_roots:      37.7 MB (1,179,648 × 32 bytes)
//   timestamps:         4.7 MB
//   bits:               4.7 MB
//   nonces:             4.7 MB
//   file_numbers:       2.4 MB (1,179,648 × 2 bytes)
//   data_positions:     4.7 MB
//   undo_positions:     4.7 MB
//   ─────────────────────────────
//   Total:            ~137 MB
//
// Thread safety:
//   - Writers: CFM's bucket lock during insert_and_visit
//   - Readers: CFM's bucket lock during visit/cvisit provides acquire semantics
//   - Traversal: safe via happens-before transitivity through ancestor chain
// =============================================================================

// Default capacity - enough for ~2030 (aligned to 2^20 + buffer)
// Defined via CMake/Conan as KTH_HEADER_INDEX_CAPACITY
#ifndef KTH_HEADER_INDEX_CAPACITY
#define KTH_HEADER_INDEX_CAPACITY 1179648
#endif

/// Status flags for indexed headers (similar to BCHN's BlockStatus)
enum class header_status : uint32_t {
    none                    = 0,

    // Validation state
    valid_header            = 1 << 0,   // Header syntax valid
    valid_tree              = 1 << 1,   // Transactions merkle root valid
    valid_chain             = 1 << 2,   // Connects to valid chain
    valid_scripts           = 1 << 3,   // Scripts & signatures valid

    // Data availability
    have_data               = 1 << 4,   // Full block data available
    have_undo               = 1 << 5,   // Undo data available

    // Chain state
    failed_valid            = 1 << 6,   // Descends from failed block
    failed_child            = 1 << 7,   // Parent failed validation

    // Derived flags
    valid_mask              = valid_header | valid_tree | valid_chain | valid_scripts,
};

constexpr header_status operator|(header_status a, header_status b) noexcept {
    return header_status(uint32_t(a) | uint32_t(b));
}

constexpr header_status operator&(header_status a, header_status b) noexcept {
    return header_status(uint32_t(a) & uint32_t(b));
}

constexpr bool has_flag(header_status status, header_status flag) noexcept {
    return (uint32_t(status) & uint32_t(flag)) != 0;
}

/// All data for a single header entry
struct header_entry {
    domain::chain::header header;
    hash_digest hash{};
    uint32_t parent_index{};
    uint32_t skip_index{};
    int32_t height{};
    uint64_t chain_work{};
    header_status status{};

    // Block file location (flat file storage)
    int16_t file_number{-1};    // File number (-1 = no data)
    uint32_t data_pos{0};       // Position in blk*.dat
    uint32_t undo_pos{0};       // Position in rev*.dat
};

/// Result of adding a header to the index
struct header_index_result {
    bool inserted{false};       // true if new header was added
    uint32_t index{0};          // index of the header (new or existing)
    bool capacity_warning{false}; // true if at 95% capacity
};

/// Hash function for hash_digest.
/// This used to call `GetCheapHash()` in uint256, which was later moved; the
/// cheap hash function simply calls ReadLE64() however, so the end result is
/// identical. (Comment from BCHN's BlockHasher in chain.h)
struct hash_digest_hasher {
    constexpr size_t operator()(hash_digest const& hash) const noexcept {
        return from_little_endian_unsafe<size_t>(hash);
    }
};

/// Global index of all known block headers.
/// Thread-safe, lock-free reads, bucket-locked writes.
struct KB_API header_index {
    using index_t = uint32_t;
    static constexpr index_t null_index = std::numeric_limits<index_t>::max();
    static constexpr size_t default_capacity = KTH_HEADER_INDEX_CAPACITY;

    // =========================================================================
    // Construction
    // =========================================================================

    /// Construct with specified capacity.
    /// @param capacity Maximum number of headers to store.
    explicit header_index(size_t capacity = default_capacity);

    ~header_index() = default;

    // Non-copyable, non-movable (contains atomics and concurrent map)
    header_index(header_index const&) = delete;
    header_index& operator=(header_index const&) = delete;
    header_index(header_index&&) = delete;
    header_index& operator=(header_index&&) = delete;

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Add a header to the index.
    /// Thread-safe. If header already exists, returns existing index.
    /// @param hash The header's hash.
    /// @param header The header data.
    /// @return Result with insertion status and index.
    [[nodiscard]]
    header_index_result add(hash_digest const& hash, domain::chain::header const& header);

    /// Find a header by hash.
    /// Thread-safe, lock-free.
    /// @param hash The header's hash.
    /// @return Index if found, null_index otherwise.
    [[nodiscard]]
    index_t find(hash_digest const& hash) const;

    /// Check if a header exists.
    /// @param hash The header's hash.
    [[nodiscard]]
    bool contains(hash_digest const& hash) const;

    // =========================================================================
    // Field Accessors
    // =========================================================================

    [[nodiscard]] hash_digest get_prev_block_hash(index_t idx) const;
    [[nodiscard]] index_t get_parent_index(index_t idx) const;
    [[nodiscard]] index_t get_skip_index(index_t idx) const;
    [[nodiscard]] int32_t get_height(index_t idx) const;
    [[nodiscard]] uint64_t get_chain_work(index_t idx) const;
    [[nodiscard]] header_status get_status(index_t idx) const;
    [[nodiscard]] uint32_t get_version(index_t idx) const;
    [[nodiscard]] hash_digest get_merkle(index_t idx) const;
    [[nodiscard]] uint32_t get_timestamp(index_t idx) const;
    [[nodiscard]] uint32_t get_bits(index_t idx) const;
    [[nodiscard]] uint32_t get_nonce(index_t idx) const;

    /// Get all fields for a header at once.
    [[nodiscard]] header_entry get_entry(index_t idx) const;

    /// Get the stored hash of a header.
    [[nodiscard]] hash_digest get_hash(index_t idx) const;

    /// Reconstruct the full header from stored fields.
    [[nodiscard]] domain::chain::header get_header(index_t idx) const;

    // =========================================================================
    // Status Management
    // =========================================================================

    /// Set status flags for a header.
    void set_status(index_t idx, header_status status);

    /// Add status flags to a header (OR with existing).
    void add_status(index_t idx, header_status flags);

    /// Check if header has specific status flags.
    [[nodiscard]]
    bool has_status(index_t idx, header_status flags) const;

    // =========================================================================
    // Chain Work Management
    // =========================================================================

    /// Update chain work for a header.
    void set_chain_work(index_t idx, uint64_t work);

    // =========================================================================
    // Block File Location (flat file storage)
    // =========================================================================

    /// Set block file position.
    /// @param idx Header index.
    /// @param file File number (blk*.dat).
    /// @param pos Byte offset within file.
    void set_block_pos(index_t idx, int16_t file, uint32_t pos);

    /// Set undo file position.
    /// @param idx Header index.
    /// @param pos Byte offset within rev*.dat (same file number as block).
    void set_undo_pos(index_t idx, uint32_t pos);

    /// Get file number for block.
    /// @return File number or -1 if no data.
    [[nodiscard]]
    int16_t get_file_number(index_t idx) const;

    /// Get block data position.
    [[nodiscard]]
    uint32_t get_data_pos(index_t idx) const;

    /// Get undo data position.
    [[nodiscard]]
    uint32_t get_undo_pos(index_t idx) const;

    /// Check if block data is available.
    [[nodiscard]]
    bool has_block_data(index_t idx) const;

    /// Check if undo data is available.
    [[nodiscard]]
    bool has_undo_data(index_t idx) const;

    // =========================================================================
    // Traversal Primitives
    // =========================================================================

    /// Traverse backwards from start_idx, calling visitor for each entry.
    /// @param start_idx Starting index.
    /// @param max_steps Maximum steps to traverse (0 = unlimited).
    /// @param visitor Callable with signature void(index_t idx).
    template <typename Visitor>
    void traverse_back(index_t start_idx, size_t max_steps, Visitor&& visitor) const;

    /// Get ancestor at specific height using linear traversal.
    /// O(n) where n = height difference.
    /// @param start_idx Starting index.
    /// @param target_height Target height.
    /// @return Ancestor index or null_index if not found.
    [[nodiscard]]
    index_t get_ancestor_linear(index_t start_idx, int32_t target_height) const;

    /// Get ancestor at specific height using skip pointers.
    /// O(log n) where n = height difference.
    /// @param start_idx Starting index.
    /// @param target_height Target height.
    /// @return Ancestor index or null_index if not found.
    [[nodiscard]]
    index_t get_ancestor(index_t start_idx, int32_t target_height) const;

    /// Find the common ancestor of two headers.
    /// @param idx_a First header index.
    /// @param idx_b Second header index.
    /// @return Common ancestor index or null_index if none.
    [[nodiscard]]
    index_t find_fork(index_t idx_a, index_t idx_b) const;

    // =========================================================================
    // State Queries
    // =========================================================================

    /// Get the number of headers in the index.
    [[nodiscard]]
    size_t size() const;

    /// Get the capacity of the index.
    [[nodiscard]]
    size_t capacity() const;

    /// Check if index is at warning threshold (95% full).
    [[nodiscard]]
    bool at_capacity_warning() const;

    /// Get estimated memory usage in bytes.
    [[nodiscard]]
    size_t memory_usage() const;

private:
    // Skip list helpers (from BCHN)
    [[nodiscard]]
    static int32_t invert_lowest_one(int32_t n);

    [[nodiscard]]
    static int32_t get_skip_height(int32_t height);

    [[nodiscard]]
    index_t build_skip(index_t parent_idx, int32_t height) const;

    [[nodiscard]]
    index_t get_ancestor_internal(index_t start_idx, int32_t target_height) const;

    // SoA storage - one vector per field
    std::vector<hash_digest> hashes_;  // Block hash (stored, not recomputed)
    std::vector<hash_digest> prev_block_hashes_;
    std::vector<index_t> parent_indices_;
    std::vector<index_t> skip_indices_;
    std::vector<int32_t> heights_;
    std::vector<uint64_t> chain_works_;
    // TODO(fernando): if we find data races causing issues, consider using atomics again
    // std::unique_ptr<std::atomic<uint32_t>[]> statuses_;  // atomic array for lock-free status updates
    std::vector<uint32_t> statuses_;
    std::vector<uint32_t> versions_;
    std::vector<hash_digest> merkle_roots_;
    std::vector<uint32_t> timestamps_;
    std::vector<uint32_t> bits_;
    std::vector<uint32_t> nonces_;

    // Block file location (flat file storage)
    std::vector<int16_t> file_numbers_;    // -1 = no data
    std::vector<uint32_t> data_positions_; // Position in blk*.dat
    std::vector<uint32_t> undo_positions_; // Position in rev*.dat

    // Index counter
    std::atomic<index_t> next_idx_{0};

    // Concurrent hash map - provides synchronization
    boost::concurrent_flat_map<hash_digest, index_t, hash_digest_hasher> hash_to_idx_;

    // Configuration
    size_t const capacity_;
    size_t const warn_threshold_;
};

// =============================================================================
// Template Implementation
// =============================================================================

template <typename Visitor>
void header_index::traverse_back(index_t start_idx, size_t max_steps, Visitor&& visitor) const {
    if (start_idx == null_index) {
        return;
    }

    index_t idx = start_idx;
    size_t steps = 0;

    while (idx != null_index && (max_steps == 0 || steps < max_steps)) {
        visitor(idx);
        idx = parent_indices_[idx];
        ++steps;
    }
}

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_HEADER_INDEX_HPP
