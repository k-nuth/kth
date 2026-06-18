// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/transaction_pool.hpp>

#include <cstddef>
#include <expected>
#include <memory>

#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

// Duplicate tx hashes are disallowed in a block and therefore same in pool.
// A transaction hash that exists unspent in the chain is still not acceptable
// even if the original becomes spent in the same block, because the BIP30
// exmaple implementation simply tests all txs in a new block against
// transactions in previous blocks.

transaction_pool::transaction_pool(settings const& /*settings*/)
{}

// TODO(legacy): implement block template discovery.
awaitable_expected<std::pair<merkle_block_ptr, size_t>>
transaction_pool::fetch_template() const {
    size_t const height = max_size_t;
    auto const block = std::make_shared<domain::message::merkle_block>();
    co_return std::pair{block, height};
}

// TODO(legacy): implement mempool message payload discovery.
awaitable_expected<inventory_ptr>
transaction_pool::fetch_mempool(size_t /*maximum*/) const {
    auto const empty = std::make_shared<domain::message::inventory>();
    co_return empty;
}

} // namespace kth::blockchain
