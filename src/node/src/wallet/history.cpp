// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/wallet/history.hpp>

#include <algorithm>
#include <set>
#include <utility>

#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::node::wallet {

using domain::chain::point;
using domain::chain::transaction;

bool keeps(token_filter filter, domain::chain::token_data_opt const& token) {
    switch (filter) {
        case token_filter::exclude: return ! token.has_value();
        case token_filter::only: return token.has_value();
        case token_filter::include: break;
    }
    return true;
}

// target
// ----------------------------------------------------------------------------

target target::of_script(data_chunk script) {
    return target{kind::script, std::move(script)};
}

target target::of_script_hash(hash_digest const& hash) {
    return target{kind::script_hash, data_chunk(hash.begin(), hash.end())};
}

target target::of_category(hash_digest const& category) {
    return target{kind::category, data_chunk(category.begin(), category.end())};
}

namespace {

hash_digest to_digest(data_chunk const& bytes) {
    hash_digest out{};
    std::copy_n(bytes.begin(), std::min(bytes.size(), out.size()), out.begin());
    return out;
}

} // namespace

hash_digest target::key() const {
    switch (what) {
        case kind::script_hash: return to_digest(bytes);
        case kind::category: return domain::chain::category_key(to_digest(bytes));
        case kind::script: break;
    }
    return domain::chain::script_key(bytes);
}

bool target::owns(domain::chain::output const& out) const {
    switch (what) {
        case kind::script_hash: return sha256_hash(out.script().bytes()) == to_digest(bytes);
        case kind::category: return out.token_data().has_value() && out.token_data()->id == to_digest(bytes);
        case kind::script: break;
    }
    return out.script().bytes() == bytes;
}

// target_state
// ----------------------------------------------------------------------------

target_state::target_state(target watched, uint32_t start)
    : target_(std::move(watched)), key_(target_.key()), start_(start) {}

uint32_t target_state::next_height() const {
    return synced_ ? *synced_ + 1 : start_;
}

void target_state::set_synced(uint32_t height) {
    if (height + 1 >= start_) synced_ = height;
}

bool target_state::apply_block(uint32_t height, hash_digest const& block, transaction::list const& txs) {
    std::map<uint32_t, confirmed_tx> entries;
    auto const entry = [&](uint32_t position, hash_digest const& txid) -> confirmed_tx& {
        auto found = entries.find(position);
        if (found == entries.end()) {
            found = entries.emplace(position, confirmed_tx{txid, height, block, position, {}, {}}).first;
        }
        return found->second;
    };
    std::vector<hash_digest> txids;
    txids.reserve(txs.size());
    for (auto const& tx : txs) txids.push_back(tx.hash());

    for (uint32_t position = 0; position < txs.size(); ++position) {
        auto const& outputs = txs[position].outputs();
        for (uint32_t vout = 0; vout < outputs.size(); ++vout) {
            auto const& out = outputs[vout];
            if ( ! target_.owns(out)) continue;
            point const outpoint(txids[position], vout);
            coins_.insert_or_assign(outpoint, coin{outpoint, out.value(), out.token_data(), out.script().bytes(), height, position});
            entry(position, txids[position]).outputs.push_back(received{vout, out.value(), out.token_data(), out.script().bytes()});
        }
    }
    // The coinbase's input spends nothing.
    for (uint32_t position = 1; position < txs.size(); ++position) {
        auto const& inputs = txs[position].inputs();
        for (uint32_t vin = 0; vin < inputs.size(); ++vin) {
            auto const& previous = inputs[vin].previous_output();
            point const outpoint(previous.hash(), previous.index());
            auto const owned = coins_.find(outpoint);
            if (owned == coins_.end()) continue;
            spent_by_.insert_or_assign(outpoint, std::pair{txids[position], height});
            entry(position, txids[position]).inputs.push_back(spent{vin, outpoint, owned->second.value, owned->second.token});
        }
    }
    for (auto& [position, tx] : entries) {
        history_.push_back(std::move(tx));
    }
    if (height + 1 >= start_ && ( ! synced_ || *synced_ < height)) synced_ = height;
    return ! entries.empty();
}

bool target_state::rollback(uint32_t fork) {
    if ( ! synced_ || *synced_ <= fork) return false;
    synced_ = fork < start_ ? std::nullopt : std::optional<uint32_t>(fork);
    auto const before = history_.size();
    std::erase_if(history_, [fork](confirmed_tx const& tx) { return tx.height > fork; });
    std::erase_if(coins_, [fork](auto const& entry) { return entry.second.height > fork; });
    std::erase_if(spent_by_, [fork](auto const& entry) { return entry.second.second > fork; });
    return before != history_.size();
}

bool target_state::is_spent(point const& outpoint) const {
    return spent_by_.contains(outpoint);
}

std::vector<coin> target_state::unspent() const {
    std::vector<coin> out;
    for (auto const& [outpoint, owned] : coins_) {
        if ( ! spent_by_.contains(outpoint)) out.push_back(owned);
    }
    std::ranges::sort(out, [](coin const& a, coin const& b) {
        if (a.height != b.height) return a.height < b.height;
        if (a.position != b.position) return a.position < b.position;
        return a.outpoint.index() < b.outpoint.index();
    });
    return out;
}

// mempool_copy
// ----------------------------------------------------------------------------

mempool_copy::mempool_copy(size_t max_bytes) : max_bytes_(max_bytes) {}

bool mempool_copy::contains(hash_digest const& txid) const {
    return txs_.contains(txid);
}

mempool_tx const* mempool_copy::find(hash_digest const& txid) const {
    auto const found = txs_.find(txid);
    return found == txs_.end() ? nullptr : &found->second;
}

std::optional<hash_digest> mempool_copy::spender(point const& outpoint) const {
    auto const found = spends_.find(outpoint);
    if (found == spends_.end()) return std::nullopt;
    return found->second;
}

bool mempool_copy::has_unconfirmed_parent(transaction const& tx) const {
    return std::ranges::any_of(tx.inputs(), [&](auto const& in) { return txs_.contains(in.previous_output().hash()); });
}

bool mempool_copy::insert(transaction tx, value_lookup const& known) {
    auto const txid = tx.hash();
    if (txs_.contains(txid) || tx.is_coinbase()) return false;
    std::optional<uint64_t> in_value = uint64_t{0};
    for (auto const& in : tx.inputs()) {
        point const outpoint(in.previous_output().hash(), in.previous_output().index());
        std::optional<uint64_t> value;
        if (auto const parent = txs_.find(outpoint.hash()); parent != txs_.end()) {
            auto const& outputs = parent->second.tx.outputs();
            if (outpoint.index() < outputs.size()) value = outputs[outpoint.index()].value();
        } else if (known) {
            value = known(outpoint);
        }
        if ( ! value || ! in_value) {
            in_value.reset();
        } else {
            *in_value += *value;
        }
    }
    uint64_t out_value = 0;
    for (auto const& out : tx.outputs()) out_value += out.value();
    std::optional<uint64_t> fee;
    if (in_value && *in_value >= out_value) fee = *in_value - out_value;

    for (auto const& in : tx.inputs()) {
        spends_.insert_or_assign(point(in.previous_output().hash(), in.previous_output().index()), txid);
    }
    auto const size = tx.serialized_size(true);
    bytes_ += size;
    order_.emplace(sequence_, txid);
    txs_.emplace(txid, mempool_tx{std::move(tx), txid, size, fee, sequence_});
    ++sequence_;
    while (bytes_ > max_bytes_ && ! order_.empty()) {
        auto const oldest = order_.begin()->second;
        remove(oldest);
    }
    return true;
}

void mempool_copy::remove(hash_digest const& txid) {
    auto const found = txs_.find(txid);
    if (found == txs_.end()) return;
    for (auto const& in : found->second.tx.inputs()) {
        point const outpoint(in.previous_output().hash(), in.previous_output().index());
        auto const spend = spends_.find(outpoint);
        if (spend != spends_.end() && spend->second == txid) spends_.erase(spend);
    }
    bytes_ -= found->second.size;
    order_.erase(found->second.sequence);
    txs_.erase(found);
}

bool mempool_copy::erase(hash_digest const& txid) {
    if ( ! txs_.contains(txid)) return false;
    remove(txid);
    return true;
}

std::vector<hash_digest> mempool_copy::remove_for_block(transaction::list const& txs) {
    std::vector<hash_digest> removed;
    std::vector<hash_digest> doomed;
    for (auto const& tx : txs) {
        auto const txid = tx.hash();
        if (txs_.contains(txid)) {
            remove(txid);
            removed.push_back(txid);
        }
        if (tx.is_coinbase()) continue;
        for (auto const& in : tx.inputs()) {
            if (auto const conflict = spender(point(in.previous_output().hash(), in.previous_output().index()))) {
                doomed.push_back(*conflict);
            }
        }
    }
    // A conflict's descendants spend outputs that no longer exist.
    std::set<hash_digest> seen;
    while ( ! doomed.empty()) {
        auto const txid = doomed.back();
        doomed.pop_back();
        if ( ! seen.insert(txid).second) continue;
        auto const found = txs_.find(txid);
        if (found == txs_.end()) continue;
        auto const outputs = found->second.tx.outputs().size();
        for (uint32_t vout = 0; vout < outputs; ++vout) {
            if (auto const child = spender(point(txid, vout))) doomed.push_back(*child);
        }
        remove(txid);
        removed.push_back(txid);
    }
    return removed;
}

void mempool_copy::clear() {
    txs_.clear();
    spends_.clear();
    order_.clear();
    bytes_ = 0;
}

} // namespace kth::node::wallet
