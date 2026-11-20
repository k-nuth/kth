// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_BLOCK_HPP
#define KTH_DOMAIN_CHAIN_BLOCK_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <vector>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/config/network.hpp>


#include <kth/domain/concepts.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

using indexes = std::vector<size_t>;

struct KD_API block {
    using list = std::vector<block>;
    using indexes = std::vector<size_t>;

    // Constructors.
    //-------------------------------------------------------------------------

    // A block is a pure structural aggregate of a header and its transactions
    // and performs no consensus checks, so construction cannot fail and every
    // state is syntactically valid — including the all-default one and the
    // empty block. Consensus validity is validation's concern.
    block() = default;
    block(chain::header header, transaction::list transactions);

    // Operators.
    //-------------------------------------------------------------------------

    bool operator==(block const& x) const = default;
    bool operator!=(block const& x) const = default;

    // Serialization.
    //-------------------------------------------------------------------------

    static
    expect<block> from_data(byte_reader& reader);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer) const noexcept;

    [[nodiscard]]
    size_t serialized_size() const;

    // Properties (accessors).
    //-------------------------------------------------------------------------

    [[nodiscard]]
    chain::header const& header() const;

    transaction::list& transactions();

    [[nodiscard]]
    transaction::list const& transactions() const;

    void set_transactions(transaction::list const& value);
    void set_transactions(transaction::list&& value);

    [[nodiscard]]
    hash_digest hash() const;

    [[nodiscard]]
    hash_list to_hashes() const;

    // The overloads below taking `txids` read the transaction identifiers from
    // it instead of hashing each transaction again: computing an identifier
    // serializes the transaction, so a caller that needs them in several checks
    // computes them once with to_hashes() and passes them along. `txids` must be
    // this block's to_hashes(); the overloads without it compute them once.

    // Utilities.
    //-------------------------------------------------------------------------

    static
    block genesis_mainnet();

    static
    block genesis_testnet();

    static
    block genesis_regtest();

#if defined(KTH_CURRENCY_BCH)
    static
    block genesis_testnet4();

    static
    block genesis_scalenet();

    static
    block genesis_chipnet();
#endif

    /// The genesis block of `network`. Tempnet starts from chipnet's.
    static
    block genesis(domain::config::network network);

    static
    size_t locator_size(size_t top);

    static
    indexes locator_heights(size_t top);

    // Validation.
    //-------------------------------------------------------------------------

    static
    uint64_t subsidy(size_t height, bool retarget);

    /// Subsidy of block `height` on a branch with the given tick schedule
    /// (CHIP-2025-03): the halving count is keyed to the 600-tick chunk
    /// ceil(tick / 600) containing the block's interval and the amount is
    /// scaled by the block's interval, ((50 BCH) >> halvings) * tau / 600.
    static
    uint64_t subsidy(block_ticks const& ticks, size_t height, bool retarget);

    static
    uint256_t proof(uint32_t bits);

    [[nodiscard]]
    uint64_t fees() const;

    [[nodiscard]]
    uint64_t claim() const;

    [[nodiscard]]
    uint64_t reward(size_t height) const;

    [[nodiscard]]
    uint64_t reward(block_ticks const& ticks, size_t height) const;

    [[nodiscard]]
    uint256_t proof() const;

    [[nodiscard]]
    hash_digest generate_merkle_root() const;

    [[nodiscard]]
    hash_digest generate_merkle_root(std::span<hash_digest const> txids) const;

#if defined(KTH_WITH_NEUTRINO)
    // The Merkle branch of the transaction at `index`: the hash of its sibling
    // at each level, from the transactions up to just below the root. A light
    // client folds it with the hash of the transaction to the Merkle root of
    // the header (merkle_branch_root). The last node of a level that has no
    // partner is paired with itself, as generate_merkle_root pairs it, so its
    // sibling is its own hash. A block of one transaction has an empty branch,
    // and so has an index past the last transaction, which the caller checks.
    [[nodiscard]]
    hash_list transaction_merkle_branch(size_t index) const;

    [[nodiscard]]
    hash_list transaction_merkle_branch(size_t index, std::span<hash_digest const> txids) const;

    // The root that a transaction hash at `index` and its branch fold to: at
    // each level the hash is the left child where the index is even and the
    // right one where it is odd, and the index is halved. None where a right
    // child would be paired with itself: only a left node is ever paired with
    // itself (the last of an odd level), so such a step claims a position
    // past the last transaction, which folds to the same root as the real one
    // (CVE-2012-2459; as checkTxMerkleBranch in gcash/neutrino#52).
    [[nodiscard]]
    static
    std::optional<hash_digest> merkle_branch_root(hash_digest const& transaction_hash, size_t index, std::span<hash_digest const> branch);
#endif // KTH_WITH_NEUTRINO

    [[nodiscard]]
    size_t signature_operations(bool bip16, bool bip141) const;

    [[nodiscard]]
    size_t total_inputs(bool with_coinbase) const;

    [[nodiscard]]
    bool is_extra_coinbases() const;

    [[nodiscard]]
    bool is_final(size_t height, uint32_t block_time) const;

    [[nodiscard]]
    bool is_final(block_ticks const& ticks, size_t height, uint32_t block_time) const;

    [[nodiscard]]
    bool is_distinct_transaction_set() const;

    [[nodiscard]]
    bool is_distinct_transaction_set(std::span<hash_digest const> txids) const;

    [[nodiscard]]
    bool is_valid_coinbase_claim(size_t height) const;

    [[nodiscard]]
    bool is_valid_coinbase_claim(block_ticks const& ticks, size_t height) const;

    /// CHIP-2025-03: the coinbase shape version 1, locktime 0 and outputs of at
    /// least 25 BCH, which is invalid from the block built on H_a onward. The
    /// caller decides whether the block is governed; this only reads the shape.
    [[nodiscard]]
    bool has_forbidden_coinbase_shape() const;

    // Collision protection once Faster Blocks is active. Mainnet has fixed
    // height gates; other networks immediately use the four-field shape rule.
    [[nodiscard]]
    bool has_forbidden_coinbase_shape(config::network network, size_t height) const;

    [[nodiscard]]
    bool is_valid_coinbase_script(size_t height) const;

    [[nodiscard]]
    bool is_forward_reference() const;

    [[nodiscard]]
    bool is_forward_reference(std::span<hash_digest const> txids) const;

    [[nodiscard]]
    bool is_canonical_ordered() const;

    [[nodiscard]]
    bool is_canonical_ordered(std::span<hash_digest const> txids) const;

    [[nodiscard]]
    bool is_internal_double_spend() const;

    [[nodiscard]]
    bool is_valid_merkle_root() const;

    [[nodiscard]]
    bool is_valid_merkle_root(std::span<hash_digest const> txids) const;

    code check() const;

    /// Check block body only (skip header validation for headers-first sync).
    /// Use this when headers have already been validated during header sync.
    code check_body() const;

    code check_body(std::span<hash_digest const> txids) const;

    [[nodiscard]]
    code check_transactions() const;

    [[nodiscard]]
    size_t non_coinbase_input_count() const;

private:
    chain::header header_;
    transaction::list transactions_;
};

// Non-member functions.
//-------------------------------------------------------------------------

size_t locator_size(size_t top);

indexes locator_heights(size_t top);

size_t total_inputs(block const& blk, bool with_coinbase);

size_t serialized_size(block const& blk);

} // namespace kth::domain::chain

#endif
