// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/spent_outputs.hpp>

#include <map>
#include <optional>
#include <utility>

#include <boost/unordered/unordered_flat_map.hpp>
#include <utxoz/utils.hpp>

#include <kth/blockchain/utxo_builder.hpp>
#include <kth/database/databases/utxo_entry.hpp>

namespace kth::blockchain {

std::expected<domain::chain::output, database::result_code>
output_of_stored_entry(std::span<uint8_t const> value) {
    byte_reader reader(value);
    auto const entry = database::utxo_entry::from_data(reader);
    if ( ! entry) {
        return std::unexpected(database::result_code::other);
    }
    return entry->output();
}

std::expected<spent_output_list, database::result_code>
spent_outputs_with_heights(domain::chain::block const& blk, uint32_t height,
                           database::block_undo const& undo, undo_output_reader const& read) {
    auto const& txs = blk.transactions();
    size_t inputs = 0;
    for (size_t i = 1; i < txs.size(); ++i) {
        inputs += txs[i].inputs().size();
    }

    spent_output_list spent;
    spent.outputs.reserve(inputs);
    spent.heights.reserve(inputs);
    if (inputs == 0) {
        return spent;
    }

    boost::unordered_flat_map<utxoz::raw_outpoint, database::spent_output const*, outpoint_fast_hasher> recorded;
    recorded.reserve(undo.spent.size());
    for (auto const& entry : undo.spent) {
        recorded.emplace(entry.key, &entry);
    }

    // Filled only when an input names an output the record does not hold: most
    // blocks spend little of what they create, and this hashes every
    // transaction of the block.
    std::optional<std::map<hash_digest, size_t>> created;

    for (size_t i = 1; i < txs.size(); ++i) {
        for (auto const& input : txs[i].inputs()) {
            auto const& point = input.previous_output();
            if (auto const it = recorded.find(utxoz::make_outpoint(std::span<uint8_t const, hash_size>{point.hash()}, point.index())); it != recorded.end()) {
                auto output = read(*it->second);
                if ( ! output) {
                    return std::unexpected(output.error());
                }
                spent.outputs.push_back(std::move(*output));
                spent.heights.push_back(it->second->height);
                continue;
            }
            if ( ! created) {
                created.emplace();
                for (size_t t = 0; t < txs.size(); ++t) {
                    created->emplace(txs[t].hash(), t);
                }
            }
            auto const it = created->find(point.hash());
            if (it == created->end() || point.index() >= txs[it->second].outputs().size()) {
                return std::unexpected(database::result_code::key_not_found);
            }
            spent.outputs.push_back(txs[it->second].outputs()[point.index()]);
            spent.heights.push_back(height);
        }
    }
    return spent;
}

} // namespace kth::blockchain
