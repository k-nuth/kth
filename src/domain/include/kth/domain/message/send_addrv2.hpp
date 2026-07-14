// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_SEND_ADDRV2_HPP
#define KTH_DOMAIN_MESSAGE_SEND_ADDRV2_HPP

#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

/// BIP155: sendaddrv2 message.
/// This is an empty message that signals support for addrv2 (BIP155).
/// Must be sent after VERSION but before VERACK to signal addrv2 support.
struct KD_API send_addrv2 {
    using ptr = std::shared_ptr<send_addrv2>;
    using const_ptr = std::shared_ptr<const send_addrv2>;

    [[nodiscard]]
    friend bool operator==(send_addrv2 const&, send_addrv2 const&) = default;

    static constexpr
    size_t satoshi_fixed_size(uint32_t /*version*/) {
        // `send_addrv2` has an empty payload.
        return 0;
    }

    static
    expect<send_addrv2> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    [[nodiscard]]
    constexpr
    size_t serialized_size(uint32_t version) const {
        return satoshi_fixed_size(version);
    }

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;
};

} // namespace kth::domain::message

#endif
