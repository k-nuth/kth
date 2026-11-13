// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_CHAIN_MMR_HPP
#define KTH_BLOCKCHAIN_CHAIN_MMR_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <shared_mutex>
#include <vector>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/header_index.hpp>
#include <kth/domain/math/mmr.hpp>

namespace kth::blockchain {

struct shv_proof_result {
    uint64_t block_height;
    domain::chain::header header;
    hash_list proof;
    hash_digest target;
};

/// Why there is no proof for a request. An shv reply carries the reason as
/// the entry's status.
enum class shv_miss {
    not_found,            ///< The connected chain has no such block.
    above_commitment,     ///< The block is above the commitment height.
    commitment_above_tip  ///< The commitment height is above the connected tip.
};

using shv_proof_expect = std::expected<shv_proof_result, shv_miss>;

/// The connected chain, independent of the active header-height mapping. Header
/// entries remain stable when that mapping moves ahead or changes branches.
class KB_API chain_mmr {
public:
    explicit chain_mmr(header_index const& headers) : headers_(headers) {}

    /// Follow this connected tip's ancestry, retaining the common prefix.
    /// Call under the exclusion used to publish the connected chain view.
    [[nodiscard]] bool set_tip(header_index::index_t tip);
    /// The disconnect path has already established that height is its tip.
    void disconnect(uint32_t height);
    [[nodiscard]] uint64_t leaf_count() const;
    [[nodiscard]] std::optional<uint64_t> height(hash_digest const& hash) const;
    [[nodiscard]] std::optional<hash_digest> root(uint64_t height,
        std::optional<hash_digest> expected_hash = std::nullopt) const;
    [[nodiscard]] shv_proof_expect proof(uint64_t height,
        uint64_t commitment_height, bool to_root) const;
    [[nodiscard]] shv_proof_expect proof(hash_digest const& hash,
        uint64_t commitment_height, bool to_root) const;

private:
    [[nodiscard]] std::optional<uint64_t> height_locked(hash_digest const& hash) const;
    [[nodiscard]] shv_proof_expect proof_locked(uint64_t height,
        uint64_t commitment_height, bool to_root) const;

    header_index const& headers_;
    mutable std::shared_mutex mutex_;
    domain::mmr mmr_;
    std::vector<header_index::index_t> leaves_;
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_CHAIN_MMR_HPP
