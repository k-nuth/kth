// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_VERACK_HPP
#define KTH_DOMAIN_MESSAGE_VERACK_HPP

#include <istream>
#include <memory>
#include <string>
#include <string_view>

#include <kth/domain/message/version.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

// The checksum is ignored by the verack command.
struct KD_API verack {
    using ptr = std::shared_ptr<verack>;
    using const_ptr = std::shared_ptr<const verack>;

    static
    size_t satoshi_fixed_size(uint32_t version);

    verack() = default;

    static
    expect<verack> from_data(byte_reader& reader, uint32_t version) noexcept;

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;

    static constexpr std::string_view command = "verack";
    static constexpr uint32_t version_minimum = version::level::minimum;
    static constexpr uint32_t version_maximum = version::level::maximum;

};

} // namespace kth::domain::message

#endif
