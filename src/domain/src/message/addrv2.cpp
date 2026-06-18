// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/addrv2.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

// Address sizes for each BIP155 network type
constexpr size_t ADDR_IPV4_SIZE = 4;
constexpr size_t ADDR_IPV6_SIZE = 16;
constexpr size_t ADDR_TORV2_SIZE = 10;  // Deprecated
constexpr size_t ADDR_TORV3_SIZE = 32;
constexpr size_t ADDR_I2P_SIZE = 32;
constexpr size_t ADDR_CJDNS_SIZE = 16;

// addrv2_entry implementation
//-----------------------------------------------------------------------------

bool addrv2_entry::is_valid() const {
    auto const expected_size = expected_addr_size(network);
    if (expected_size == 0) {
        return false;  // Unknown network type
    }
    return addr.size() == expected_size;
}

// static
size_t addrv2_entry::expected_addr_size(bip155_network net) {
    switch (net) {
        case bip155_network::ipv4:
            return ADDR_IPV4_SIZE;
        case bip155_network::ipv6:
            return ADDR_IPV6_SIZE;
        case bip155_network::torv2:
            return ADDR_TORV2_SIZE;
        case bip155_network::torv3:
            return ADDR_TORV3_SIZE;
        case bip155_network::i2p:
            return ADDR_I2P_SIZE;
        case bip155_network::cjdns:
            return ADDR_CJDNS_SIZE;
        default:
            return 0;  // Unknown network
    }
}

std::optional<infrastructure::message::network_address> addrv2_entry::to_network_address() const {
    infrastructure::message::network_address result;

    if (network == bip155_network::ipv4 && addr.size() == ADDR_IPV4_SIZE) {
        // IPv4: stored as 4 bytes, convert to IPv4-mapped IPv6 format
        // ::ffff:a.b.c.d format: 10 bytes of 0, 2 bytes of 0xff, then 4 bytes of IPv4
        infrastructure::message::ip_address ip{};
        std::fill(ip.begin(), ip.begin() + 10, 0x00);
        ip[10] = 0xff;
        ip[11] = 0xff;
        std::copy(addr.begin(), addr.end(), ip.begin() + 12);

        result.set_ip(ip);
        result.set_port(port);
        result.set_services(services);
        result.set_timestamp(time);
        return result;
    }

    if (network == bip155_network::ipv6 && addr.size() == ADDR_IPV6_SIZE) {
        // IPv6: direct copy
        infrastructure::message::ip_address ip{};
        std::copy(addr.begin(), addr.end(), ip.begin());

        result.set_ip(ip);
        result.set_port(port);
        result.set_services(services);
        result.set_timestamp(time);
        return result;
    }

    // Other network types (Tor, I2P, CJDNS) not supported for legacy conversion
    return std::nullopt;
}

// static
addrv2_entry addrv2_entry::from_network_address(infrastructure::message::network_address const& addr) {
    addrv2_entry entry;
    entry.time = addr.timestamp();
    entry.services = addr.services();
    entry.port = addr.port();

    auto const& ip = addr.ip();

    // Check if it's an IPv4-mapped IPv6 address (::ffff:x.x.x.x)
    // Pattern: 10 bytes of 0x00, then 0xff 0xff, then 4 bytes of IPv4
    bool is_ipv4_mapped = true;
    for (size_t i = 0; i < 10; ++i) {
        if (ip[i] != 0x00) {
            is_ipv4_mapped = false;
            break;
        }
    }
    is_ipv4_mapped = is_ipv4_mapped && ip[10] == 0xff && ip[11] == 0xff;

    if (is_ipv4_mapped) {
        entry.network = bip155_network::ipv4;
        entry.addr.assign(ip.begin() + 12, ip.end());
    } else {
        entry.network = bip155_network::ipv6;
        entry.addr.assign(ip.begin(), ip.end());
    }

    return entry;
}

// addrv2 implementation
//-----------------------------------------------------------------------------

std::string const addrv2::command = "addrv2";
uint32_t const addrv2::version_minimum = version::level::feature_negotiation;
uint32_t const addrv2::version_maximum = version::level::maximum;

addrv2::addrv2(entry_list const& addresses)
    : addresses_(addresses)
{}

addrv2::addrv2(entry_list&& addresses)
    : addresses_(std::move(addresses))
{}

bool addrv2::operator==(addrv2 const& x) const {
    if (addresses_.size() != x.addresses_.size()) {
        return false;
    }
    for (size_t i = 0; i < addresses_.size(); ++i) {
        if (addresses_[i].time != x.addresses_[i].time ||
            addresses_[i].services != x.addresses_[i].services ||
            addresses_[i].network != x.addresses_[i].network ||
            addresses_[i].addr != x.addresses_[i].addr ||
            addresses_[i].port != x.addresses_[i].port) {
            return false;
        }
    }
    return true;
}

bool addrv2::operator!=(addrv2 const& x) const {
    return !(*this == x);
}

bool addrv2::is_valid() const {
    return !addresses_.empty();
}

void addrv2::reset() {
    addresses_.clear();
    addresses_.shrink_to_fit();
}

addrv2::entry_list& addrv2::addresses() {
    return addresses_;
}

addrv2::entry_list const& addrv2::addresses() const {
    return addresses_;
}

void addrv2::set_addresses(entry_list const& value) {
    addresses_ = value;
}

void addrv2::set_addresses(entry_list&& value) {
    addresses_ = std::move(value);
}

infrastructure::message::network_address::list addrv2::to_network_addresses() const {
    infrastructure::message::network_address::list result;
    result.reserve(addresses_.size());

    for (auto const& entry : addresses_) {
        auto converted = entry.to_network_address();
        if (converted) {
            result.push_back(std::move(*converted));
        }
    }

    return result;
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<addrv2> addrv2::from_data(byte_reader& reader, uint32_t version) {
    auto count = reader.read_size_little_endian();
    if (!count) {
        return std::unexpected(count.error());
    }

    // Guard against potential for arbitrary memory allocation.
    if (*count > max_addresses) {
        return std::unexpected(error::invalid_address_count);
    }

    entry_list addresses;
    addresses.reserve(*count);

    for (size_t i = 0; i < *count; ++i) {
        addrv2_entry entry;

        // time: uint32
        auto time = reader.read_little_endian<uint32_t>();
        if (!time) {
            return std::unexpected(time.error());
        }
        entry.time = *time;

        // services: compactsize
        auto services = reader.read_size_little_endian();
        if (!services) {
            return std::unexpected(services.error());
        }
        entry.services = *services;

        // network: uint8
        auto network = reader.read_byte();
        if (!network) {
            return std::unexpected(network.error());
        }
        entry.network = static_cast<bip155_network>(*network);

        // addr_len: compactsize
        auto addr_len = reader.read_size_little_endian();
        if (!addr_len) {
            return std::unexpected(addr_len.error());
        }

        if (*addr_len > max_addr_size) {
            return std::unexpected(error::invalid_address_count);
        }

        // addr: variable bytes
        auto addr_bytes = reader.read_bytes(*addr_len);
        if (!addr_bytes) {
            return std::unexpected(addr_bytes.error());
        }
        entry.addr.assign(addr_bytes->begin(), addr_bytes->end());

        // port: uint16 big-endian
        auto port = reader.read_big_endian<uint16_t>();
        if (!port) {
            return std::unexpected(port.error());
        }
        entry.port = *port;

        // Validate expected address size for known networks
        auto const expected_size = addrv2_entry::expected_addr_size(entry.network);
        if (expected_size > 0 && entry.addr.size() != expected_size) {
            // Skip invalid entries but continue parsing
            continue;
        }

        addresses.push_back(std::move(entry));
    }

    return addrv2(std::move(addresses));
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk addrv2::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void addrv2::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

size_t addrv2::serialized_size(uint32_t /*version*/) const {
    size_t size = infrastructure::message::variable_uint_size(addresses_.size());

    for (auto const& entry : addresses_) {
        size += 4;  // time
        size += infrastructure::message::variable_uint_size(entry.services);
        size += 1;  // network
        size += infrastructure::message::variable_uint_size(entry.addr.size());
        size += entry.addr.size();
        size += 2;  // port
    }

    return size;
}

} // namespace kth::domain::message
