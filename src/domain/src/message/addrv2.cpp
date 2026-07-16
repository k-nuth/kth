// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/addrv2.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/limits.hpp>
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

addrv2_entry::addrv2_entry(uint32_t time, uint64_t services,
    bip155_network network, data_chunk addr, uint16_t port)
    : time_(time)
    , services_(services)
    , network_(network)
    , addr_(std::move(addr))
    , port_(port)
{}

// static
expect<addrv2_entry> addrv2_entry::create(uint32_t time, uint64_t services,
    bip155_network network, data_chunk addr, uint16_t port) {
    // BIP155 fixes one address length per network; an unknown id or a length
    // that does not match it cannot describe a real address.
    auto const expected = expected_addr_size(network);
    if (expected == 0 || addr.size() != expected) {
        return std::unexpected(error::invalid_address);
    }
    return addrv2_entry(time, services, network, std::move(addr), port);
}

uint32_t addrv2_entry::time() const {
    return time_;
}

uint64_t addrv2_entry::services() const {
    return services_;
}

bip155_network addrv2_entry::network() const {
    return network_;
}

data_chunk const& addrv2_entry::addr() const {
    return addr_;
}

uint16_t addrv2_entry::port() const {
    return port_;
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
    infrastructure::message::ip_address ip{};

    if (network_ == bip155_network::ipv4 && addr_.size() == ADDR_IPV4_SIZE) {
        // IPv4 is carried as 4 bytes; widen to the IPv4-mapped IPv6 form
        // ::ffff:a.b.c.d -- 10 zero bytes, 0xff 0xff, then the 4 IPv4 bytes.
        ip[10] = 0xff;
        ip[11] = 0xff;
        std::copy(addr_.begin(), addr_.end(), ip.begin() + 12);
    } else if (network_ == bip155_network::ipv6 && addr_.size() == ADDR_IPV6_SIZE) {
        std::copy(addr_.begin(), addr_.end(), ip.begin());
    } else {
        // Tor, I2P and CJDNS have no legacy network_address representation.
        return std::nullopt;
    }

    return infrastructure::message::network_address(time_, services_, ip, port_);
}

// static
addrv2_entry addrv2_entry::from_network_address(infrastructure::message::network_address const& addr) {
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

    auto const network = is_ipv4_mapped ? bip155_network::ipv4 : bip155_network::ipv6;
    data_chunk bytes(is_ipv4_mapped ? ip.begin() + 12 : ip.begin(), ip.end());

    // IPv4 is 4 bytes, IPv6 is 16 -- each matches its network, so create never
    // fails here.
    return create(addr.timestamp(), addr.services(), network,
        std::move(bytes), addr.port()).value();
}

// addrv2 implementation
//-----------------------------------------------------------------------------

std::string const addrv2::command = "addrv2";
uint32_t const addrv2::version_minimum = version::level::feature_negotiation;
uint32_t const addrv2::version_maximum = version::level::maximum;

addrv2::addrv2(entry_list addresses)
    : addresses_(std::move(addresses))
{}

// static
expect<addrv2> addrv2::create(entry_list addresses) {
    if (addresses.size() > max_addresses) {
        return std::unexpected(error::invalid_address_count);
    }
    return addrv2(std::move(addresses));
}

addrv2::addrv2::entry_list const& addrv2::addresses() const {
    return addresses_;
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
        // time: uint32
        auto time = reader.read_little_endian<uint32_t>();
        if (!time) {
            return std::unexpected(time.error());
        }

        // services: compactsize
        auto services = reader.read_size_little_endian();
        if (!services) {
            return std::unexpected(services.error());
        }

        // network: uint8
        auto network_byte = reader.read_byte();
        if (!network_byte) {
            return std::unexpected(network_byte.error());
        }
        auto const network = static_cast<bip155_network>(*network_byte);

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

        // port: uint16 big-endian
        auto port = reader.read_big_endian<uint16_t>();
        if (!port) {
            return std::unexpected(port.error());
        }

        // BIP155: an unknown network id may be a future type. Drop it and keep
        // parsing (its bytes are already consumed) rather than fail, so a peer
        // can still relay the entries we do understand.
        if (addrv2_entry::expected_addr_size(network) == 0) {
            continue;
        }

        // A known network with the wrong length is a malformed message, not a
        // droppable entry. create() rejects it, and we reject the whole addrv2
        // -- matching BCHN.
        auto entry = addrv2_entry::create(*time, *services, network,
            data_chunk(addr_bytes->begin(), addr_bytes->end()), *port);
        if (!entry) {
            return std::unexpected(entry.error());
        }

        addresses.push_back(std::move(*entry));
    }

    return create(std::move(addresses));
}

// Serialization.
//-----------------------------------------------------------------------------

size_t addrv2::serialized_size(uint32_t /*version*/) const {
    size_t size = infrastructure::message::variable_uint_size(addresses_.size());

    for (auto const& entry : addresses_) {
        size += 4;  // time
        size += infrastructure::message::variable_uint_size(entry.services());
        size += 1;  // network
        size += infrastructure::message::variable_uint_size(entry.addr().size());
        size += entry.addr().size();
        size += 2;  // port
    }

    return size;
}

expect<void> addrv2::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_variable_little_endian(addresses_.size()); ! r) return r;

        for (auto const& entry : addresses_) {
            if (auto r = writer.write_little_endian<uint32_t>(entry.time()); ! r) return r;
            if (auto r = writer.write_variable_little_endian(entry.services()); ! r) return r;
            if (auto r = writer.write_byte(static_cast<uint8_t>(entry.network())); ! r) return r;
            if (auto r = writer.write_variable_little_endian(entry.addr().size()); ! r) return r;
            if (auto r = writer.write_bytes(entry.addr()); ! r) return r;
            if (auto r = writer.write_big_endian<uint16_t>(entry.port()); ! r) return r;
        }
        return {};
}

} // namespace kth::domain::message
