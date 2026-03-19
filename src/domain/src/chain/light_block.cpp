// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/light_block.hpp>

#include <kth/infrastructure/math/hash.hpp>

// Bitcoin Core optimized SHA256D64 for merkle tree computation
#include <crypto/sha256.h>

namespace kth::domain::chain {

namespace {

// Skip a single transaction in the reader.
// Returns the number of bytes skipped.
expect<uint32_t> skip_transaction(byte_reader& reader) {
    size_t const tx_start = reader.position();

    // Version: 4 bytes
    if (auto ec = reader.skip(4); !ec) {
        return std::unexpected(ec.error());
    }

    // Input count (varint)
    auto input_count = reader.read_size_little_endian();
    if (!input_count) {
        return std::unexpected(input_count.error());
    }

    // Skip all inputs
    for (size_t i = 0; i < *input_count; ++i) {
        // Previous output hash (32) + index (4)
        if (auto ec = reader.skip(36); !ec) {
            return std::unexpected(ec.error());
        }

        // Script length (varint)
        auto script_len = reader.read_size_little_endian();
        if (!script_len) {
            return std::unexpected(script_len.error());
        }

        // Script + sequence (4 bytes)
        if (auto ec = reader.skip(*script_len + 4); !ec) {
            return std::unexpected(ec.error());
        }
    }

    // Output count (varint)
    auto output_count = reader.read_size_little_endian();
    if (!output_count) {
        return std::unexpected(output_count.error());
    }

    // Skip all outputs
    for (size_t i = 0; i < *output_count; ++i) {
        // Value: 8 bytes
        if (auto ec = reader.skip(8); !ec) {
            return std::unexpected(ec.error());
        }

        // Script length (varint)
        auto script_len = reader.read_size_little_endian();
        if (!script_len) {
            return std::unexpected(script_len.error());
        }

        // Script
        if (auto ec = reader.skip(*script_len); !ec) {
            return std::unexpected(ec.error());
        }
    }

    // Locktime: 4 bytes
    if (auto ec = reader.skip(4); !ec) {
        return std::unexpected(ec.error());
    }

    size_t const tx_end = reader.position();
    return uint32_t(tx_end - tx_start);
}

} // anonymous namespace

// static
expect<light_block> light_block::from_data(byte_reader& reader, bool wire) {
    // Remember start position for raw data capture
    size_t const start_pos = reader.position();

    // 1. Parse header (80 bytes) - this is the only full parse
    auto hdr = header::from_data(reader, wire);
    if (!hdr) {
        return std::unexpected(hdr.error());
    }

    // 2. Read transaction count (varint)
    auto tx_count = reader.read_size_little_endian();
    if (!tx_count) {
        return std::unexpected(tx_count.error());
    }

    // 3. For each transaction: record start offset and skip
    std::vector<uint32_t> offsets;
    offsets.reserve(*tx_count);

    for (size_t i = 0; i < *tx_count; ++i) {
        offsets.push_back(uint32_t(reader.position() - start_pos));

        // Skip transaction
        auto length = skip_transaction(reader);
        if (!length) {
            return std::unexpected(length.error());
        }
    }

    // 4. Copy raw data from reader's buffer (for disk storage)
    auto const raw_span = reader.buffer().subspan(start_pos, reader.position() - start_pos);
    data_chunk raw(raw_span.begin(), raw_span.end());

    light_block result;
    result.header_ = std::move(*hdr);
    result.raw_data_ = std::move(raw);
    result.tx_offsets_ = std::move(offsets);
    return result;
}

uint32_t light_block::tx_length(size_t index) const {
    if (index + 1 < tx_offsets_.size()) {
        return tx_offsets_[index + 1] - tx_offsets_[index];
    }
    return uint32_t(raw_data_.size()) - tx_offsets_[index];
}

byte_span light_block::tx_bytes(size_t index) const {
    return byte_span(raw_data_.data() + tx_offsets_[index], tx_length(index));
}

hash_digest light_block::generate_merkle_root() const {
    if (tx_offsets_.empty()) {
        return null_hash;
    }

    // Step 1: Hash each transaction's raw bytes to get leaf hashes
    hash_list hashes;
    hashes.reserve(tx_offsets_.size());

    for (size_t i = 0; i < tx_offsets_.size(); ++i) {
        auto const tx_data = tx_bytes(i);
        // bitcoin_hash = SHA256(SHA256(data))
        hashes.push_back(bitcoin_hash(tx_data));
    }

    // Step 2: Build merkle tree using Bitcoin Core's optimized SHA256D64
    // SHA256D64 uses SIMD: 2-way on ARM, 4-way on SSE4.1, 8-way on AVX2
    std::vector<uint8_t> batch_input;
    std::vector<uint8_t> batch_output;

    while (hashes.size() > 1) {
        // Bitcoin merkle: duplicate last hash if odd count at this level
        if (hashes.size() % 2 != 0) {
            hashes.push_back(hashes.back());
        }

        size_t const num_pairs = hashes.size() / 2;

        // Prepare batch input: all pairs concatenated (64 bytes each)
        batch_input.resize(num_pairs * 64);
        batch_output.resize(num_pairs * 32);

        for (size_t i = 0; i < num_pairs; ++i) {
            std::copy(hashes[i * 2].begin(), hashes[i * 2].end(),
                      batch_input.begin() + i * 64);
            std::copy(hashes[i * 2 + 1].begin(), hashes[i * 2 + 1].end(),
                      batch_input.begin() + i * 64 + 32);
        }

        // Process all pairs at once using Bitcoin Core's optimized SHA256D64
        SHA256D64(batch_output.data(), batch_input.data(), num_pairs);

        // Copy results back to hashes vector
        hashes.resize(num_pairs);
        for (size_t i = 0; i < num_pairs; ++i) {
            std::copy(batch_output.begin() + i * 32,
                      batch_output.begin() + (i + 1) * 32,
                      hashes[i].begin());
        }
    }

    return hashes.front();
}

} // namespace kth::domain::chain
