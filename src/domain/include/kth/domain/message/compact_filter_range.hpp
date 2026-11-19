// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_COMPACT_FILTER_RANGE_HPP
#define KTH_DOMAIN_MESSAGE_COMPACT_FILTER_RANGE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <kth/domain/chain/script_filter.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/message/compact_filter_tree.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth::domain::message {

// `cfrange`, the answer to a `getcfrange`. With the hashes on its edges, the
// run of leaves from `before` to `after` rebuilds the root of the tree, which
// shows that a tree whose leaves are in order holds no other leaf with the
// prefix asked for (chain::check_script_range).
struct KD_API compact_filter_range {
    using ptr = std::shared_ptr<compact_filter_range>;
    using const_ptr = std::shared_ptr<const compact_filter_range>;

    uint8_t filter_type{0};
    hash_digest block_hash{null_hash};

    // The number of leaves in the tree.
    uint32_t leaf_count{0};

    // The index of the first leaf with the prefix or, when no leaf has it, of
    // the place where one would stand.
    uint32_t first_index{0};

    // The leaves with the prefix.
    std::vector<chain::script_leaf> leaves;

    // The leaf just before them: there exactly when `first_index` is above 0.
    std::optional<chain::script_leaf> before;

    // The leaf just after them: there exactly when they end before the last
    // leaf of the tree.
    std::optional<chain::script_leaf> after;

    // The hashes on the edges of the run from `before` to `after`, level by
    // level from the leaves up, the left one before the right one.
    std::vector<hash_digest> hashes;

    [[nodiscard]]
    friend bool operator==(compact_filter_range const&, compact_filter_range const&) = default;

    static expect<compact_filter_range> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static std::string const command;
    static uint32_t const version_minimum;
    static uint32_t const version_maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_COMPACT_FILTER_RANGE_HPP
