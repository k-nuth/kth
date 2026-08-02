// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/block_organizer.hpp>

#include <cstddef>
#include <utility>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/block_pool.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace kd::config;

// Single-block acceptance (organize) ran on the pre-v1 LMDB storage (validate_block
// over the LMDB utxo_pool, reorg pool) and was removed with that storage; the v1
// node validates through validate_block_batch + utxo_build. What remains here is
// the block-pool get_data filter and the block-broadcast subscription.

block_organizer::block_organizer(prioritized_mutex& mutex, executor_type executor, size_t threads, threadpool& thread_pool, block_chain& chain, settings const& settings, domain::config::network network, bool relay_transactions)
    : chain_(chain)
    , mutex_(mutex)
    , stopped_(true)
    , executor_(std::move(executor))
    , threads_(threads)
    , block_pool_(settings.reorganization_limit, chain_.block_validations())
    , broadcaster_(executor_)
{
    (void)thread_pool;
    (void)network;
    (void)relay_transactions;
}

bool block_organizer::stopped() const {
    return stopped_;
}

bool block_organizer::start() {
    stopped_ = false;
    broadcaster_.start();
    return true;
}

bool block_organizer::stop() {
    broadcaster_.stop();
    stopped_ = true;
    return true;
}

block_organizer::block_broadcaster::channel_ptr block_organizer::subscribe() {
    return broadcaster_.subscribe();
}

void block_organizer::unsubscribe(block_broadcaster::channel_ptr const& channel) {
    broadcaster_.unsubscribe(channel);
}

void block_organizer::filter(get_data_ptr message) const {
    block_pool_.filter(message);
}

} // namespace kth::blockchain
