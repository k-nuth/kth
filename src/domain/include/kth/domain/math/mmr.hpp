// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MATH_MMR_HPP
#define KTH_DOMAIN_MATH_MMR_HPP

#include <cstddef>
#include <cstdint>
#include <optional>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/hash_define.hpp>

namespace kth::domain {

/// All hashes use internal byte order, as returned by bitcoin_hash. Reverse
/// display-order hashes (including Electrum branches) at the protocol boundary.
[[nodiscard]] KD_API hash_digest mmr_node_hash(hash_digest const& left, hash_digest const& right);

/// Verify a Bitcoin-style path, including the duplicate-subtree guard.
[[nodiscard]] KD_API bool verify_mmr_proof(uint64_t index, uint64_t leaf_count,
    hash_digest const& leaf, hash_list const& siblings, hash_digest const& expected_root);

/// Pure SHV server storage: only n - popcount(n) internal nodes, in creation order.
/// Leaf arguments must come from the same sequence as the stored nodes. Zero
/// hashes are valid values, not sentinels. Sizes count leaves, not block heights.
class KD_API mmr {
public:
    [[nodiscard]] uint64_t leaf_count() const noexcept { return leaf_count_; }
    [[nodiscard]] size_t node_count() const noexcept { return nodes_.size(); }

    /// previous_leaf is required only when the old leaf count is odd.
    /// The count must be less than UINT64_MAX; storage allocation may throw.
    void append(hash_digest const& leaf, hash_digest const& previous_leaf = null_hash);

    /// Remove the last count leaves. An excessive count leaves the state unchanged.
    [[nodiscard]] bool remove(uint64_t count) noexcept;

    /// Query any stored prefix. last_leaf is required only for an odd prefix.
    /// Out-of-range prefixes return nullopt; the empty prefix has the zero root.
    [[nodiscard]] std::optional<hash_list> peaks(uint64_t leaf_count, hash_digest const& last_leaf = null_hash) const;
    [[nodiscard]] std::optional<hash_digest> root(uint64_t leaf_count, hash_digest const& last_leaf = null_hash) const;

    /// Siblings run from leaf level upward. leaf_sibling is required unless the
    /// leaf is a lone peak; proof_to_root also needs last_leaf for odd prefixes.
    /// Invalid indices or prefixes return nullopt, distinct from a valid empty proof.
    [[nodiscard]] std::optional<hash_list> proof_to_peak(uint64_t index, uint64_t leaf_count,
        hash_digest const& leaf_sibling = null_hash) const;
    [[nodiscard]] std::optional<hash_list> proof_to_root(uint64_t index, uint64_t leaf_count,
        hash_digest const& leaf_sibling = null_hash, hash_digest const& last_leaf = null_hash) const;

    [[nodiscard]] bool operator==(mmr const&) const = default;

private:
    uint64_t leaf_count_{0};
    hash_list nodes_;
};

/// Client state consists only of the leaf count and peaks, tallest first.
/// Hash byte order and leaf-count conventions are the same as for mmr.
class KD_API mmr_accumulator {
public:
    [[nodiscard]] uint64_t leaf_count() const noexcept { return leaf_count_; }
    [[nodiscard]] hash_list const& peaks() const noexcept { return peaks_; }
    [[nodiscard]] hash_digest root() const;

    /// Rejects count overflow without changing the state.
    [[nodiscard]] bool extend(hash_digest const& leaf);

    /// Import trusted peaks, rejecting a count inconsistent with their number.
    [[nodiscard]] static std::optional<mmr_accumulator> from_peaks(uint64_t leaf_count, hash_list peaks);

    /// Peak proofs use trusted peak structure, so duplicate subtrees are allowed.
    [[nodiscard]] bool verify_proof_to_peak(uint64_t index, hash_digest const& leaf, hash_list const& siblings) const;
    [[nodiscard]] bool verify_proof_to_root(uint64_t index, hash_digest const& leaf, hash_list const& siblings) const;

    /// Recover peaks from the last leaf's proof, checking structure, padding,
    /// duplicate subtrees and the supplied trusted root before accepting state.
    [[nodiscard]] static std::optional<mmr_accumulator> bootstrap(uint64_t leaf_count,
        hash_digest const& last_leaf, hash_list const& siblings, hash_digest const& expected_root);

    [[nodiscard]] bool operator==(mmr_accumulator const&) const = default;

private:
    uint64_t leaf_count_{0};
    hash_list peaks_;
};

} // namespace kth::domain

#endif // KTH_DOMAIN_MATH_MMR_HPP
