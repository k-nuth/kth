// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TRANSACTION_POOL_HPP
#define KTH_BLOCKCHAIN_TRANSACTION_POOL_HPP

#include <cstddef>
#include <cstdint>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/safe_chain.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

/// TODO: this class is not implemented or utilized.
struct KB_API transaction_pool {
    using inventory_fetch_handler = safe_chain::inventory_fetch_handler;
    using merkle_block_fetch_handler = safe_chain::merkle_block_fetch_handler;

    transaction_pool(settings const& settings);

    void fetch_template(merkle_block_fetch_handler) const;
    void fetch_mempool(size_t maximum, inventory_fetch_handler) const;

////private:
////    bool const reject_conflicts_;
////    const uint64_t minimum_fee_;
};

} // namespace kth::blockchain

#endif
