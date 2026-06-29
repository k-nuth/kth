// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_HEADER_RAW_HPP
#define KTH_DOMAIN_CHAIN_HEADER_RAW_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>

#include <kth/domain/deserialization.hpp>

namespace kth::domain::chain {

// Forward declaration
struct chain_state;

/// Immutable block header - stores raw 80 bytes as a contiguous byte array.
/// All field access is through getters that decode from the raw data.
/// See also: header_members.hpp for an alternative implementation with explicit member fields.
struct KD_API header {
    // Constants for field offsets within the 80-byte header.
    static constexpr size_t version_offset = 0;
    static constexpr size_t previous_block_hash_offset = 4;
    static constexpr size_t merkle_offset = 36;
    static constexpr size_t timestamp_offset = 68;
    static constexpr size_t bits_offset = 72;
    static constexpr size_t nonce_offset = 76;
    static constexpr size_t serialized_size_wire = 80;

    using list = std::vector<header>;
    using ptr = std::shared_ptr<header>;
    using const_ptr = std::shared_ptr<header const>;
    using ptr_list = std::vector<ptr>;
    using const_ptr_list = std::vector<const_ptr>;

private:
    // Raw 80-byte header data (little-endian as on wire).
    std::array<uint8_t, serialized_size_wire> data_{};

    // Validation metadata (not part of wire format, persisted when wire=false).
    // uint32_t median_time_past_{0};

    // Private helper to build data array from fields (used by constructor).
    static constexpr std::array<uint8_t, serialized_size_wire>
    make_data(uint32_t version, hash_digest const& previous_block_hash,
              hash_digest const& merkle, uint32_t timestamp,
              uint32_t bits, uint32_t nonce) {
        std::array<uint8_t, serialized_size_wire> data{};

        // Version (little-endian)
        auto const ver_bytes = to_little_endian(version);
        std::copy(ver_bytes.begin(), ver_bytes.end(), data.begin() + version_offset);

        // Previous block hash
        std::copy(previous_block_hash.begin(), previous_block_hash.end(),
                  data.begin() + previous_block_hash_offset);

        // Merkle root
        std::copy(merkle.begin(), merkle.end(), data.begin() + merkle_offset);

        // Timestamp (little-endian)
        auto const ts_bytes = to_little_endian(timestamp);
        std::copy(ts_bytes.begin(), ts_bytes.end(), data.begin() + timestamp_offset);

        // Bits (little-endian)
        auto const bits_bytes = to_little_endian(bits);
        std::copy(bits_bytes.begin(), bits_bytes.end(), data.begin() + bits_offset);

        // Nonce (little-endian)
        auto const nonce_bytes = to_little_endian(nonce);
        std::copy(nonce_bytes.begin(), nonce_bytes.end(), data.begin() + nonce_offset);

        return data;
    }

public:
    // Constructors.
    //-------------------------------------------------------------------------

    constexpr header() = default;

    // Construct from raw bytes (must be exactly 80 bytes).
    explicit constexpr header(std::span<uint8_t const, serialized_size_wire> raw_data)
        : data_{}
        // , median_time_past_{0}
    {
        std::copy(raw_data.begin(), raw_data.end(), data_.begin());
    }

    // Construct from raw byte array.
    explicit constexpr header(std::array<uint8_t, serialized_size_wire> const& raw_data)
        : data_{raw_data}
        // , median_time_past_{0}
    {}

    // // Construct from raw byte array with median_time_past.
    // constexpr header(std::array<uint8_t, serialized_size_wire> const& raw_data,
    //                  uint32_t mtp)
    //     : data_{raw_data}
    //     // , median_time_past_{mtp}
    // {}

    // Construct from individual fields.
    constexpr header(uint32_t version, hash_digest const& previous_block_hash,
                     hash_digest const& merkle, uint32_t timestamp,
                     uint32_t bits, uint32_t nonce)
        : data_{make_data(version, previous_block_hash, merkle, timestamp, bits, nonce)}
        // , median_time_past_{0}
    {}

    // // Construct from individual fields with median_time_past.
    // constexpr header(uint32_t version, hash_digest const& previous_block_hash,
    //                  hash_digest const& merkle, uint32_t timestamp,
    //                  uint32_t bits, uint32_t nonce, uint32_t mtp)
    //     : data_{make_data(version, previous_block_hash, merkle, timestamp, bits, nonce)}
    //     // , median_time_past_{mtp}
    // {}

    // Defaulted copy/move.
    constexpr header(header const&) = default;
    constexpr header(header&&) = default;
    constexpr header& operator=(header const&) = default;
    constexpr header& operator=(header&&) = default;

    // Comparison operators (C++20 defaulted).
    //-------------------------------------------------------------------------

    constexpr bool operator==(header const&) const = default;

    // Getters - return values (with endianness conversion).
    //-------------------------------------------------------------------------

    [[nodiscard]]
    constexpr uint32_t version() const {
        return from_little_endian<uint32_t>(version_span());
    }

    [[nodiscard]]
    constexpr hash_digest previous_block_hash() const {
        hash_digest result{};
        auto const span = previous_block_hash_span();
        std::copy(span.begin(), span.end(), result.begin());
        return result;
    }

    [[nodiscard]]
    constexpr hash_digest merkle() const {
        hash_digest result{};
        auto const span = merkle_span();
        std::copy(span.begin(), span.end(), result.begin());
        return result;
    }

    [[nodiscard]]
    constexpr uint32_t timestamp() const {
        return from_little_endian<uint32_t>(timestamp_span());
    }

    [[nodiscard]]
    constexpr uint32_t bits() const {
        return from_little_endian<uint32_t>(bits_span());
    }

    [[nodiscard]]
    constexpr uint32_t nonce() const {
        return from_little_endian<uint32_t>(nonce_span());
    }

    // [[nodiscard]]
    // constexpr uint32_t median_time_past() const {
    //     return median_time_past_;
    // }

    // Getters - return spans (zero-copy views into raw data).
    //-------------------------------------------------------------------------

    [[nodiscard]]
    constexpr std::span<uint8_t const, sizeof(uint32_t)> version_span() const {
        return std::span<uint8_t const, sizeof(uint32_t)>(
            data_.data() + version_offset, sizeof(uint32_t));
    }

    [[nodiscard]]
    constexpr std::span<uint8_t const, hash_size> previous_block_hash_span() const {
        return std::span<uint8_t const, hash_size>(
            data_.data() + previous_block_hash_offset, hash_size);
    }

    [[nodiscard]]
    constexpr std::span<uint8_t const, hash_size> merkle_span() const {
        return std::span<uint8_t const, hash_size>(
            data_.data() + merkle_offset, hash_size);
    }

    [[nodiscard]]
    constexpr std::span<uint8_t const, sizeof(uint32_t)> timestamp_span() const {
        return std::span<uint8_t const, sizeof(uint32_t)>(
            data_.data() + timestamp_offset, sizeof(uint32_t));
    }

    [[nodiscard]]
    constexpr std::span<uint8_t const, sizeof(uint32_t)> bits_span() const {
        return std::span<uint8_t const, sizeof(uint32_t)>(
            data_.data() + bits_offset, sizeof(uint32_t));
    }

    [[nodiscard]]
    constexpr std::span<uint8_t const, sizeof(uint32_t)> nonce_span() const {
        return std::span<uint8_t const, sizeof(uint32_t)>(
            data_.data() + nonce_offset, sizeof(uint32_t));
    }

    // Full raw data access.
    [[nodiscard]]
    constexpr std::span<uint8_t const, serialized_size_wire> raw_data() const {
        return std::span<uint8_t const, serialized_size_wire>(data_);
    }

    // Mutable raw data access (for direct deserialization).
    // Use with caution - allows modifying internal state.
    [[nodiscard]]
    constexpr std::array<uint8_t, serialized_size_wire>& raw_data_mutable() {
        return data_;
    }

    // Deserialization.
    //-------------------------------------------------------------------------

    static
    expect<header> from_data(byte_reader& reader, bool wire = true);

    [[nodiscard]]
    constexpr bool is_valid() const {
        // Check if any field is non-zero (header has been set).
        constexpr std::array<uint8_t, serialized_size_wire> zero_data{};
        return data_ != zero_data;
    }

    // Serialization.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data(bool wire = true) const;

    void to_data(data_sink& stream, bool wire = true) const;

    template <typename W>
    void to_data(W& sink, bool wire = true) const {
        // Write raw bytes directly - already in little-endian wire format.
        sink.write_bytes(data_.data(), serialized_size_wire);

        // if ( ! wire) {
        //     sink.write_4_bytes_little_endian(median_time_past_);
        // }
    }

    // Properties (size).
    //-------------------------------------------------------------------------

    static constexpr
    size_t satoshi_fixed_size() {
        return serialized_size_wire;
    }

    [[nodiscard]]
    constexpr size_t serialized_size(bool wire = true) const {
        // return serialized_size_wire + (wire ? 0 : sizeof(median_time_past_));
        return serialized_size_wire;
    }

    // Proof computation.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    static
    uint256_t proof(uint32_t bits);

    [[nodiscard]]
    uint256_t proof() const;

    // Validation.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    bool is_valid_timestamp() const;

    [[nodiscard]]
    bool is_valid_proof_of_work(hash_digest const& hash, bool retarget = true) const;

    [[nodiscard]]
    code check(hash_digest const& hash, bool retarget = false) const;

    [[nodiscard]]
    code accept(chain_state const& state, hash_digest const& hash) const;
};

// Free function for hash computation.
//-----------------------------------------------------------------------------

/// Compute the hash of a header (double SHA256 for Bitcoin, scrypt for Litecoin).
[[nodiscard]]
hash_digest hash(header const& h);

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_HEADER_RAW_HPP
