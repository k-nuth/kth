// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_FAST_CHAIN_HPP
#define KTH_BLOCKCHAIN_FAST_CHAIN_HPP

#include <cstddef>
#include <kth/database.hpp>
#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/branch.hpp>

namespace kth::blockchain {

/// A low level interface for encapsulation of the blockchain database.
/// Caller must ensure the database is not otherwise in use during these calls.
/// Implementations are NOT expected to be thread safe with the exception
/// that the import method may itself be called concurrently.
class KB_API fast_chain {
public:
    // This avoids conflict with the result_handler in safe_chain.
    using complete_handler = handle0;

    // Readers.
    // ------------------------------------------------------------------------

    /// Get position data for a transaction.
    virtual bool get_transaction_position(size_t& out_height, size_t& out_position, hash_digest const& hash, bool require_confirmed) const = 0;

    /// Get the output that is referenced by the outpoint.
    virtual bool get_output(domain::chain::output& out_output, size_t& out_height, uint32_t& out_median_time_past, bool& out_coinbase, const domain::chain::output_point& outpoint, size_t branch_height, bool require_confirmed) const = 0;

    /// Get a determination of whether the block hash exists in the store.
    virtual bool get_block_exists(hash_digest const& block_hash) const = 0;

    /// Get the hash of the block if it exists.
    virtual bool get_block_hash(hash_digest& out_hash, size_t height) const = 0;

    /// Get the work of the branch starting at the given height.
    virtual bool get_branch_work(uint256_t& out_work, uint256_t const& maximum, size_t from_height) const = 0;

    /// Get the header of the block at the given height.
    virtual bool get_header(domain::chain::header& out_header, size_t height) const = 0;

    /// Get the header of the block with the given height, also the ABLA state.
    virtual std::optional<database::header_with_abla_state_t> get_header_and_abla_state(size_t height) const = 0;

    /// Get a sequence of block headers [from, to].
    virtual domain::chain::header::list get_headers(size_t from, size_t to) const = 0;

    /// Get the height of the block with the given hash.
    virtual bool get_height(size_t& out_height, hash_digest const& block_hash) const = 0;

    /// Get the bits of the block with the given height.
    virtual bool get_bits(uint32_t& out_bits, size_t height) const = 0;

    /// Get the timestamp of the block with the given height.
    virtual bool get_timestamp(uint32_t& out_timestamp, size_t height) const = 0;

    /// Get the version of the block with the given height.
    virtual bool get_version(uint32_t& out_version, size_t height) const = 0;

    /// Get height of latest block.
    virtual bool get_last_height(size_t& out_height) const = 0;

    /// Get the output that is referenced by the outpoint in the UTXO Set.
    virtual bool get_utxo(domain::chain::output& out_output, size_t& out_height, uint32_t& out_median_time_past, bool& out_coinbase, domain::chain::output_point const& outpoint, size_t branch_height) const = 0;

    /// Get a UTXO subset from the reorganization pool, [from, to] the specified heights.
    virtual std::pair<bool, database::internal_database::utxo_pool_t> get_utxo_pool_from(uint32_t from, uint32_t to) const = 0;

#if ! defined(KTH_DB_READONLY)
    virtual void prune_reorg_async() = 0;
#endif

    //virtual void set_database_flags() = 0;

    /////// Get the transaction of the given hash and its block height.
    ////virtual transaction_ptr get_transaction(size_t& out_block_height,
    ////    hash_digest const& hash, bool require_confirmed) const = 0;

    // Writers.
    // ------------------------------------------------------------------------

#if ! defined(KTH_DB_READONLY)
    /// Insert a block to the blockchain, height is checked for existence.
    // virtual bool insert(block_const_ptr block, size_t height, int) = 0;
    virtual bool insert(block_const_ptr block, size_t height) = 0;

    /// Push an unconfirmed transaction to the tx table and index outputs.
    virtual void push(transaction_const_ptr tx, dispatcher& dispatch,
        complete_handler handler) = 0;

    /// Swap incoming and outgoing blocks, height is validated.
    virtual void reorganize(const infrastructure::config::checkpoint& fork_point,
        block_const_ptr_list_const_ptr incoming_blocks,
        block_const_ptr_list_ptr outgoing_blocks, dispatcher& dispatch,
        complete_handler handler) = 0;

#endif //! defined(KTH_DB_READONLY)

    // Properties
    // ------------------------------------------------------------------------

    /// Get a reference to the chain state relative to the next block.
    virtual domain::chain::chain_state::ptr chain_state() const = 0;

    /// Get a reference to the chain state relative to the next block.
    virtual domain::chain::chain_state::ptr chain_state(branch::const_ptr branch) const = 0;

    virtual bool is_stale_fast() const = 0;
};

} // namespace kth::blockchain

#endif
