// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_ADDRV2_HPP
#define KTH_DOMAIN_MESSAGE_ADDRV2_HPP

#include <memory>
#include <string>
#include <vector>

#include <kth/domain/concepts.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/message/network_address.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
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
    /// Fails with error::invalid_address when the network id is unknown or the
    /// address length does not match it (BIP155 fixes one length per network).
    static
    expect<addrv2_entry> create(uint32_t time, uint64_t services,
        bip155_network network, data_chunk addr, uint16_t port);

    [[nodiscard]]
    friend bool operator==(addrv2_entry const&, addrv2_entry const&) = default;

    [[nodiscard]] uint32_t time() const;
    [[nodiscard]] uint64_t services() const;
    [[nodiscard]] bip155_network network() const;
    [[nodiscard]] data_chunk const& addr() const;
    [[nodiscard]] uint16_t port() const;

    /// The address length BIP155 fixes for a network id, or 0 if unknown.
    static
    size_t expected_addr_size(bip155_network net);

    /// Convert to network_address (only for IPv4/IPv6)
    [[nodiscard]]
    std::optional<infrastructure::message::network_address> to_network_address() const;

    /// Create from network_address. IPv4/IPv6 always yield a valid entry.
    static
    addrv2_entry from_network_address(infrastructure::message::network_address const& addr);

private:
    addrv2_entry(uint32_t time, uint64_t services, bip155_network network,
        data_chunk addr, uint16_t port);

    uint32_t time_;
    uint64_t services_;
    bip155_network network_;
    data_chunk addr_;
    uint16_t port_;
};

/// BIP155: addrv2 message.
/// Extended address message supporting longer addresses (Tor v3, I2P, CJDNS).
struct KD_API addrv2 {
    using ptr = std::shared_ptr<addrv2>;
    using const_ptr = std::shared_ptr<const addrv2>;
    using entry_list = std::vector<addrv2_entry>;

    addrv2() = default;

    /// Fails with error::invalid_address_count over max_addresses entries.
    static
    expect<addrv2> create(entry_list addresses);

    [[nodiscard]]
    friend bool operator==(addrv2 const&, addrv2 const&) = default;

    [[nodiscard]]
    entry_list const& addresses() const;


    /// Convert to legacy network_address list (IPv4/IPv6 only)
    [[nodiscard]]
    infrastructure::message::network_address::list to_network_addresses() const;

    static
    expect<addrv2> from_data(byte_reader& reader, uint32_t version);

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

    /// Maximum addresses per message (same as addr message)
    static constexpr size_t max_addresses = 1000;

    /// Maximum address size per BIP155
    static constexpr size_t max_addr_size = 512;

private:
    addrv2(entry_list addresses);

    entry_list addresses_;
};

} // namespace kth::domain::message

#endif
