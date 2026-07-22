// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_MINING_TEMPLATE_HPP_
#define KTH_CAPI_DETAIL_MINING_TEMPLATE_HPP_

#include <cstring>
#include <utility>
#include <vector>

#include <kth/blockchain/pools/block_template.hpp>
#include <kth/domain/chain/transaction.hpp>

#include <kth/capi/chain/mining_template.h>
#include <kth/capi/handles.h>
#include <kth/capi/helpers.hpp>

namespace kth::capi {

// Fill the C header POD from a C++ mining_template and hand the caller an owned
// transaction list built from the selection. Shared by the sync and async
// wrappers. mining_template is not trivially copyable (it owns the selection
// vector), so the header fields are copied one by one rather than memcpy'd.
inline void fill_mining_template(
    kth_mining_template_t& out,
    kth_transaction_list_mut_t& out_txs,
    kth::blockchain::mining_template const& tmpl) {

    out.version = tmpl.version;
    std::memcpy(out.previous_block_hash.hash,
        tmpl.previous_block_hash.data(), tmpl.previous_block_hash.size());
    out.height = tmpl.height;
    out.bits = tmpl.bits;
    out.min_time = tmpl.min_time;
    out.current_time = tmpl.current_time;
    out.coinbase_value = tmpl.coinbase_value;
    out.size_limit = tmpl.size_limit;
    out.sigchecks_limit = tmpl.sigchecks_limit;

    std::vector<kth::domain::chain::transaction> txs;
    txs.reserve(tmpl.selection.txs.size());
    for (auto const& tx : tmpl.selection.txs) {
        txs.push_back(*tx); // slice message::transaction -> chain::transaction
    }
    out_txs = kth::leak(std::move(txs));
}

} // namespace kth::capi

#endif // KTH_CAPI_DETAIL_MINING_TEMPLATE_HPP_
