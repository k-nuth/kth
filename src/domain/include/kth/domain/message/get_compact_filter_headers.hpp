// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_HEADERS_HPP
#define KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_HEADERS_HPP

#if ! defined(KTH_WITH_NEUTRINO)
#error "Compact filters are an optional feature: build with the Conan option neutrino=True."
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth::domain::message {

// `getcfheaders`: the filter hashes of the blocks from a height up to a
// block, at most max_compact_filter_headers of them.
struct KD_API get_compact_filter_headers {
    using ptr = std::shared_ptr<get_compact_filter_headers>;
    using const_ptr = std::shared_ptr<const get_compact_filter_headers>;

    uint8_t filter_type{0};
    uint32_t start_height{0};
    hash_digest stop_hash{null_hash};

    [[nodiscard]]
    friend bool operator==(get_compact_filter_headers const&, get_compact_filter_headers const&) = default;

    static expect<get_compact_filter_headers> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static std::string const command;
    static uint32_t const version_minimum;
    static uint32_t const version_maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_HEADERS_HPP
