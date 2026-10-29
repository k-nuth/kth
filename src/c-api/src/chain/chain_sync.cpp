// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_sync.h>
#include "chain_sync_private.hpp"

#include <array>
#include <cstdio>
#include <future>
#include <memory>
#include <system_error>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/message/merkle_block.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/capi/detail/mining_template.hpp>

#include <kth/capi/domain/chain/block_list.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// kth_chain_sync_block_height is generated (chain_query.cpp).

// The confirmed address index (spend / history / confirmed transactions) was
// backed by the LMDB spend and history stores, which the v1 node no longer
// maintains. Those queries were removed from block_chain; a v1 address index
// will be reintroduced here if/when it is needed.

// ------------------------------------------------------------------

kth_mempool_transaction_list_t kth_chain_sync_mempool_transactions(kth_chain_t chain, kth_payment_address_t address, kth_bool_t use_testnet_rules) {
    // `address` is an opaque handle; if the caller managed to get one,
    // it's a valid payment_address by construction. Empty-payload
    // legacy branch dropped along with `.valid()`.
    auto const& address_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(address);
    auto txs = safe_chain(chain).get_mempool_transactions(address_cpp.encoded_cashaddr(false), kth::int_to_bool(use_testnet_rules));
    return kth::to_owned(std::move(txs));
}

kth_transaction_list_mut_t kth_chain_sync_mempool_transactions_from_wallets(kth_chain_t chain, kth_payment_address_list_t addresses, kth_bool_t use_testnet_rules) {
    auto const& addresses_cpp = *static_cast<std::vector<kth::domain::wallet::payment_address> const*>(addresses);
    auto txs = safe_chain(chain).get_mempool_transactions_from_wallets(addresses_cpp, kth::int_to_bool(use_testnet_rules));
    return kth::to_owned(std::move(txs));
}

} // extern "C"
