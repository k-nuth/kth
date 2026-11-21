// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_COMPACT_FILTER_HEADERS_HPP
#define KTH_DOMAIN_MESSAGE_COMPACT_FILTER_HEADERS_HPP

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
#include <vector>

namespace kth::domain::message {

// The most blocks one `getcfheaders` may cover, and so the most filter hashes
// one `cfheaders` carries.
inline constexpr size_t max_compact_filter_headers = 2000;

// `cfheaders`: the filter header before the first block asked for, and the
// filter hash of each block. The receiver chains them: a filter header is the
// double SHA-256 of the filter hash and the filter header before it.
struct KD_API compact_filter_headers {
    using ptr = std::shared_ptr<compact_filter_headers>;
    using const_ptr = std::shared_ptr<const compact_filter_headers>;

    uint8_t filter_type{0};
    hash_digest stop_hash{null_hash};
    hash_digest previous_filter_header{null_hash};
    std::vector<hash_digest> filter_hashes;

    [[nodiscard]]
    friend bool operator==(compact_filter_headers const&, compact_filter_headers const&) = default;

    static expect<compact_filter_headers> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static constexpr std::string_view command = "cfheaders";
    static constexpr uint32_t version_minimum = version::level::minimum;
    static constexpr uint32_t version_maximum = version::level::maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_COMPACT_FILTER_HEADERS_HPP
