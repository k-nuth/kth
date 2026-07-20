// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_XVERACK_HPP_
#define KTH_DOMAIN_MESSAGE_XVERACK_HPP_

#include <istream>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

// Implementation of BU xversion and xverack messages
// https://github.com/BitcoinUnlimited/BitcoinUnlimited/blob/dev/doc/xversionmessage.md


// The checksum is ignored by the xverack command.
struct KD_API xverack {
    using ptr = std::shared_ptr<xverack>;
    using const_ptr = std::shared_ptr<const xverack>;

    static
    size_t satoshi_fixed_size(uint32_t version);

    xverack() = default;

    static
    expect<xverack> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;
    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

};

} // namespace kth::domain::message

#endif
