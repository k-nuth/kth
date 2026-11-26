// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/light_block.hpp>

#include <kth/domain/constants.hpp>
#include <kth/domain/constants/functions.hpp>
#include <kth/infrastructure/math/hash.hpp>


#include <kth/domain/chain/merkle.hpp>

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

    // Input count (varint).
    auto input_count = reader.read_size_little_endian();
    if (!input_count) {
        return std::unexpected(input_count.error());
    }
    if (*input_count > static_absolute_max_block_size()) {
        return std::unexpected(error::invalid_size);
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

        // Script and sequence skipped separately so script_len is never added
        // to another value (the sum could overflow past the bounds check).
        if (auto ec = reader.skip(*script_len); !ec) {
            return std::unexpected(ec.error());
        }
        if (auto ec = reader.skip(4); !ec) {
            return std::unexpected(ec.error());
        }
    }

    // Output count (varint)
    auto output_count = reader.read_size_little_endian();
    if (!output_count) {
        return std::unexpected(output_count.error());
    }
    if (*output_count > static_absolute_max_block_size()) {
        return std::unexpected(error::invalid_size);
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

    // 2. Read transaction count (varint). Cap before reserve() below.
    auto tx_count = reader.read_size_little_endian();
    if (!tx_count) {
        return std::unexpected(tx_count.error());
    }
    if (*tx_count > static_absolute_max_block_size()) {
        return std::unexpected(error::invalid_size);
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

    return light_block{std::move(*hdr), std::move(raw), std::move(offsets)};
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

code light_block::check_transaction_sizes() const {
#if defined(KTH_CURRENCY_BCH)
    for (size_t i = 0; i < tx_offsets_.size(); ++i) {
        if (tx_length(i) > max_tx_size) {
            return error::transaction_size_limit;
        }
    }
#endif
    return error::success;
}

hash_digest light_block::generate_merkle_root() const {
    bool mutated;
    return generate_merkle_root(mutated);
}

hash_digest light_block::generate_merkle_root(bool& mutated) const {
    mutated = false;
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

    // Step 2: Build the merkle tree over those hashes.
    return merkle_root(std::move(hashes), mutated);
}

} // namespace kth::domain::chain
