// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POPULATE_CHAIN_STATE_HPP
#define KTH_BLOCKCHAIN_POPULATE_CHAIN_STATE_HPP

#include <cstddef>
#include <cstdint>

#include <kth/domain.hpp>
#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/settings.hpp>

namespace kth::blockchain {

/// This class is NOT thread safe.
struct KB_API populate_chain_state {
    populate_chain_state(fast_chain const& chain, settings const& settings, domain::config::network network);

    /// Populate chain state for the tx pool (start).
    domain::chain::chain_state::ptr populate() const;

    /// Populate chain state for the top block in the branch (try).
    domain::chain::chain_state::ptr populate(domain::chain::chain_state::ptr pool, branch::const_ptr branch) const;

    /// Populate pool state from the top block (organized).
    domain::chain::chain_state::ptr populate(domain::chain::chain_state::ptr top) const;

private:
    using branch_ptr = branch::const_ptr;
    using map = domain::chain::chain_state::map;
    using data = domain::chain::chain_state::data;

    bool populate_all(data& data, branch_ptr branch) const;
    bool populate_bits(data& data, map const& map, branch_ptr branch) const;
    bool populate_versions(data& data, map const& map, branch_ptr branch) const;
    bool populate_timestamps(data& data, map const& map, branch_ptr branch) const;
    bool populate_collision(data& data, map const& map, branch_ptr branch) const;

#if ! defined(KTH_CURRENCY_BCH)
    bool populate_bip9_bit0(data& data, map const& map, branch_ptr branch) const;
    bool populate_bip9_bit1(data& data, map const& map, branch_ptr branch) const;
#endif

#if defined(KTH_CURRENCY_BCH)
    //domain::chain::chain_state::assert_anchor_block_info_t find_assert_anchor_block(size_t height, domain::config::network network, data const& data, branch_ptr branch) const;
    domain::chain::chain_state::assert_anchor_block_info_t get_assert_anchor_block(domain::config::network network) const;
#endif

    bool get_bits(uint32_t& out_bits, size_t height, branch_ptr branch) const;
    bool get_version(uint32_t& out_version, size_t height, branch_ptr branch) const;
    bool get_timestamp(uint32_t& out_timestamp, size_t height, branch_ptr branch) const;
    bool get_block_hash(hash_digest& out_hash, size_t height, branch_ptr branch) const;

#if defined(KTH_CURRENCY_BCH)
    settings const& settings_;
#endif //KTH_CURRENCY_BCH

    // These are thread safe.
    uint32_t const configured_forks_;
    infrastructure::config::checkpoint::list const checkpoints_;
    domain::config::network const network_;

    // Populate is guarded against concurrent callers but because it uses the fast
    // chain it must not be invoked during chain writes.
    fast_chain const& fast_chain_;
    mutable shared_mutex mutex_;
};

} // namespace kth::blockchain

#endif
