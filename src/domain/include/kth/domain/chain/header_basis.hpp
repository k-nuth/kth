// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_HEADER_BASIS_HPP
#define KTH_DOMAIN_CHAIN_HEADER_BASIS_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <vector>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>


// #include <kth/domain/concepts.hpp>
#include <kth/domain/deserialization.hpp>

namespace kth::domain::chain {

struct KD_API header_basis {
    using list = std::vector<header_basis>;
    using ptr = std::shared_ptr<header_basis>;
    using const_ptr = std::shared_ptr<header_basis const>;
    using ptr_list = std::vector<header_basis>;
    using const_ptr_list = std::vector<const_ptr>;

    // Constructors.
    //-----------------------------------------------------------------------------

    header_basis() = default;
    header_basis(uint32_t version, hash_digest const& previous_block_hash, hash_digest const& merkle, uint32_t timestamp, uint32_t bits, uint32_t nonce);

    // Operators.
    //-----------------------------------------------------------------------------

    friend
    bool operator==(header_basis const& x, header_basis const& y);

    friend
    bool operator!=(header_basis const& x, header_basis const& y);

    // Deserialization.
    //-----------------------------------------------------------------------------

    static
    kth::expect<header_basis> from_data(byte_reader& reader, bool /*wire*/ = true);

    [[nodiscard]]
    bool is_valid() const;

    // Serialization.
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data(bool wire = true) const;

    void to_data(data_sink& stream, bool wire = true) const;

    template <typename W>
    void to_data(W& sink, bool /*wire = true*/) const {
        sink.write_4_bytes_little_endian(version_);
        sink.write_hash(previous_block_hash_);
        sink.write_hash(merkle_);
        sink.write_4_bytes_little_endian(timestamp_);
        sink.write_4_bytes_little_endian(bits_);
        sink.write_4_bytes_little_endian(nonce_);
    }

    // Properties (size, accessors).
    //-----------------------------------------------------------------------------
    static
    uint256_t proof(uint32_t bits);

    [[nodiscard]]
    uint256_t proof() const;

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
    constexpr
    size_t serialized_size(bool /*wire*/) const {
        return satoshi_fixed_size();
    }

    [[nodiscard]]
    uint32_t version() const;

    void set_version(uint32_t value);

    // [[deprecated]] // unsafe
    hash_digest& previous_block_hash();

    [[nodiscard]]
    hash_digest const& previous_block_hash() const;

    void set_previous_block_hash(hash_digest const& value);

    // [[deprecated]] // unsafe
    hash_digest& merkle();

    [[nodiscard]]
    hash_digest const& merkle() const;

    void set_merkle(hash_digest const& value);

    [[nodiscard]]
    uint32_t timestamp() const;

    void set_timestamp(uint32_t value);

    [[nodiscard]]
    uint32_t bits() const;

    void set_bits(uint32_t value);

    [[nodiscard]]
    uint32_t nonce() const;

    void set_nonce(uint32_t value);

    // Validation.
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    bool is_valid_timestamp() const;

    [[nodiscard]]
    bool is_valid_proof_of_work(hash_digest const& hash, bool retarget = true) const;

    [[nodiscard]]
    code check(hash_digest const& hash, bool retarget = false) const;

    [[nodiscard]]
    code accept(::kth::domain::chain::chain_state const& state, hash_digest const& hash) const;

    void reset();

private:
    uint32_t version_{0};
    hash_digest previous_block_hash_{null_hash};
    hash_digest merkle_{null_hash};
    uint32_t timestamp_{0};
    uint32_t bits_{0};
    uint32_t nonce_{0};
};

hash_digest hash(header_basis const& header);

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_HEADER_BASIS_HPP
