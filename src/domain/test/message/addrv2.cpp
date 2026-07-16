// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/domain/message/addrv2.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

using namespace kth;
using namespace kd;
using namespace kd::message;

namespace {

// One addrv2 entry on the wire: time(4) services(varint) net(1) addr_len(varint)
// addr(len) port(2 BE). Helper builds one with a single-byte varint length.
data_chunk entry_bytes(uint32_t time, uint8_t services, uint8_t net,
    data_chunk const& addr, uint16_t port) {
    data_chunk out;
    out.push_back(uint8_t(time));
    out.push_back(uint8_t(time >> 8));
    out.push_back(uint8_t(time >> 16));
    out.push_back(uint8_t(time >> 24));
    out.push_back(services);
    out.push_back(net);
    out.push_back(uint8_t(addr.size()));       // addr_len (< 253, so one byte)
    out.insert(out.end(), addr.begin(), addr.end());
    out.push_back(uint8_t(port >> 8));          // port is big-endian
    out.push_back(uint8_t(port));
    return out;
}

data_chunk one_entry_message(data_chunk const& entry) {
    data_chunk out{0x01};                        // count = 1
    out.insert(out.end(), entry.begin(), entry.end());
    return out;
}

data_chunk const ipv6(16, 0x11);

} // namespace

// Start Test Suite: addrv2 tests

TEST_CASE("addrv2_entry create accepts a known network at its size", "[addrv2]") {
    auto const entry = addrv2_entry::create(1234u, 1u, bip155_network::ipv6, ipv6, 8333u);
    REQUIRE(entry);
    REQUIRE(entry->network() == bip155_network::ipv6);
    REQUIRE(entry->addr() == ipv6);
    REQUIRE(entry->port() == 8333u);
}

TEST_CASE("addrv2_entry create rejects an unknown network", "[addrv2]") {
    auto const net = static_cast<bip155_network>(0x07);   // beyond cjdns=6
    auto const entry = addrv2_entry::create(1u, 0u, net, data_chunk(16, 0x00), 0u);
    REQUIRE( ! entry);
    REQUIRE(entry.error() == error::invalid_address);
}

TEST_CASE("addrv2_entry create rejects a known network at the wrong size", "[addrv2]") {
    // ipv6 must be 16 bytes; hand it 15.
    auto const entry = addrv2_entry::create(1u, 0u, bip155_network::ipv6, data_chunk(15, 0x00), 0u);
    REQUIRE( ! entry);
    REQUIRE(entry.error() == error::invalid_address);
}

TEST_CASE("addrv2_entry from_network_address round-trips through create", "[addrv2]") {
    infrastructure::message::ip_address ip{};
    std::copy(ipv6.begin(), ipv6.end(), ip.begin());
    infrastructure::message::network_address const addr(1234u, 1u, ip, 8333u);

    auto const entry = addrv2_entry::from_network_address(addr);
    REQUIRE(entry.network() == bip155_network::ipv6);
    REQUIRE(entry.addr() == ipv6);
    REQUIRE(entry.port() == 8333u);
}

TEST_CASE("addrv2 from_data keeps a well-formed entry", "[addrv2]") {
    auto const raw = one_entry_message(
        entry_bytes(1234u, 1u, uint8_t(bip155_network::ipv6), ipv6, 8333u));
    byte_reader reader(raw);
    auto const result = addrv2::from_data(reader, version::level::maximum);
    REQUIRE(result);
    REQUIRE(result->addresses().size() == 1u);
}

TEST_CASE("addrv2 from_data drops an unknown-network entry but keeps parsing", "[addrv2]") {
    // BIP155: an unknown network id (maybe a future type) is silently dropped,
    // and the entries we understand are still returned -- matching BCHN.
    data_chunk raw{0x02};                        // two entries
    auto const unknown = entry_bytes(1u, 0u, 0x07, data_chunk(20, 0xaa), 0u);
    auto const known = entry_bytes(1234u, 1u, uint8_t(bip155_network::ipv6), ipv6, 8333u);
    raw.insert(raw.end(), unknown.begin(), unknown.end());
    raw.insert(raw.end(), known.begin(), known.end());

    byte_reader reader(raw);
    auto const result = addrv2::from_data(reader, version::level::maximum);
    REQUIRE(result);
    REQUIRE(result->addresses().size() == 1u);   // the unknown one is gone
    REQUIRE(result->addresses()[0].network() == bip155_network::ipv6);
}

TEST_CASE("addrv2 from_data rejects the whole message on a bad known-network size", "[addrv2]") {
    // A known network (ipv6) with the wrong length is malformed. BCHN rejects
    // the entire message rather than skipping the entry.
    auto const bad = entry_bytes(1u, 0u, uint8_t(bip155_network::ipv6), data_chunk(15, 0x00), 0u);
    auto const raw = one_entry_message(bad);
    byte_reader reader(raw);
    auto const result = addrv2::from_data(reader, version::level::maximum);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::invalid_address);
}

// End Test Suite
