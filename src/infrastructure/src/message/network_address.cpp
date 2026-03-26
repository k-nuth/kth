// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/message/network_address.hpp>

#include <algorithm>
#include <cstdint>

#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::infrastructure::message {

// TODO(legacy): create derived address that adds the timestamp.

bool network_address::operator==(network_address const& x) const {
    return (services_ == x.services_) && (port_ == x.port_) && (ip_ == x.ip_);
}

bool network_address::operator!=(network_address const& x) const {
    return !(*this == x);
}

bool network_address::is_valid() const {
    return (timestamp_ != 0)
        || (services_ != 0)
        || (port_ != 0)
        || (ip_ != null_address);
}

namespace {

// Check if address is IPv4-mapped in IPv6 (::ffff:x.x.x.x)
inline bool is_ipv4_mapped(ip_address const& ip) {
    // IPv4-mapped IPv6: 00 00 00 00 00 00 00 00 00 00 FF FF xx xx xx xx
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0 &&
           ip[4] == 0 && ip[5] == 0 && ip[6] == 0 && ip[7] == 0 &&
           ip[8] == 0 && ip[9] == 0 && ip[10] == 0xff && ip[11] == 0xff;
}

// Get the IPv4 bytes from a mapped address (last 4 bytes)
inline std::array<uint8_t, 4> get_ipv4_bytes(ip_address const& ip) {
    return {ip[12], ip[13], ip[14], ip[15]};
}

// RFC1918: Private IPv4 (10.x.x.x, 172.16-31.x.x, 192.168.x.x)
inline bool is_rfc1918(ip_address const& ip) {
    if (!is_ipv4_mapped(ip)) return false;
    auto const ipv4 = get_ipv4_bytes(ip);
    return ipv4[0] == 10 ||
           (ipv4[0] == 192 && ipv4[1] == 168) ||
           (ipv4[0] == 172 && ipv4[1] >= 16 && ipv4[1] <= 31);
}

// RFC3927: Link-local IPv4 (169.254.x.x)
inline bool is_rfc3927(ip_address const& ip) {
    if (!is_ipv4_mapped(ip)) return false;
    auto const ipv4 = get_ipv4_bytes(ip);
    return ipv4[0] == 169 && ipv4[1] == 254;
}

// RFC5737: Documentation IPv4 (192.0.2.x, 198.51.100.x, 203.0.113.x)
inline bool is_rfc5737(ip_address const& ip) {
    if (!is_ipv4_mapped(ip)) return false;
    auto const ipv4 = get_ipv4_bytes(ip);
    return (ipv4[0] == 192 && ipv4[1] == 0 && ipv4[2] == 2) ||
           (ipv4[0] == 198 && ipv4[1] == 51 && ipv4[2] == 100) ||
           (ipv4[0] == 203 && ipv4[1] == 0 && ipv4[2] == 113);
}

// RFC6598: Carrier-grade NAT (100.64-127.x.x)
inline bool is_rfc6598(ip_address const& ip) {
    if (!is_ipv4_mapped(ip)) return false;
    auto const ipv4 = get_ipv4_bytes(ip);
    return ipv4[0] == 100 && ipv4[1] >= 64 && ipv4[1] <= 127;
}

// RFC2544: Benchmarking (198.18.x.x, 198.19.x.x)
inline bool is_rfc2544(ip_address const& ip) {
    if (!is_ipv4_mapped(ip)) return false;
    auto const ipv4 = get_ipv4_bytes(ip);
    return ipv4[0] == 198 && (ipv4[1] == 18 || ipv4[1] == 19);
}

// IPv4 loopback (127.x.x.x) or unspecified (0.x.x.x)
inline bool is_ipv4_local(ip_address const& ip) {
    if (!is_ipv4_mapped(ip)) return false;
    auto const ipv4 = get_ipv4_bytes(ip);
    return ipv4[0] == 127 || ipv4[0] == 0;
}

// RFC4862: IPv6 Link-local (FE80::/10)
inline bool is_rfc4862(ip_address const& ip) {
    // Pure IPv6 check (not IPv4-mapped)
    if (is_ipv4_mapped(ip)) return false;
    return ip[0] == 0xfe && (ip[1] & 0xc0) == 0x80;
}

// RFC4193: IPv6 Unique-local (FC00::/7)
inline bool is_rfc4193(ip_address const& ip) {
    if (is_ipv4_mapped(ip)) return false;
    return (ip[0] & 0xfe) == 0xfc;
}

// RFC3849: IPv6 Documentation (2001:DB8::/32)
inline bool is_rfc3849(ip_address const& ip) {
    if (is_ipv4_mapped(ip)) return false;
    return ip[0] == 0x20 && ip[1] == 0x01 && ip[2] == 0x0d && ip[3] == 0xb8;
}

// IPv6 loopback (::1)
inline bool is_ipv6_loopback(ip_address const& ip) {
    if (is_ipv4_mapped(ip)) return false;
    return ip == ip_address{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
}

// IPv6 unspecified (::)
inline bool is_ipv6_unspecified(ip_address const& ip) {
    return ip == null_address;
}

} // anonymous namespace

bool network_address::is_routable() const {
    if (!is_valid()) return false;
    if (port_ == 0) return false;

    // Check for non-routable addresses
    // IPv4-mapped addresses
    if (is_ipv4_mapped(ip_)) {
        if (is_rfc1918(ip_)) return false;   // Private
        if (is_rfc3927(ip_)) return false;   // Link-local
        if (is_rfc5737(ip_)) return false;   // Documentation
        if (is_rfc6598(ip_)) return false;   // Carrier-grade NAT
        if (is_rfc2544(ip_)) return false;   // Benchmarking
        if (is_ipv4_local(ip_)) return false; // Loopback/unspecified
    } else {
        // Pure IPv6
        if (is_rfc4862(ip_)) return false;   // Link-local
        if (is_rfc4193(ip_)) return false;   // Unique-local
        if (is_rfc3849(ip_)) return false;   // Documentation
        if (is_ipv6_loopback(ip_)) return false;
        if (is_ipv6_unspecified(ip_)) return false;
    }

    return true;
}

void network_address::reset() {
    timestamp_ = 0;
    services_ = 0;
    ip_.fill(0);
    port_ = 0;
}

// static
expect<network_address> network_address::from_data(byte_reader& reader, uint32_t version, bool with_timestamp) {
    uint32_t timestamp = 0;
    if (with_timestamp) {
        auto const timestamp_exp = reader.read_little_endian<uint32_t>();
        if ( ! timestamp_exp) {
            return std::unexpected(timestamp_exp.error());
        }
        timestamp = *timestamp_exp;
    }

    auto const services = reader.read_little_endian<uint64_t>();
    if ( ! services) {
        return std::unexpected(services.error());
    }

    auto const ip = reader.read_bytes(std::tuple_size<ip_address>::value);
    if ( ! ip) {
        return std::unexpected(ip.error());
    }

    auto const port = reader.read_big_endian<uint16_t>();
    if ( ! port) {
        return std::unexpected(port.error());
    }

    ip_address ip_addr;
    std::memcpy(ip_addr.data(), ip->data(), ip->size());
    return network_address(timestamp, *services, ip_addr, *port);
}

data_chunk network_address::to_data(uint32_t version, bool with_timestamp) const {
    data_chunk data;
    auto const size = serialized_size(version, with_timestamp);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream, with_timestamp);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void network_address::to_data(uint32_t version, data_sink& stream, bool with_timestamp) const {
    ostream_writer sink(stream);
    to_data(version, sink, with_timestamp);
}

size_t network_address::serialized_size(uint32_t version, bool with_timestamp) const {
    return network_address::satoshi_fixed_size(version, with_timestamp);
}

size_t network_address::satoshi_fixed_size(uint32_t version, bool with_timestamp) {
    size_t result = 26;

    if (with_timestamp) {
        result += 4u;
    }

    return result;
}

uint32_t network_address::timestamp() const {
    return timestamp_;
}

void network_address::set_timestamp(uint32_t value) {
    timestamp_ = value;
}

uint64_t network_address::services() const {
    return services_;
}

void network_address::set_services(uint64_t value) {
    services_ = value;
}

ip_address& network_address::ip() {
    return ip_;
}

ip_address const& network_address::ip() const {
    return ip_;
}

void network_address::set_ip(ip_address const& value) {
    ip_ = value;
}

uint16_t network_address::port() const {
    return port_;
}

void network_address::set_port(uint16_t value) {
    port_ = value;
}

} // namespace kth::infrastructure::message
