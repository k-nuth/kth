// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Test to compare our addrv2 parser with BCHN's EXACT implementation

#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

// BCHN includes - use their classes directly
#include <serialize.h>
#include <streams.h>
#include <netaddress.h>
#include <protocol.h>

// KTH includes for comparison
#include <kth/domain/message/addrv2.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

std::vector<uint8_t> hex_to_bytes(std::string const& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
    }
    return bytes;
}

std::string read_hex_file(std::string const& filename) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Cannot open: " << filename << "\n";
        return "";
    }
    std::string hex;
    file >> hex;
    return hex;
}

void dump_hex(std::vector<uint8_t> const& data, size_t offset, size_t len) {
    for (size_t i = 0; i < len && offset + i < data.size(); ++i) {
        printf("%02x ", data[offset + i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
}

// BCHN test vector from bchn/src/test/netbase_tests.cpp
static const char* bchn_test_addrv2_hex =
    "03" // number of entries

    "61bc6649"                         // time, Fri Jan  9 02:54:25 UTC 2009
    "00"                               // service flags, COMPACTSIZE(NODE_NONE)
    "02"                               // network id, IPv6
    "10"                               // address length, COMPACTSIZE(16)
    "00000000000000000000000000000001" // address
    "0000"                             // port

    "79627683"                         // time, Tue Nov 22 11:22:33 UTC 2039
    "01"                               // service flags, COMPACTSIZE(NODE_NETWORK)
    "02"                               // network id, IPv6
    "10"                               // address length, COMPACTSIZE(16)
    "00000000000000000000000000000001" // address
    "00f1"                             // port

    "ffffffff"                         // time, Sun Feb  7 06:28:15 UTC 2106
    "fd0004"                           // service flags, COMPACTSIZE(NODE_NETWORK_LIMITED)
    "02"                               // network id, IPv6
    "10"                               // address length, COMPACTSIZE(16)
    "00000000000000000000000000000001" // address
    "f1f2";                            // port

// Expected values from BCHN test
struct ExpectedAddr {
    uint32_t time;
    uint64_t services;
    uint16_t port;
};
static const ExpectedAddr bchn_expected[] = {
    {0x4966bc61, 0, 0},        // NODE_NONE, port 0
    {0x83766279, 1, 0x00f1},   // NODE_NETWORK, port 241
    {0xffffffff, 0x0400, 0xf1f2}, // NODE_NETWORK_LIMITED (1024), port 61938
};

void run_bchn_test_vector() {
    std::cout << "=== BCHN TEST VECTOR ===\n";
    auto payload = hex_to_bytes(bchn_test_addrv2_hex);
    std::cout << "Payload: " << payload.size() << " bytes\n\n";

    // BCHN parser
    std::cout << "--- BCHN Parser ---\n";
    VectorReader bchn_stream(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT, payload, 0);
    std::vector<CAddress> bchn_addresses;
    bchn_stream >> bchn_addresses;

    bool bchn_ok = true;
    for (size_t i = 0; i < bchn_addresses.size(); ++i) {
        auto const& addr = bchn_addresses[i];
        auto const& exp = bchn_expected[i];
        bool match = (addr.nTime == exp.time && addr.nServices == exp.services && addr.GetPort() == exp.port);
        std::cout << "[BCHN] Entry " << i
                  << ": time=" << addr.nTime << " (exp " << exp.time << ")"
                  << " svc=" << addr.nServices << " (exp " << exp.services << ")"
                  << " port=" << addr.GetPort() << " (exp " << exp.port << ")"
                  << (match ? " OK" : " FAIL")
                  << "\n";
        if (!match) bchn_ok = false;
    }
    std::cout << "[BCHN] " << (bchn_ok ? "ALL TESTS PASSED" : "TESTS FAILED") << "\n\n";

    // KTH parser
    std::cout << "--- KTH Parser ---\n";
    kth::byte_reader reader(payload);
    auto count = reader.read_size_little_endian();
    std::cout << "[KTH] Count: " << *count << "\n";

    bool kth_ok = true;
    for (size_t i = 0; i < *count; ++i) {
        auto time = reader.read_little_endian<uint32_t>();
        auto services = reader.read_size_little_endian();
        auto network = reader.read_byte();
        auto addr_len = reader.read_size_little_endian();
        auto addr_bytes = reader.read_bytes(*addr_len);
        auto port = reader.read_big_endian<uint16_t>();

        auto const& exp = bchn_expected[i];
        bool match = (*time == exp.time && *services == exp.services && *port == exp.port);
        std::cout << "[KTH] Entry " << i
                  << ": time=" << *time << " (exp " << exp.time << ")"
                  << " svc=" << *services << " (exp " << exp.services << ")"
                  << " port=" << *port << " (exp " << exp.port << ")"
                  << (match ? " OK" : " FAIL")
                  << "\n";
        if (!match) kth_ok = false;
    }
    std::cout << "[KTH] " << (kth_ok ? "ALL TESTS PASSED" : "TESTS FAILED") << "\n\n";
}

int main(int argc, char** argv) {
    // Always run BCHN test vector first
    run_bchn_test_vector();

    // Then optionally test a file
    if (argc <= 1) {
        std::cout << "To test a file: " << argv[0] << " <hex_file>\n";
        return 0;
    }

    std::string hex_payload = read_hex_file(argv[1]);
    if (hex_payload.empty()) return 1;

    auto payload = hex_to_bytes(hex_payload);
    std::cout << "=== FILE PAYLOAD TEST ===\n";
    std::cout << "Payload: " << payload.size() << " bytes\n\n";

    // =========================================================================
    // BCHN Parser - using their classes directly
    // =========================================================================
    std::cout << "--- BCHN Parser ---\n";

    VectorReader bchn_stream(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT, payload, 0);

    uint64_t bchn_count;
    bchn_stream >> COMPACTSIZE(bchn_count);
    std::cout << "[BCHN] Count: " << bchn_count << " addresses\n";

    std::vector<CAddress> bchn_addresses;
    size_t bchn_port0 = 0;
    size_t bchn_bad_svc = 0;
    for (uint64_t i = 0; i < bchn_count; ++i) {
        CAddress addr;
        bchn_stream >> addr;
        bchn_addresses.push_back(addr);

        bool is_bad = (addr.GetPort() == 0 || addr.nServices > 0xFFFFFFFF);
        if (i < 5 || is_bad) {
            std::cout << "[BCHN] Entry " << i
                      << ": port=" << addr.GetPort()
                      << " svc=" << addr.nServices
                      << (is_bad ? " *** BAD ***" : "")
                      << "\n";
        }
        if (addr.GetPort() == 0) ++bchn_port0;
        if (addr.nServices > 0xFFFFFFFF) ++bchn_bad_svc;
    }
    std::cout << "[BCHN] Parsed: " << bchn_addresses.size()
              << ", port=0: " << bchn_port0
              << ", bad_svc: " << bchn_bad_svc << "\n\n";

    // =========================================================================
    // KTH Parser
    // =========================================================================
    std::cout << "--- KTH Parser ---\n";

    kth::byte_reader reader(payload);
    auto count_result = reader.read_size_little_endian();
    if (!count_result) {
        std::cout << "[KTH] Failed to read count\n";
        return 1;
    }

    uint64_t kth_count = *count_result;
    std::cout << "[KTH] Count: " << kth_count << " addresses\n";

    std::vector<std::tuple<uint16_t, uint64_t, size_t>> kth_addresses;  // port, services, start_pos
    size_t kth_port0 = 0;
    size_t kth_bad_svc = 0;
    for (uint64_t i = 0; i < kth_count; ++i) {
        size_t entry_start = reader.position();

        auto time = reader.read_little_endian<uint32_t>();
        if (!time) break;

        auto services = reader.read_size_little_endian();
        if (!services) break;

        auto network = reader.read_byte();
        if (!network) break;

        auto addr_len = reader.read_size_little_endian();
        if (!addr_len) break;

        auto addr_bytes = reader.read_bytes(*addr_len);
        if (!addr_bytes) break;

        auto port = reader.read_big_endian<uint16_t>();
        if (!port) break;

        kth_addresses.emplace_back(*port, *services, entry_start);

        bool is_bad = (*port == 0 || *services > 0xFFFFFFFF);
        if (i < 5 || is_bad) {
            std::cout << "[KTH] Entry " << i
                      << " @" << entry_start
                      << ": time=" << *time
                      << " net=" << (int)*network
                      << " len=" << *addr_len
                      << " port=" << *port
                      << " svc=" << *services
                      << (is_bad ? " *** BAD ***" : "")
                      << "\n";
            if (is_bad && i < 20) {
                std::cout << "  Raw bytes at offset " << entry_start << ":\n  ";
                dump_hex(payload, entry_start, 40);
            }
        }
        if (*port == 0) ++kth_port0;
        if (*services > 0xFFFFFFFF) ++kth_bad_svc;
    }
    std::cout << "[KTH] Parsed: " << kth_addresses.size()
              << ", port=0: " << kth_port0
              << ", bad_svc: " << kth_bad_svc << "\n\n";

    // =========================================================================
    // Compare
    // =========================================================================
    std::cout << "--- Comparison ---\n";
    size_t min_len = std::min(bchn_addresses.size(), kth_addresses.size());
    for (size_t i = 0; i < min_len; ++i) {
        auto const& b = bchn_addresses[i];
        auto const& [k_port, k_svc, k_pos] = kth_addresses[i];

        if (b.GetPort() != k_port || b.nServices != k_svc) {
            std::cout << "DIVERGENCE at entry " << i << ":\n";
            std::cout << "  BCHN: port=" << b.GetPort() << " svc=" << b.nServices << "\n";
            std::cout << "  KTH:  port=" << k_port << " svc=" << k_svc << "\n";
            break;
        }
    }

    if (bchn_addresses.size() == kth_addresses.size()) {
        std::cout << "Both parsers agree!\n";
    }

    return 0;
}
