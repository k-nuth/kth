// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_ADDRESS_HPP
#define KTH_DOMAIN_MESSAGE_ADDRESS_HPP

#include <istream>
#include <memory>
#include <string>


#include <kth/domain/concepts.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/message/network_address.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>

namespace kth::domain::message {

struct KD_API address {
    using ptr = std::shared_ptr<address>;
    using const_ptr = std::shared_ptr<const address>;

    address() = default;
    address(infrastructure::message::network_address::list const& addresses);
    address(infrastructure::message::network_address::list&& addresses);

    bool operator==(address const& x) const;
    bool operator!=(address const& x) const;

    infrastructure::message::network_address::list& addresses();

    [[nodiscard]]
    infrastructure::message::network_address::list const& addresses() const;

    void set_addresses(infrastructure::message::network_address::list const& value);
    void set_addresses(infrastructure::message::network_address::list&& value);

    static
    expect<address> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t version, W& sink) const {
        sink.write_variable_little_endian(addresses_.size());

        for (auto const& net_address : addresses_) {
            net_address.to_data(version, sink, true);
        }
    }

    //void to_data(uint32_t version, writer& sink) const;

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
    infrastructure::message::network_address::list addresses_;
};

} // namespace kth::domain::message

#endif
