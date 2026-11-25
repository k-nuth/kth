// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/bloom_filters.hpp>

#include <utility>

#include <kth/domain/chain/partial_merkle_tree.hpp>
#include <kth/domain/message/filter_add.hpp>
#include <kth/domain/message/filter_clear.hpp>
#include <kth/domain/message/filter_load.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth::node::detail {

filter_change apply_filter_message(network::tx_inventory& inventory, std::string_view command,
    byte_span payload, uint32_t version) {
    byte_reader reader(payload);
    if (command == domain::message::filter_load::command) {
        auto const parsed = domain::message::filter_load::from_data(reader, version);
        if ( ! parsed) {
            return parsed.error() == error::invalid_filter_load ? filter_change::refused : filter_change::none;
        }
        domain::chain::bloom_filter filter(parsed->filter(), parsed->hash_functions(), parsed->tweak(),
            parsed->flags());
        if ( ! filter.within_limits()) return filter_change::refused;
        inventory.load_filter(std::move(filter));
        return filter_change::loaded;
    }
    if (command == domain::message::filter_add::command) {
        auto const parsed = domain::message::filter_add::from_data(reader, version);
        if ( ! parsed) {
            return parsed.error() == error::invalid_filter_add ? filter_change::refused : filter_change::none;
        }
        if (parsed->data().size() > domain::chain::bloom_filter::max_element_size) {
            return filter_change::refused;
        }
        // Into the filter of no bytes this adds nothing: it holds everything.
        inventory.filter().insert(parsed->data());
        return filter_change::added;
    }
    if (command == domain::message::filter_clear::command) {
        inventory.clear_filter();
        return filter_change::cleared;
    }
    return filter_change::none;
}

filtered_block filter_block(domain::chain::block const& block, domain::chain::bloom_filter& filter) {
    auto const& transactions = block.transactions();
    hash_list txids;
    txids.reserve(transactions.size());
    std::vector<bool> matches(transactions.size(), false);
    for (size_t i = 0; i < transactions.size(); ++i) {
        txids.push_back(transactions[i].hash());
        matches[i] = filter.match_and_insert_outputs(transactions[i]);
    }
    filtered_block answer;
    for (size_t i = 0; i < transactions.size(); ++i) {
        if ( ! matches[i]) matches[i] = filter.match_inputs(transactions[i]);
        if (matches[i]) answer.matched.push_back(i);
    }
    auto tree = domain::chain::partial_merkle_tree::build(txids, matches);
    answer.proof = domain::message::merkle_block(block.header(), tree.total_transactions,
        std::move(tree.hashes), std::move(tree.flags));
    return answer;
}

} // namespace kth::node::detail
