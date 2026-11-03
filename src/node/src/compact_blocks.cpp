// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/compact_blocks.hpp>

#include <unordered_map>
#include <utility>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/constants/functions.hpp>

namespace kth::node {

namespace {

using short_id_indexes = std::unordered_map<uint64_t, size_t>;

bool commits(domain::chain::header const& header, domain::chain::transaction::list const& transactions) {
    domain::chain::block const block{header, transactions};
    return block.is_valid_merkle_root();
}

template <typename Fill>
compact_reconstruction reconstruct(domain::message::compact_block const& message, Fill fill) {
    auto const& short_ids = message.short_ids();
    auto const& prefilled = message.transactions();
    if (prefilled.size() > max_compact_transactions()
        || short_ids.size() > max_compact_transactions() - prefilled.size()) {
        return {};
    }
    auto const total = short_ids.size() + prefilled.size();
    if (total == 0) {
        return {};
    }

    domain::chain::transaction::list slots(total);
    uint64_t previous = 0;
    bool started = false;
    for (auto const& item : prefilled) {
        if (item.index() >= total || (started && item.index() <= previous)
            || item.transaction().is_null()) {
            return {};
        }
        slots[static_cast<size_t>(item.index())] = item.transaction();
        previous = item.index();
        started = true;
    }

    short_id_indexes indexes;
    indexes.reserve(short_ids.size());
    size_t hole = 0;
    for (auto const id : short_ids) {
        while ( ! slots[hole].is_null()) {
            ++hole;
        }
        // Repeated short ids cannot distinguish the two positions, even when
        // the mempool has only one match. Ask for the full block.
        // Bound bucket walks too: a peer chooses the IDs and can crowd a bucket.
        if ( ! indexes.emplace(id, hole++).second || indexes.bucket_size(indexes.bucket(id)) > 12) {
            return {compact_reconstruct::collision, {}, {}};
        }
    }
    if (fill(indexes, slots)) {
        return {compact_reconstruct::collision, {}, {}};
    }

    std::vector<uint64_t> missing;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].is_null()) {
            missing.push_back(i);
        }
    }
    if ( ! missing.empty()) {
        return {compact_reconstruct::missing, std::move(slots), std::move(missing)};
    }
    if ( ! commits(message.header(), slots)) {
        return {};
    }
    return {compact_reconstruct::complete, std::move(slots), {}};
}

} // namespace

compact_reconstruction reconstruct_compact_block(
    domain::message::compact_block const& message,
    std::vector<domain::chain::transaction> const& available) {
    auto const key = domain::message::hash(message);
    return reconstruct(message, [&](short_id_indexes const& indexes, domain::chain::transaction::list& slots) {
        std::vector<bool> filled(slots.size());
        for (auto const& tx : available) {
            auto const found = indexes.find(domain::message::compact_short_id(key, tx.hash()));
            if (found == indexes.end()) {
                continue;
            }
            if (filled[found->second]) {
                return true;
            }
            filled[found->second] = true;
            slots[found->second] = tx;
        }
        return false;
    });
}

compact_reconstruction reconstruct_compact_block(
    domain::message::compact_block const& message, blockchain::block_chain const& chain) {
    return reconstruct(message, [&](short_id_indexes const& indexes, domain::chain::transaction::list& slots) {
        size_t mempool_count = 0;
        return chain.fill_tx_list_from_mempool(message, mempool_count, slots, indexes);
    });
}

compact_reconstruction accept_block_transactions(
    domain::message::compact_block const& message,
    compact_reconstruction const& partial,
    domain::chain::transaction::list const& provided) {
    if (partial.status != compact_reconstruct::missing
        || provided.size() != partial.missing.size()
        || partial.transactions.empty()
        || partial.transactions.size() != message.transactions().size() + message.short_ids().size()) {
        return {};
    }

    auto slots = partial.transactions;
    auto const key = domain::message::hash(message);
    size_t short_index = 0;
    size_t provided_index = 0;
    size_t prefilled_index = 0;
    for (size_t index = 0; index < slots.size(); ++index) {
        if (prefilled_index < message.transactions().size()
            && message.transactions()[prefilled_index].index() == index) {
            ++prefilled_index;
            continue;
        }
        if (short_index >= message.short_ids().size()) {
            return {};
        }
        if (provided_index < partial.missing.size() && partial.missing[provided_index] == index) {
            auto const& tx = provided[provided_index++];
            if (domain::message::compact_short_id(key, tx.hash()) != message.short_ids()[short_index]) {
                return {};
            }
            slots[index] = tx;
        }
        ++short_index;
    }
    if (provided_index != provided.size() || ! commits(message.header(), slots)) {
        return {};
    }
    return {compact_reconstruct::complete, std::move(slots), {}};
}

} // namespace kth::node
