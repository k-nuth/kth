// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/compact_filter.hpp>

#include <algorithm>
#include <span>
#include <vector>

namespace kth::domain::chain {

namespace {

// The outpoint as it stands in a transaction: the hash, then the index in
// little-endian.
data_chunk outpoint_item(output_point const& point) {
    data_chunk item;
    item.reserve(hash_size + sizeof(uint32_t));
    item.insert(item.end(), point.hash().begin(), point.hash().end());
    auto const index = point.index();
    item.push_back(uint8_t(index & 0xff));
    item.push_back(uint8_t((index >> 8) & 0xff));
    item.push_back(uint8_t((index >> 16) & 0xff));
    item.push_back(uint8_t((index >> 24) & 0xff));
    return item;
}

} // namespace

golomb_key compact_filter_key(hash_digest const& block_hash) {
    golomb_key key{};
    std::copy_n(block_hash.begin(), key.size(), key.begin());
    return key;
}

std::expected<golomb_set, golomb_set_error>
build_regular_filter(block const& blk) {
    auto const& txs = blk.transactions();

    std::vector<data_chunk> items;
    auto const add_output_scripts = [&items](transaction const& tx) {
        for (auto const& output : tx.outputs()) {
            auto const& script = output.script().bytes();
            if (script.empty()) {
                continue;
            }
            items.emplace_back(script.begin(), script.end());
        }
    };

    if ( ! txs.empty()) {
        // The coinbase on its own: its input names no output.
        add_output_scripts(txs.front());
        for (auto const& tx : std::span{txs}.subspan(1)) {
            for (auto const& input : tx.inputs()) {
                items.push_back(outpoint_item(input.previous_output()));
            }
            add_output_scripts(tx);
        }
    }

    // Equal items are one item of the set.
    return golomb_set::build(compact_filter_key(blk.hash()), items);
}

hash_digest compact_filter_hash(golomb_set const& filter) {
    return bitcoin_hash(filter.to_data());
}

hash_digest compact_filter_header(golomb_set const& filter, hash_digest const& previous_header) {
    return bitcoin_hash(splice(compact_filter_hash(filter), previous_header));
}

} // namespace kth::domain::chain
