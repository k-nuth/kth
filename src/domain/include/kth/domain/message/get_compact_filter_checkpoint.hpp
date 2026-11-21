// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_CHECKPOINT_HPP
#define KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_CHECKPOINT_HPP

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

// `getcfcheckpt`: the filter headers at the checkpoint interval on the chain
// of a block.
struct KD_API get_compact_filter_checkpoint {
    using ptr = std::shared_ptr<get_compact_filter_checkpoint>;
    using const_ptr = std::shared_ptr<const get_compact_filter_checkpoint>;

    uint8_t filter_type{0};
    hash_digest stop_hash{null_hash};

    [[nodiscard]]
    friend bool operator==(get_compact_filter_checkpoint const&, get_compact_filter_checkpoint const&) = default;

    static expect<get_compact_filter_checkpoint> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static constexpr std::string_view command = "getcfcheckpt";
    static constexpr uint32_t version_minimum = version::level::minimum;
    static constexpr uint32_t version_maximum = version::level::maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_GET_COMPACT_FILTER_CHECKPOINT_HPP
