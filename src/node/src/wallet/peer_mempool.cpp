// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wallet support answered from peers: the mempool copy, filled by ordinary
// relay (every announced transaction is fetched, and only what was asked
// for is taken), double-spend proofs about it, and broadcast.

#include "peer_source.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/node/wallet/blocks.hpp>

namespace kth::node::wallet::detail {

using network::raw_message;
using domain::chain::point;

std::optional<uint64_t> peer_impl::known_value(point const& outpoint) const {
    if (auto const found = recent_outputs.find(outpoint); found != recent_outputs.end()) return found->second;
    for (auto const& [key, state] : targets) {
        if (auto const owned = state.coins().find(outpoint); owned != state.coins().end()) return owned->second.value;
    }
    return std::nullopt;
}

void peer_impl::on_mempool_tx(domain::chain::transaction tx) {
    // A coinbase or a transaction without inputs or outputs is no mempool
    // transaction.
    if (tx.is_coinbase() || tx.inputs().empty() || tx.outputs().empty()) return;
    auto const size_before = pool.size();
    auto const txid = tx.hash();
    if ( ! pool.insert(std::move(tx), [this](point const& outpoint) { return known_value(outpoint); })) return;
    resolve_dsproofs(false);
    // Room made by dropping older transactions touches what they touched.
    auto const* entry = pool.find(txid);
    if (entry == nullptr || pool.size() != size_before + 1) {
        changes.bump();
        return;
    }
    auto const keys = touched_by(entry->tx, pool, targets);
    changes.touch(keys);
}

// A proof shows nothing until a transaction of the mempool copy spends its
// outpoint (it may arrive first), and goes at the next block if none does.
// At most dsproof_limit are kept, the oldest out first.
void peer_impl::on_dsproof(message::double_spend_proof const& proof, data_chunk const& raw) {
    auto const dspid = proof.hash();
    dsproof_info info{dspid, null_hash, raw, point(proof.out_point().hash(), proof.out_point().index()), {}};
    describe(info, pool);
    // Who follows the proof, or a transaction it is about.
    std::vector<hash_digest> keys(info.descendants.begin(), info.descendants.end());
    keys.push_back(dspid);
    if (dsproofs.insert_or_assign(dspid, std::move(info)).second) dsproof_order.push_back(dspid);
    while (dsproof_order.size() > dsproof_limit) {
        dsproofs.erase(dsproof_order.front());
        dsproof_order.pop_front();
    }
    changes.touch(keys);
}

// Proofs follow the mempool: descendants that arrive later join, proofs
// whose transaction left it go.
void peer_impl::resolve_dsproofs(bool drop_unknown) {
    for (auto it = dsproofs.begin(); it != dsproofs.end();) {
        describe(it->second, pool);
        if (drop_unknown && it->second.txid == null_hash) {
            it = dsproofs.erase(it);
        } else {
            ++it;
        }
    }
    std::erase_if(dsproof_order, [this](hash_digest const& dspid) { return ! dsproofs.contains(dspid); });
}

// Over a connection of its own, so the peers that serve this service's
// headers and filters do not learn it sent the transaction. A pong after it
// says the peer read it, and a reject comes before the pong. The answer is
// the transaction's hash once it was sent: a slow announcement by others is
// no failure, which could make a wallet pay twice.
::asio::awaitable<std::expected<hash_digest, std::error_code>> peer_impl::broadcast(domain::chain::transaction tx) {
    auto const txid = tx.hash();
    auto const link = co_await connect_isolated();
    if ( ! link) co_return std::unexpected(error::channel_stopped);
    pending_broadcasts.insert(txid);
    struct forget {
        peer_impl& self;
        hash_digest txid;
        std::shared_ptr<peer_link> link;
        ~forget() {
            self.pending_broadcasts.erase(txid);
            self.rejections.erase(txid);
            if (link->session) link->session->stop();
        }
    } guard{*this, txid, link};
    auto const sent = co_await send(*link, message::transaction(tx));
    if (sent) co_return std::unexpected(sent);
    auto const nonce = next_nonce();
    auto const pong = co_await request(link, message::ping(nonce), [nonce, version = link->session->negotiated_version()](raw_message const& m) {
        if (m.heading.command() != message::pong::command) return step::skip;
        byte_reader reader(m.payload);
        auto const parsed = message::pong::from_data(reader, version);
        return parsed && parsed->nonce() == nonce ? step::done : step::skip;
    });
    if (auto const reason = rejections.find(txid); reason != rejections.end()) {
        last_error = "transaction rejected: " + reason->second;
        co_return std::unexpected(error::operation_failed);
    }
    if ( ! pong) spdlog::debug("[wallet] No pong after broadcasting {}", encode_hash(txid));
    on_mempool_tx(std::move(tx));
    co_return txid;
}

// The median of what connected peers say their mempools take at least
// (their fee filters), never below the network's floor nor above
// maximum_relay_fee: one peer cannot raise what a wallet pays.
fee_estimate peer_impl::fees() const {
    std::vector<uint64_t> rates;
    for (auto const& link : links) {
        if (link->closed) continue;
        if (auto const rate = fee_filters.find(link->id); rate != fee_filters.end()) rates.push_back(rate->second);
    }
    uint64_t relay = default_relay_fee;
    if ( ! rates.empty()) {
        std::ranges::sort(rates);
        relay = std::clamp(rates[rates.size() / 2], default_relay_fee, maximum_relay_fee);
    }
    return fee_estimate{relay, relay};
}

mempool_summary peer_impl::mempool() const {
    mempool_summary out{pool.size(), pool.bytes(), {}};
    for (auto const& [txid, entry] : pool.transactions()) {
        if (entry.fee) out.fee_rates.emplace_back(double(*entry.fee) / double(std::max<size_t>(1, entry.size)), entry.size);
    }
    return out;
}

std::vector<rpa_match> peer_impl::rpa_mempool(uint16_t prefix, uint8_t bits) const {
    std::vector<rpa_match> out;
    for (auto const& [txid, entry] : pool.transactions()) {
        auto inputs = prefix_inputs(entry.tx, prefix, bits);
        if ( ! inputs.empty()) out.push_back(rpa_match{txid, std::nullopt, std::move(inputs)});
    }
    return out;
}

::asio::awaitable<std::optional<dsproof_info>> peer_impl::dsproof(hash_digest id) {
    if (auto const found = dsproofs.find(id); found != dsproofs.end()) co_return found->second;
    for (auto const& [dspid, info] : dsproofs) {
        if (std::ranges::find(info.descendants, id) != info.descendants.end()) co_return info;
    }
    co_return std::nullopt;
}

::asio::awaitable<std::vector<hash_digest>> peer_impl::dsproof_transactions() {
    std::set<hash_digest> all;
    for (auto const& [dspid, info] : dsproofs) all.insert(info.descendants.begin(), info.descendants.end());
    co_return std::vector<hash_digest>(all.begin(), all.end());
}

} // namespace kth::node::wallet::detail
