// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_ADDRESS_HPP
#define KTH_DOMAIN_MESSAGE_ADDRESS_HPP

#include <memory>
#include <string>


#include <kth/domain/concepts.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/message/network_address.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
namespace kth::domain::message {

struct KD_API address {
    using ptr = std::shared_ptr<address>;
    using const_ptr = std::shared_ptr<const address>;

    address() = default;

    /// Fails with error::invalid_address_count over max_address entries.
    static
    expect<address> create(infrastructure::message::network_address::list addresses);

    [[nodiscard]]
    friend bool operator==(address const&, address const&) = default;

    [[nodiscard]]
    infrastructure::message::network_address::list const& addresses() const;


    static
    expect<address> from_data(byte_reader& reader, uint32_t version);

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

private:
    address(infrastructure::message::network_address::list addresses);

    infrastructure::message::network_address::list addresses_;
};

} // namespace kth::domain::message

#endif
