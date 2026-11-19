// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_COMPACT_FILTER_PROOF_HPP
#define KTH_DOMAIN_MESSAGE_COMPACT_FILTER_PROOF_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <kth/domain/chain/script_filter.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/message/compact_filter_tree.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth::domain::message {

// Where a spent output was created: its transaction, at `transaction_index`
// in block `block_hash`, with the Merkle branch of that transaction there.
struct KD_API compact_filter_proof_origin {
    hash_digest block_hash{null_hash};
    uint32_t transaction_index{0};
    chain::transaction transaction;
    std::vector<hash_digest> transaction_branch;

    [[nodiscard]]
    friend bool operator==(compact_filter_proof_origin const&, compact_filter_proof_origin const&) = default;
};

// `cfproof`, the answer to a `getcfproof`: a leaf with its branch in the entry
// tree, the transaction it names with that transaction's Merkle branch in the
// block and, for the leaf of a spent output, the transaction of that output
// with its own block and Merkle branch. A wallet that has the block headers
// checks the leaf against them without downloading either block.
struct KD_API compact_filter_proof {
    using ptr = std::shared_ptr<compact_filter_proof>;
    using const_ptr = std::shared_ptr<const compact_filter_proof>;

    uint8_t filter_type{0};
    hash_digest block_hash{null_hash};

    // The number of leaves in the tree, and the index of `leaf` among them.
    uint32_t leaf_count{0};
    uint32_t leaf_index{0};
    chain::script_leaf leaf{};

    // Rebuilds the root of the tree from `leaf`, level by level from the
    // leaves up.
    std::vector<hash_digest> leaf_branch;

    // The transaction at the leaf's transaction index, and its Merkle branch
    // to the block's Merkle root.
    chain::transaction transaction;
    std::vector<hash_digest> transaction_branch;

    // There exactly when the leaf is of a spent output.
    std::optional<compact_filter_proof_origin> origin;

    [[nodiscard]]
    friend bool operator==(compact_filter_proof const&, compact_filter_proof const&) = default;

    static expect<compact_filter_proof> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static std::string const command;
    static uint32_t const version_minimum;
    static uint32_t const version_maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_COMPACT_FILTER_PROOF_HPP
