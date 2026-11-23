// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_FILTER_CLEAR_HPP
#define KTH_DOMAIN_MESSAGE_FILTER_CLEAR_HPP

#include <memory>
#include <string>
#include <string_view>

#include <kth/domain/message/version.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API filter_clear {
    using ptr = std::shared_ptr<filter_clear>;
    using const_ptr = std::shared_ptr<const filter_clear>;

    [[nodiscard]]
    friend bool operator==(filter_clear const&, filter_clear const&) = default;

    static constexpr
    size_t satoshi_fixed_size(uint32_t /*version*/) {
        // `filter_clear` has an empty payload.
        return 0;
    }

    static
    expect<filter_clear> from_data(byte_reader& reader, uint32_t version) noexcept;

    [[nodiscard]]
    expect<void> to_data(byte_writer& /*writer*/, uint32_t /*version*/) const noexcept {
        // filter_clear has an empty payload.
        return {};
    }

    [[nodiscard]]
    constexpr
    size_t serialized_size(uint32_t version) const {
        return satoshi_fixed_size(version);
    }

    static constexpr std::string_view command = "filterclear";
    static constexpr uint32_t version_minimum = version::level::bip37;
    static constexpr uint32_t version_maximum = version::level::maximum;
};

} // namespace kth::domain::message

#endif
