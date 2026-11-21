// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTERS_HPP
#define KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTERS_HPP

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
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth::domain::message {

// The most blocks one `getcfilters` may cover, both ends included.
inline constexpr size_t max_compact_filters_per_request = 1000;

// `getcfilters`: the filters of the blocks from a height up to a block, on
// the chain of that block. Each is answered with its own `cfilter`.
struct KD_API get_compact_filters {
    using ptr = std::shared_ptr<get_compact_filters>;
    using const_ptr = std::shared_ptr<const get_compact_filters>;

    uint8_t filter_type{0};
    uint32_t start_height{0};
    hash_digest stop_hash{null_hash};

    [[nodiscard]]
    friend bool operator==(get_compact_filters const&, get_compact_filters const&) = default;

    static expect<get_compact_filters> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static constexpr std::string_view command = "getcfilters";
    static constexpr uint32_t version_minimum = version::level::minimum;
    static constexpr uint32_t version_maximum = version::level::maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTERS_HPP
