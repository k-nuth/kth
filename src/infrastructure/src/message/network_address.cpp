// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/message/network_address.hpp>

#include <algorithm>
#include <cstdint>

#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
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
