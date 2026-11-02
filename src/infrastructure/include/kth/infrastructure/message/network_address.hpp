// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_MESSAGE_NETWORK_ADDRESS_HPP
#define KTH_INFRASTUCTURE_MESSAGE_NETWORK_ADDRESS_HPP

#include <cstdint>
#include <istream>
#include <vector>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>

namespace kth::infrastructure::message {

using ip_address = byte_array<16>;
constexpr ip_address null_address {{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }};

enum class address_network : uint8_t { ip, torv3 = 4, i2p = 5 };
using overlay_address = byte_array<32>;

struct KI_API network_address {
    using list = std::vector<network_address>;

    network_address() = default;

    constexpr
    network_address(uint32_t timestamp, uint64_t services, ip_address const& ip, uint16_t port)
        : timestamp_(timestamp)
        , services_(services)
        , ip_(ip)
        , port_(port)
    {}

    constexpr
    network_address(uint32_t timestamp, uint64_t services, address_network network,
        message::overlay_address const& address, uint16_t port)
        : timestamp_(timestamp)
        , services_(services)
        , port_(port)
        , network_(network)
        , overlay_(address)
    {}

    network_address(network_address const& x) = default;
    network_address& operator=(network_address const& x) = default;

    bool operator==(network_address const& x) const;
    bool operator!=(network_address const& x) const;

    // Starting version 31402, addresses are prefixed with a timestamp.
    uint32_t timestamp() const;
    void set_timestamp(uint32_t value);

    uint64_t services() const;
    void set_services(uint64_t value);

    ip_address& ip();
    ip_address const& ip() const;
    void set_ip(ip_address const& value);

    uint16_t port() const;
    void set_port(uint16_t value);

    [[nodiscard]] address_network network() const { return network_; }
    [[nodiscard]] bool is_overlay() const { return network_ != address_network::ip; }
    [[nodiscard]] message::overlay_address const& overlay_address() const { return overlay_; }

    size_t serialized_size(uint32_t version, bool with_timestamp) const;

    static
    expect<network_address> from_data(byte_reader& reader, uint32_t version, bool with_timestamp);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version, bool with_timestamp) const noexcept;

    bool is_valid() const;
    bool is_routable() const;
    void reset();

    static
    size_t satoshi_fixed_size(uint32_t version, bool with_timestamp);


private:
    uint32_t timestamp_{0};
    uint64_t services_{0};
    ip_address ip_{null_address};
    uint16_t port_{0};
    address_network network_{address_network::ip};
    message::overlay_address overlay_{};
};

// version::services::none
constexpr uint32_t no_services = 0;
constexpr uint32_t no_timestamp = 0;
constexpr uint16_t unspecified_ip_port = 0;
constexpr ip_address unspecified_ip_address {{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00
}};

// Defaults to full node services.
constexpr network_address unspecified_network_address {
    no_timestamp,
    no_services,
    unspecified_ip_address,
    unspecified_ip_port
};

} // namespace kth::infrastructure::message

#endif
