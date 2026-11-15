// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_COMPACT_FILTER_HPP
#define KTH_DOMAIN_MESSAGE_COMPACT_FILTER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::message {

// The filter type of every compact filter message. Zero is the regular
// filter: the outpoint of every input after the coinbase, and every output
// script that is not empty.
inline constexpr uint8_t regular_compact_filter = 0;

// The largest filter a `cfilter` may carry.
inline constexpr size_t max_compact_filter_size = 4 * 1024 * 1024;

// `cfilter`: the filter of one block, as its Golomb-coded set goes on the
// wire (the item count, then the bit stream).
struct KD_API compact_filter {
    using ptr = std::shared_ptr<compact_filter>;
    using const_ptr = std::shared_ptr<const compact_filter>;

    uint8_t filter_type{regular_compact_filter};
    hash_digest block_hash{null_hash};
    data_chunk filter;

    [[nodiscard]]
    friend bool operator==(compact_filter const&, compact_filter const&) = default;

    static expect<compact_filter> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static std::string const command;
    static uint32_t const version_minimum;
    static uint32_t const version_maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_COMPACT_FILTER_HPP
