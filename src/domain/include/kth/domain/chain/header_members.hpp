// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_HEADER_MEMBERS_HPP
#define KTH_DOMAIN_CHAIN_HEADER_MEMBERS_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>

#include <kth/domain/deserialization.hpp>

namespace kth::domain::chain {

// Forward declaration
struct chain_state;

/// Block header - stores fields as separate members.
/// Hash is computed on demand via free function hash(header).
/// See also: header_raw.hpp for an alternative implementation with raw byte storage.
struct KD_API header {
    using list = std::vector<header>;
    using ptr = std::shared_ptr<header>;
    using const_ptr = std::shared_ptr<header const>;
    using ptr_list = std::vector<ptr>;
    using const_ptr_list = std::vector<const_ptr>;

private:
    uint32_t version_{0};
    hash_digest previous_block_hash_{null_hash};
    hash_digest merkle_{null_hash};
    uint32_t timestamp_{0};
    uint32_t bits_{0};
    uint32_t nonce_{0};

public:
    // Constructors.
    //-------------------------------------------------------------------------

    constexpr header() = default;

    constexpr header(uint32_t version, hash_digest const& previous_block_hash,
                     hash_digest const& merkle, uint32_t timestamp,
                     uint32_t bits, uint32_t nonce)
        : version_(version)
        , previous_block_hash_(previous_block_hash)
        , merkle_(merkle)
        , timestamp_(timestamp)
        , bits_(bits)
        , nonce_(nonce)
    {}

    // Defaulted copy/move (implicitly constexpr in C++20+)
    constexpr header(header const&) = default;
    constexpr header(header&&) = default;
    constexpr header& operator=(header const&) = default;
    constexpr header& operator=(header&&) = default;

    // Comparison operators (C++20 defaulted).
    //-------------------------------------------------------------------------

    constexpr bool operator==(header const&) const = default;

    // Getters.
    //-------------------------------------------------------------------------

    [[nodiscard]] constexpr uint32_t version() const { return version_; }
    [[nodiscard]] constexpr hash_digest const& previous_block_hash() const { return previous_block_hash_; }
    [[nodiscard]] constexpr hash_digest const& merkle() const { return merkle_; }
    [[nodiscard]] constexpr uint32_t timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr uint32_t bits() const { return bits_; }
    [[nodiscard]] constexpr uint32_t nonce() const { return nonce_; }

    // Deserialization.
    //-------------------------------------------------------------------------

    static
    expect<header> from_data(byte_reader& reader, bool wire = true);

    [[nodiscard]]
    constexpr bool is_valid() const {
        return (version_ != 0) ||
               (previous_block_hash_ != null_hash) ||
               (merkle_ != null_hash) ||
               (timestamp_ != 0) ||
               (bits_ != 0) ||
               (nonce_ != 0);
    }

    // Serialization.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data(bool wire = true) const;

    void to_data(data_sink& stream, bool wire = true) const;

    template <typename W>
    void to_data(W& sink, bool wire = true) const {
        sink.write_4_bytes_little_endian(version_);
        sink.write_hash(previous_block_hash_);
        sink.write_hash(merkle_);
        sink.write_4_bytes_little_endian(timestamp_);
        sink.write_4_bytes_little_endian(bits_);
        sink.write_4_bytes_little_endian(nonce_);
    }

    // Properties (size).
    //-------------------------------------------------------------------------

    static constexpr
    size_t satoshi_fixed_size() {
        return sizeof(version_) +
            hash_size +
            hash_size +
            sizeof(timestamp_) +
            sizeof(bits_) +
            sizeof(nonce_);
    }

    [[nodiscard]]
    constexpr size_t serialized_size(bool /*wire*/ = true) const {
        return satoshi_fixed_size();
    }

    // Proof computation.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    static uint256_t proof(uint32_t bits);

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

#endif // KTH_DOMAIN_CHAIN_HEADER_MEMBERS_HPP
