// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_COMPACT_FILTER_HPP
#define KTH_DOMAIN_CHAIN_COMPACT_FILTER_HPP

#include <cstdint>
#include <expected>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/golomb_set.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

// The regular compact filter of a block: filter type 0 of the compact filter
// messages, as Bitcoin Cash nodes serve it. A light wallet downloads it in
// place of the block and tests it for what it watches, so the node learns
// nothing about the wallet.
//
// Its items are, for every transaction after the coinbase, the outpoint of
// each input in wire form (the hash, then the index in little-endian), and
// for every transaction, each output script that is not empty. A script that
// starts with OP_RETURN is an item like any other.
//
// Everything in it comes from the block itself, so a node builds it for any
// block it holds and keeps nothing for it.
inline constexpr uint8_t regular_filter_type = 0;

// The key of a block's filters: the leading 16 bytes of the block hash in
// stored order.
[[nodiscard]]
KD_API
golomb_key compact_filter_key(hash_digest const& block_hash);

[[nodiscard]]
KD_API
std::expected<golomb_set, golomb_set_error>
build_regular_filter(block const& blk);

// The filter hash: the double SHA-256 of the filter as it is served, the item
// count and then the bit stream.
[[nodiscard]]
KD_API
hash_digest compact_filter_hash(golomb_set const& filter);

// The filter header: the double SHA-256 of the filter hash followed by the
// filter header of the previous block. The header before the genesis block is
// all zeros.
[[nodiscard]]
KD_API
hash_digest compact_filter_header(golomb_set const& filter, hash_digest const& previous_header);

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_COMPACT_FILTER_HPP
