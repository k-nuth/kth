// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_ANNOUNCE_XVERSION_HPP_
#define KTH_DOMAIN_MESSAGE_ANNOUNCE_XVERSION_HPP_

#include <cstdint>
#include <istream>
#include <memory>
#include <string>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/message/network_address.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

using namespace kth::infrastructure::message;

// Implementation of BU xversion and xverack messages
// https://github.com/BitcoinUnlimited/BitcoinUnlimited/blob/dev/doc/xversionmessage.md

struct KD_API xversion {
    using ptr = std::shared_ptr<xversion>;
    using const_ptr = std::shared_ptr<xversion const>;

    xversion() = default;

    [[nodiscard]]
    friend bool operator==(xversion const&, xversion const&) = default;

    static
    expect<xversion> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    bool is_valid() const;

    void reset();

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

private:
};

} // namespace kth::domain::message

#endif
