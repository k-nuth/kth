// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_RANGE_HPP
#define KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_RANGE_HPP

#if ! defined(KTH_WITH_NEUTRINO)
#error "Compact filters are an optional feature: build with the Conan option neutrino=True."
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/message/compact_filter_tree.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth::domain::message {

// `getcfrange`: the leaves of a block's script filter entry tree whose keys
// start with a prefix. No bits ask for every leaf, and 256 for one exact key.
// The bits of the prefix past its length are zero.
struct KD_API get_compact_filter_range {
    using ptr = std::shared_ptr<get_compact_filter_range>;
    using const_ptr = std::shared_ptr<const get_compact_filter_range>;

    uint8_t filter_type{0};
    hash_digest block_hash{null_hash};
    uint16_t prefix_bits{0};
    hash_digest prefix{null_hash};

    [[nodiscard]]
    friend bool operator==(get_compact_filter_range const&, get_compact_filter_range const&) = default;

    static expect<get_compact_filter_range> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static constexpr std::string_view command = "getcfrange";
    static constexpr uint32_t version_minimum = version::level::minimum;
    static constexpr uint32_t version_maximum = version::level::maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_RANGE_HPP
