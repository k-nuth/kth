// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_BLOCK_ORGANIZER_HPP
#define KTH_BLOCKCHAIN_BLOCK_ORGANIZER_HPP

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/block_pool.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

#include <kth/infrastructure/utility/prioritized_mutex.hpp>

#include <kth/infrastructure/utility/threadpool.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#include <kth/infrastructure/utility/broadcaster.hpp>

namespace kth::blockchain {

// Forward declaration
struct block_chain;

/// This class is thread safe.
/// Organises blocks via the block pool to the blockchain.
struct KB_API block_organizer {
    using executor_type = ::asio::any_io_executor;
    using ptr = std::shared_ptr<block_organizer>;
    using block_handler = std::function<bool(code, size_t, block_const_ptr_list_const_ptr, block_const_ptr_list_const_ptr)>;
    using block_broadcaster = broadcaster<size_t, block_const_ptr_list_const_ptr, block_const_ptr_list_const_ptr>;

    /// Construct an instance.
    block_organizer(prioritized_mutex& mutex, executor_type executor, size_t threads, threadpool& thread_pool, block_chain& chain, settings const& settings, domain::config::network network, bool relay_transactions);

    bool start();
    bool stop();

    [[nodiscard]]
    block_broadcaster::channel_ptr subscribe();
    void unsubscribe(block_broadcaster::channel_ptr const& channel);

    /// Remove all message vectors that match block hashes.
    void filter(get_data_ptr message) const;

protected:
    bool stopped() const;

private:
    // This must be protected by the implementation.
    block_chain& chain_;

    // These are thread safe.
    prioritized_mutex& mutex_;
    std::atomic<bool> stopped_;
    executor_type executor_;
    size_t threads_;
    block_pool block_pool_;
    block_broadcaster broadcaster_;

};

} // namespace kth::blockchain

#endif
