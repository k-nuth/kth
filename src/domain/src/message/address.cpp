// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/address.hpp>

// #include <kth/infrastructure/message/message_tools.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/limits.hpp>
namespace kth::domain::message {

std::string const address::command = "addr";
uint32_t const address::version_minimum = version::level::minimum;
uint32_t const address::version_maximum = version::level::maximum;

address::address(infrastructure::message::network_address::list const& addresses)
    : addresses_(addresses)
{}

address::address(infrastructure::message::network_address::list&& addresses)
    : addresses_(std::move(addresses))
{}

bool address::operator==(address const& x) const {
    return (addresses_ == x.addresses_);
}

bool address::operator!=(address const& x) const {
    return !(*this == x);
}


bool address::is_valid() const {
    return !addresses_.empty();
}

void address::reset() {
    addresses_.clear();
    addresses_.shrink_to_fit();
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<address> address::from_data(byte_reader& reader, uint32_t version) {
    auto count = reader.read_size_little_endian();
    if (!count) {
        return std::unexpected(count.error());
    }

    // Guard against potential for arbitrary memory allocation.
    if (*count > max_address) {
        return std::unexpected(error::invalid_address_count);
    }

    infrastructure::message::network_address::list addresses;
    addresses.reserve(*count);

    for (size_t i = 0; i < *count; ++i) {
        auto address = infrastructure::message::network_address::from_data(reader, version, true);
        if (!address) {
            return std::unexpected(address.error());
        }
        addresses.push_back(std::move(*address));
    }

    return address(std::move(addresses));
}

// Serialization.
//-----------------------------------------------------------------------------



size_t address::serialized_size(uint32_t version) const {
    return infrastructure::message::variable_uint_size(addresses_.size()) +
           (addresses_.size() * infrastructure::message::network_address::satoshi_fixed_size(version, true));
}

infrastructure::message::network_address::list& address::addresses() {
    return addresses_;
}

infrastructure::message::network_address::list const& address::addresses() const {
    return addresses_;
}

void address::set_addresses(infrastructure::message::network_address::list const& value) {
    addresses_ = value;
}

void address::set_addresses(infrastructure::message::network_address::list&& value) {
    addresses_ = std::move(value);
}

expect<void> address::to_data(byte_writer& writer, uint32_t version) const {
    if (auto r = writer.write_variable_little_endian(addresses_.size()); ! r) return r;
    for (auto const& net_address : addresses_) {
        if (auto r = net_address.to_data(writer, version, true); ! r) return r;
    }
    return {};
}

} // namespace kth::domain::message
