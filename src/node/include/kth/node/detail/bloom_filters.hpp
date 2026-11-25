// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_BLOOM_FILTERS_HPP
#define KTH_NODE_DETAIL_BLOOM_FILTERS_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/bloom_filter.hpp>
#include <kth/domain/message/merkle_block.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/network/tx_inventory.hpp>
#include <kth/node/define.hpp>

namespace kth::node::detail {

// Serving a wallet that loads a bloom filter (BIP37). The filter is the
// peer's and lives in its session for as long as the connection: the node
// keeps nothing for it.

/// What a filter message did to a peer's filter.
enum class filter_change {
    /// Not a filter message, or one that does not parse: nothing changed.
    none,
    loaded,
    added,
    cleared,
    /// It broke a limit, which a wallet never does: a filter over 36,000
    /// bytes or 50 hash functions, or an element over 520 bytes. The peer is
    /// scored for it (BCHN).
    refused
};

/// Applies `filterload`, `filteradd` or `filterclear` to the peer's inventory.
[[nodiscard]] KND_API
filter_change apply_filter_message(network::tx_inventory& inventory, std::string_view command,
    byte_span payload, uint32_t version);

/// The answer to a request for a filtered block.
struct filtered_block {
    domain::message::merkle_block proof;
    /// The places in the block of the transactions the filter matched, which
    /// are sent after the proof.
    std::vector<size_t> matched;
};

/// The outputs of every transaction are matched first and their outpoints
/// added, then the inputs: in a block a transaction can stand before the one
/// it spends, and the wallet is to hear of both (BCHN).
[[nodiscard]] KND_API
filtered_block filter_block(domain::chain::block const& block, domain::chain::bloom_filter& filter);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_BLOOM_FILTERS_HPP
