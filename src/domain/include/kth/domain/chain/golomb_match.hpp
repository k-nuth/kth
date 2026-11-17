// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_GOLOMB_MATCH_HPP
#define KTH_DOMAIN_CHAIN_GOLOMB_MATCH_HPP

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

#include <kth/domain/chain/golomb_set.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

/// The positions in `items` of the items a set holds, in increasing order,
/// read from the set as it is served (the CompactSize count, then the bit
/// stream) in one pass for all of them. A wallet that watches many scripts
/// tests each block's filter once, not once per script.
/// A count above a uint32 is too_many_items; a stream that ends before a
/// value it needs is truncated.
[[nodiscard]]
KD_API
std::expected<std::vector<size_t>, golomb_set_error>
golomb_match_each(golomb_key const& key, byte_span serialized, std::span<byte_span const> items);

} // namespace kth::domain::chain

#endif
