// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_ADDRV2_HPP
#define KTH_DOMAIN_MESSAGE_ADDRV2_HPP

#include <istream>
#include <memory>
#include <string>
#include <vector>

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

/// BIP155 network identifiers
enum class bip155_network : uint8_t {
    ipv4 = 1,
    ipv6 = 2,
    torv2 = 3,   // Deprecated, 10 bytes
    torv3 = 4,   // 32 bytes
    i2p = 5,     // 32 bytes
    cjdns = 6    // 16 bytes
};

/// BIP155 address entry
struct KD_API addrv2_entry {
    uint32_t time{0};
    uint64_t services{0};
    bip155_network network{bip155_network::ipv4};
    data_chunk addr;  // Variable length based on network type
    uint16_t port{0};

    /// Check if this entry is valid
    [[nodiscard]]
    bool is_valid() const;

    /// Get the expected address size for a given network
    static
    size_t expected_addr_size(bip155_network net);

    /// Convert to network_address (only for IPv4/IPv6)
    [[nodiscard]]
    std::optional<infrastructure::message::network_address> to_network_address() const;

    /// Create from network_address
    static
    addrv2_entry from_network_address(infrastructure::message::network_address const& addr);
};

/// BIP155: addrv2 message.
/// Extended address message supporting longer addresses (Tor v3, I2P, CJDNS).
struct KD_API addrv2 {
    using ptr = std::shared_ptr<addrv2>;
    using const_ptr = std::shared_ptr<const addrv2>;
    using entry_list = std::vector<addrv2_entry>;

    addrv2() = default;
    addrv2(entry_list const& addresses);
    addrv2(entry_list&& addresses);

    bool operator==(addrv2 const& x) const;
    bool operator!=(addrv2 const& x) const;

    entry_list& addresses();

    [[nodiscard]]
    entry_list const& addresses() const;

    void set_addresses(entry_list const& value);
    void set_addresses(entry_list&& value);

    /// Convert to legacy network_address list (IPv4/IPv6 only)
    [[nodiscard]]
    infrastructure::message::network_address::list to_network_addresses() const;

    static
    expect<addrv2> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t version, W& sink) const {
        sink.write_variable_little_endian(addresses_.size());

        for (auto const& entry : addresses_) {
            sink.write_4_bytes_little_endian(entry.time);
            sink.write_variable_little_endian(entry.services);
            sink.write_byte(static_cast<uint8_t>(entry.network));
            sink.write_variable_little_endian(entry.addr.size());
            sink.write_bytes(entry.addr);
            sink.write_2_bytes_big_endian(entry.port);
        }
    }

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

    /// Maximum addresses per message (same as addr message)
    static constexpr size_t max_addresses = 1000;

    /// Maximum address size per BIP155
    static constexpr size_t max_addr_size = 512;

private:
    entry_list addresses_;
};

} // namespace kth::domain::message

#endif
