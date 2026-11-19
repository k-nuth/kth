// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_PROOF_HPP
#define KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_PROOF_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/message/compact_filter_tree.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth::domain::message {

// `getcfproof`: one leaf of a block's script filter entry tree, and what the
// leaf points to.
struct KD_API get_compact_filter_proof {
    using ptr = std::shared_ptr<get_compact_filter_proof>;
    using const_ptr = std::shared_ptr<const get_compact_filter_proof>;

    uint8_t filter_type{0};
    hash_digest block_hash{null_hash};
    uint32_t leaf_index{0};

    [[nodiscard]]
    friend bool operator==(get_compact_filter_proof const&, get_compact_filter_proof const&) = default;

    static expect<get_compact_filter_proof> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static std::string const command;
    static uint32_t const version_minimum;
    static uint32_t const version_maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_PROOF_HPP
