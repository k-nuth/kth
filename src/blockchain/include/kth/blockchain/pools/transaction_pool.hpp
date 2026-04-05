// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TRANSACTION_POOL_HPP
#define KTH_BLOCKCHAIN_TRANSACTION_POOL_HPP

#include <cstddef>
#include <cstdint>
#include <expected>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

#include <kth/infrastructure/handlers.hpp>

#include <asio/awaitable.hpp>

namespace kth::blockchain {

using kth::awaitable_expected;

/// TODO: this class is not implemented or utilized.
struct KB_API transaction_pool {
    transaction_pool(settings const& settings);

    [[nodiscard]]
    awaitable_expected<std::pair<merkle_block_ptr, size_t>> fetch_template() const;

    [[nodiscard]]
    awaitable_expected<inventory_ptr> fetch_mempool(size_t maximum) const;
};

} // namespace kth::blockchain

#endif
