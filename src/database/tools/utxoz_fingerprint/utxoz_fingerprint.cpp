// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Standalone tool to compute a deterministic fingerprint of a UTXO-Z database.
// Usage: utxoz_fingerprint <path-to-utxoz-directory>
//
// Iterates all entries (key + height + value), hashes each one individually,
// XORs all hashes together (order-independent), and prints:
//   - entry count
//   - total value bytes
//   - fingerprint (hex)
//
// Two UTXO-Z databases with identical logical content will produce
// the same fingerprint regardless of physical file layout.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include <utxoz/utxoz.hpp>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <path-to-utxoz-directory>\n", argv[0]);
        return 1;
    }

    std::filesystem::path db_path(argv[1]);
    if ( ! std::filesystem::exists(db_path)) {
        std::fprintf(stderr, "Error: directory does not exist: %s\n", argv[1]);
        return 1;
    }

    utxoz::db database;
    try {
        database.configure(db_path.string());
    } catch (std::exception const& e) {
        std::fprintf(stderr, "Error: failed to open UTXO-Z database at: %s (%s)\n", argv[1], e.what());
        return 1;
    }

    std::printf("Opened UTXO-Z database: %s (%zu entries)\n", argv[1], database.size());

    // XOR-accumulator for order-independent fingerprint
    kth::hash_digest fingerprint{};  // zero-initialized (32 bytes)

    size_t count = 0;
    size_t total_value_bytes = 0;

    // Scratch buffer for per-entry hash input: key(36) + height(4) + data(variable)
    std::vector<uint8_t> buf;

    database.for_each_entry([&](utxoz::raw_outpoint const& key, uint32_t height, std::span<uint8_t const> data) {
        // Build hash input: key || height_le || data
        size_t const needed = key.size() + sizeof(height) + data.size();
        if (buf.size() < needed) {
            buf.resize(needed);
        }

        auto* p = buf.data();
        std::memcpy(p, key.data(), key.size());
        p += key.size();
        std::memcpy(p, &height, sizeof(height));
        p += sizeof(height);
        std::memcpy(p, data.data(), data.size());

        // SHA256 of this entry
        auto const entry_hash = kth::sha256_hash(kth::byte_span{buf.data(), needed});

        // XOR into accumulator
        for (size_t i = 0; i < fingerprint.size(); ++i) {
            fingerprint[i] ^= entry_hash[i];
        }

        ++count;
        total_value_bytes += data.size();

        if (count % 10'000'000 == 0) {
            std::printf("  ... %zu entries processed\n", count);
        }
    });

    std::printf("\nEntries:      %zu\n", count);
    std::printf("Value bytes:  %zu (%.2f GB)\n", total_value_bytes, total_value_bytes / (1024.0 * 1024.0 * 1024.0));
    std::printf("Fingerprint:  %s\n", kth::encode_hash(fingerprint).c_str());

    return 0;
}
