// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/script_filter.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/endian.hpp>

namespace kth::domain::chain {

namespace {

constexpr size_t tx_index_offset = hash_size;
constexpr size_t index_offset = tx_index_offset + sizeof(uint32_t);
constexpr size_t kind_offset = index_offset + sizeof(uint32_t);

// A prefix longer than a key, or with stray bits past its length, would have
// more than one encoding for the same matched keys.
std::expected<void, script_range_error> check_script_prefix(hash_digest const& prefix, uint16_t bits) {
    if (bits > 256) {
        return std::unexpected(script_range_error::prefix);
    }

    auto full = static_cast<size_t>(bits / 8);
    auto const rem = static_cast<unsigned>(bits % 8);
    if (rem != 0) {
        // The prefix occupies the high `rem` bits of this byte. The low bits must be clear.
        auto const mask = static_cast<uint8_t>(0xffu >> rem);
        if ((prefix[full] & mask) != 0) {
            return std::unexpected(script_range_error::prefix);
        }
        ++full;
    }
    for (size_t i = full; i < prefix.size(); ++i) {
        if (prefix[i] != 0) {
            return std::unexpected(script_range_error::prefix);
        }
    }
    return {};
}

// Compares the first `bits` of a key with a prefix. The caller has already
// rejected a length above 256, so a partial byte still lies inside the key.
// Only the sign of the result is significant.
int compare_script_prefix(hash_digest const& key, hash_digest const& prefix, uint16_t bits) {
    auto const full = static_cast<size_t>(bits / 8);
    for (size_t i = 0; i < full; ++i) {
        if (key[i] < prefix[i]) {
            return -1;
        }
        if (key[i] > prefix[i]) {
            return 1;
        }
    }

    auto const rem = static_cast<unsigned>(bits % 8);
    if (rem != 0) {
        // Keep the high `rem` bits. The prefix's unused low bits are zero.
        auto const mask = static_cast<uint8_t>(0xffu << (8u - rem));
        auto const key_bits = static_cast<uint8_t>(key[full] & mask);
        if (key_bits < prefix[full]) {
            return -1;
        }
        if (key_bits > prefix[full]) {
            return 1;
        }
    }
    return 0;
}

// First index at which `pred` is true. `pred` is false for a prefix of the
// leaves and true for the rest, which is the case when the leaves are sorted
// and `pred` is a prefix lower or upper bound.
template <typename Predicate>
size_t prefix_bound(std::span<script_leaf const> leaves, Predicate pred) {
    size_t lower = 0;
    size_t upper = leaves.size();
    while (lower < upper) {
        // The sum is widened so a large leaf count cannot wrap the midpoint.
        auto const mid = static_cast<size_t>((static_cast<uint64_t>(lower) + static_cast<uint64_t>(upper)) / 2);
        if ( ! pred(leaves[mid])) {
            lower = mid + 1;
        } else {
            upper = mid;
        }
    }
    return lower;
}

// Parents of one level. Both children are read before either is replaced, and
// a final node with no partner is copied unchanged rather than hashed with itself.
std::vector<hash_digest> script_next_level(std::span<hash_digest const> level) {
    std::vector<hash_digest> next;
    next.reserve((level.size() + 1) / 2);
    for (size_t i = 0; i < level.size(); i += 2) {
        if (i + 1 == level.size()) {
            next.push_back(level[i]);
            continue;
        }
        next.push_back(script_node_hash(level[i], level[i + 1]));
    }
    return next;
}

} // namespace

// Serialization.
//-----------------------------------------------------------------------------

std::array<uint8_t, script_leaf_size> to_data(script_leaf const& leaf) {
    std::array<uint8_t, script_leaf_size> out{};
    std::copy(leaf.key.begin(), leaf.key.end(), out.begin());

    auto const tx_index = to_little_endian(leaf.tx_index);
    auto const index = to_little_endian(leaf.index);
    std::copy(tx_index.begin(), tx_index.end(), out.begin() + tx_index_offset);
    std::copy(index.begin(), index.end(), out.begin() + index_offset);
    out[kind_offset] = static_cast<uint8_t>(leaf.kind);
    return out;
}

std::expected<script_leaf, script_range_error> parse_script_leaf(byte_span data) {
    if (data.size() != script_leaf_size || data[kind_offset] > static_cast<uint8_t>(script_leaf_kind::spent_category)) {
        return std::unexpected(script_range_error::leaf);
    }

    script_leaf leaf{};
    std::copy_n(data.data(), hash_size, leaf.key.begin());

    std::array<uint8_t, sizeof(uint32_t)> tx_index{};
    std::array<uint8_t, sizeof(uint32_t)> index{};
    std::copy_n(data.data() + tx_index_offset, tx_index.size(), tx_index.begin());
    std::copy_n(data.data() + index_offset, index.size(), index.begin());
    leaf.tx_index = from_little_endian<uint32_t>(std::span<uint8_t const, sizeof(uint32_t)>{tx_index});
    leaf.index = from_little_endian<uint32_t>(std::span<uint8_t const, sizeof(uint32_t)>{index});
    leaf.kind = static_cast<script_leaf_kind>(data[kind_offset]);
    return leaf;
}

// Hashes.
//-----------------------------------------------------------------------------

hash_digest script_leaf_hash(script_leaf const& leaf) {
    auto const serialized = to_data(leaf);
    std::array<uint8_t, 1 + script_leaf_size> buffer{};
    buffer[0] = 0x00;
    std::copy(serialized.begin(), serialized.end(), buffer.begin() + 1);
    return sha256_hash(buffer);
}

hash_digest script_node_hash(hash_digest const& left, hash_digest const& right) {
    std::array<uint8_t, 1 + (2 * hash_size)> buffer{};
    buffer[0] = 0x01;
    std::copy(left.begin(), left.end(), buffer.begin() + 1);
    std::copy(right.begin(), right.end(), buffer.begin() + 1 + hash_size);
    return sha256_hash(buffer);
}

hash_digest script_leaves_root(std::span<script_leaf const> leaves) {
    if (leaves.empty()) {
        return hash_digest{};
    }

    std::vector<hash_digest> level;
    level.reserve(leaves.size());
    for (auto const& leaf : leaves) {
        level.push_back(script_leaf_hash(leaf));
    }
    while (level.size() > 1) {
        level = script_next_level(level);
    }
    return level.front();
}

// Range proofs.
//-----------------------------------------------------------------------------

std::expected<script_leaf_run, script_range_error>
script_leaves_with_prefix(std::span<script_leaf const> leaves, hash_digest const& prefix, uint16_t bits) {
    if (auto const checked = check_script_prefix(prefix, bits); ! checked) {
        return std::unexpected(checked.error());
    }

    auto const first = prefix_bound(leaves, [&](script_leaf const& leaf) {
        return compare_script_prefix(leaf.key, prefix, bits) >= 0;
    });
    auto const end = prefix_bound(leaves, [&](script_leaf const& leaf) {
        return compare_script_prefix(leaf.key, prefix, bits) > 0;
    });
    return script_leaf_run{first, end};
}

std::expected<std::vector<hash_digest>, script_range_error>
script_range_hashes(std::span<script_leaf const> leaves, size_t lo, size_t end) {
    if (lo >= end || end > leaves.size()) {
        return std::unexpected(script_range_error::range);
    }

    std::vector<hash_digest> level;
    level.reserve(leaves.size());
    for (auto const& leaf : leaves) {
        level.push_back(script_leaf_hash(leaf));
    }

    std::vector<hash_digest> hashes;
    auto a = lo;
    auto b = end - 1;
    while (level.size() > 1) {
        if (a % 2 == 1) {
            hashes.push_back(level[a - 1]);
        }
        if (b % 2 == 0 && b + 1 < level.size()) {
            hashes.push_back(level[b + 1]);
        }
        level = script_next_level(level);
        a /= 2;
        b /= 2;
    }
    return hashes;
}

std::expected<hash_digest, script_range_error>
script_range_root(uint32_t count, uint32_t lo, std::span<script_leaf const> run, std::span<hash_digest const> hashes) {
    // Widened before the addition so a large index and a large run cannot wrap.
    if (run.empty() || static_cast<uint64_t>(lo) + static_cast<uint64_t>(run.size()) > static_cast<uint64_t>(count)) {
        return std::unexpected(script_range_error::range);
    }

    std::vector<hash_digest> nodes;
    nodes.reserve(run.size());
    for (auto const& leaf : run) {
        nodes.push_back(script_leaf_hash(leaf));
    }

    auto remaining = hashes;
    auto a = static_cast<uint64_t>(lo);
    auto b = static_cast<uint64_t>(lo) + static_cast<uint64_t>(run.size()) - 1;
    auto width = static_cast<uint64_t>(count);
    while (width > 1) {
        if (a % 2 == 1) {
            if (remaining.empty()) {
                return std::unexpected(script_range_error::range);
            }
            auto const sibling = remaining.front();
            remaining = remaining.subspan(1);
            nodes.insert(nodes.begin(), sibling);
            --a;
        }
        if (b % 2 == 0 && b + 1 < width) {
            if (remaining.empty()) {
                return std::unexpected(script_range_error::range);
            }
            auto const sibling = remaining.front();
            remaining = remaining.subspan(1);
            nodes.push_back(sibling);
            ++b;
        }
        nodes = script_next_level(nodes);
        a /= 2;
        b /= 2;
        // The full level rounds up: an unpaired last node still takes a place above.
        width = (width + 1) / 2;
    }
    if ( ! remaining.empty()) {
        return std::unexpected(script_range_error::range);
    }
    return nodes.front();
}

std::expected<void, script_range_error>
check_script_range(hash_digest const& root, uint32_t count, uint32_t first,
                   std::span<script_leaf const> run,
                   script_leaf const* before, script_leaf const* after,
                   std::span<hash_digest const> hashes,
                   hash_digest const& prefix, uint16_t bits) {
    if (auto const checked = check_script_prefix(prefix, bits); ! checked) {
        return std::unexpected(checked.error());
    }

    // Widened so the end index cannot wrap when the run is far past `first`.
    auto const end = static_cast<uint64_t>(first) + static_cast<uint64_t>(run.size());
    if (end > count || (first > 0) != (before != nullptr) || (end < count) != (after != nullptr)) {
        return std::unexpected(script_range_error::range);
    }
    if (count == 0) {
        if ( ! hashes.empty()) {
            return std::unexpected(script_range_error::range);
        }
        if (root != hash_digest{}) {
            return std::unexpected(script_range_error::root);
        }
        return {};
    }

    // The neighbours are part of the proved span, so the edge hashes cover them too.
    std::vector<script_leaf> covered;
    covered.reserve(run.size() + 2);
    auto lo = first;
    if (before != nullptr) {
        covered.push_back(*before);
        --lo;
    }
    covered.insert(covered.end(), run.begin(), run.end());
    if (after != nullptr) {
        covered.push_back(*after);
    }

    auto const rebuilt = script_range_root(count, lo, covered, hashes);
    if ( ! rebuilt) {
        return std::unexpected(rebuilt.error());
    }
    if (*rebuilt != root) {
        return std::unexpected(script_range_error::root);
    }

    for (auto const& leaf : run) {
        if (compare_script_prefix(leaf.key, prefix, bits) != 0) {
            return std::unexpected(script_range_error::leaves);
        }
    }
    if (before != nullptr && compare_script_prefix(before->key, prefix, bits) >= 0) {
        return std::unexpected(script_range_error::leaves);
    }
    if (after != nullptr && compare_script_prefix(after->key, prefix, bits) <= 0) {
        return std::unexpected(script_range_error::leaves);
    }
    for (size_t i = 1; i < covered.size(); ++i) {
        if (covered[i - 1] >= covered[i]) {
            return std::unexpected(script_range_error::leaves);
        }
    }
    return {};
}

} // namespace kth::domain::chain
