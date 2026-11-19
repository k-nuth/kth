// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_WALLET_SCAN_HPP
#define KTH_NODE_WALLET_SCAN_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/define.hpp>
#include <kth/node/wallet/filter_store.hpp>

namespace kth::node::wallet {

// Matching what a wallet watches against the stored filters, all of it in one
// pass over each filter, the heights split over threads. Nothing leaves the
// process: the matches only say which whole blocks to download.

/// A script filter key (script, category or transaction) watched from a height on.
struct scan_key {
    hash_digest key;
    uint32_t from;
};

/// The first `bits` bits (0 to 16) of an input prefix, right-aligned in
/// `prefix`, watched from a height on.
struct scan_prefix {
    uint16_t prefix;
    uint8_t bits;
    uint32_t from;
};

/// The heights that match, in increasing order, each with the positions of
/// the keys or prefixes it matches, in increasing order.
using scan_hits = std::vector<std::pair<uint32_t, std::vector<size_t>>>;

/// Tests `keys` against the script filters (filter type 1: the entry tree's
/// root, then the Golomb set) at `locations` of `data`. A filter that does not
/// parse matches every key, so it can never hide a payment. file_system when
/// the file cannot be read.
[[nodiscard]]
KND_API
std::expected<scan_hits, std::error_code> scan_script_filters(std::filesystem::path const& data,
    std::span<filter_location const> locations, std::span<scan_key const> keys, size_t threads);

/// Tests `prefixes` against the input prefix filters (filter type 2) at
/// `locations` of `data`. A filter that does not parse matches every prefix.
[[nodiscard]]
KND_API
std::expected<scan_hits, std::error_code> scan_input_prefix_filters(std::filesystem::path const& data,
    std::span<filter_location const> locations, std::span<scan_prefix const> prefixes, size_t threads);

/// The keys of `keys` a script filter holds, by position; all of them when
/// the filter does not parse.
[[nodiscard]]
KND_API
std::vector<size_t> match_script_filter(hash_digest const& block, byte_span filter,
    std::span<hash_digest const> keys);

} // namespace kth::node::wallet

#endif // KTH_NODE_WALLET_SCAN_HPP
