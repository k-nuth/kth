// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/transaction_relay.hpp>

#include <memory>
#include <utility>

#include <spdlog/spdlog.h>

namespace kth::node {

using domain::message::inventory_vector;

recent_txids::recent_txids(std::size_t capacity)
    : capacity_(capacity) {
}

bool recent_txids::remember(hash_digest const& txid) {
    std::lock_guard lock(mutex_);
    if ( ! set_.insert(txid).second) {
        return true;
    }
    order_.push_back(txid);
    if (order_.size() > capacity_) {
        set_.erase(order_.front());
        order_.pop_front();
    }
    return false;
}

inventory_vector::list transactions_to_request(
    domain::message::inventory const& inv,
    blockchain::mempool const& pool, recent_txids& recent) {

    inventory_vector::list wanted;
    for (auto const& item : inv.inventories()) {
        if (item.type() != inventory_vector::type_id::transaction) {
            continue;
        }
        if (pool.contains(item.hash()) || recent.remember(item.hash())) {
            continue;
        }
        wanted.push_back(item);
    }
    return wanted;
}

transaction_relay::transaction_relay(blockchain::block_chain& chain)
    : chain_(chain)
    , recent_(remembered_txids) {
}

::asio::awaitable<message_result> transaction_relay::on_inventory(
    network::peer_session& peer, domain::message::inventory const& inv) {

    if ( ! chain_.synchronization().synchronized()) {
        co_return message_result::handled;
    }

    auto wanted = transactions_to_request(inv, chain_.mempool_ref(), recent_);
    if (wanted.empty()) {
        co_return message_result::handled;
    }

    auto const count = wanted.size();
    auto request = domain::message::get_data::create(std::move(wanted));
    if ( ! request) {
        spdlog::debug("[relay] Not requesting {} transactions from [{}]: {}",
            count, peer.authority(), request.error().message());
        co_return message_result::handled;
    }
    co_await peer.send(*request);
    co_return message_result::handled;
}

::asio::awaitable<message_result> transaction_relay::on_transaction(
    network::peer_session& peer, domain::message::transaction const& tx) {

    auto const pooled = std::make_shared<domain::message::transaction const>(tx);
    recent_.remember(pooled->hash());
    auto const ec = co_await chain_.organize(pooled);
    if (ec) {
        spdlog::debug("[relay] Transaction {} from [{}] not admitted: {}",
            encode_hash(pooled->hash()), peer.authority(), ec.message());
    }
    co_return message_result::handled;
}

} // namespace kth::node
