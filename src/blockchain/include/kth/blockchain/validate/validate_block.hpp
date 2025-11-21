// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_BLOCK_HPP
#define KTH_BLOCKCHAIN_VALIDATE_BLOCK_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/populate/populate_block.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

#if defined(KTH_WITH_MEMPOOL)
#include <kth/mining/mempool.hpp>
#endif

namespace kth::blockchain {

/// This class is NOT thread safe.
struct KB_API validate_block {
    using result_handler = handle0;

#if defined(KTH_WITH_MEMPOOL)
    validate_block(dispatcher& dispatch, fast_chain const& chain, settings const& settings, domain::config::network network, bool relay_transactions, mining::mempool const& mp);
#else
    validate_block(dispatcher& dispatch, fast_chain const& chain, settings const& settings, domain::config::network network, bool relay_transactions);
#endif

    void start();
    void stop();

    void check(block_const_ptr block, result_handler handler) const;
    void accept(branch::const_ptr branch, result_handler handler) const;
    void connect(branch::const_ptr branch, result_handler handler) const;

protected:
    inline
    bool stopped() const {
        return stopped_;
    }

    float hit_rate() const;

private:
    using atomic_counter = std::atomic<size_t>;
    using atomic_counter_ptr = std::shared_ptr<atomic_counter>;

    static
    void dump(code const& ec, const domain::chain::transaction& tx, uint32_t input_index, uint32_t forks, size_t height);

    void check_block(block_const_ptr block, size_t bucket, size_t buckets, result_handler handler) const;
    void handle_checked(code const& ec, block_const_ptr block, result_handler handler) const;
    void handle_populated(code const& ec, block_const_ptr block, result_handler handler) const;
    void accept_transactions(block_const_ptr block, size_t bucket, size_t buckets, atomic_counter_ptr sigops, bool bip16, bool bip141, result_handler handler) const;
    void handle_accepted(code const& ec, block_const_ptr block, atomic_counter_ptr sigops, bool bip141, result_handler handler) const;
    void connect_inputs(block_const_ptr block, size_t bucket, size_t buckets, result_handler handler) const;
    void handle_connected(code const& ec, block_const_ptr block, result_handler handler) const;

    // These are thread safe.
    std::atomic<bool> stopped_;
    fast_chain const& fast_chain_;
    domain::config::network network_;
    dispatcher& priority_dispatch_;
    mutable atomic_counter hits_;
    mutable atomic_counter queries_;

    // Caller must not invoke accept/connect concurrently.
    populate_block block_populator_;
};

} // namespace kth::blockchain

#endif
