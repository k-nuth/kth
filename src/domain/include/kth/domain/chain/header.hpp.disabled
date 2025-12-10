// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_HEADER_HPP
#define KTH_DOMAIN_CHAIN_HEADER_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <vector>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/hash_memoizer.hpp>
#include <kth/domain/chain/header_basis.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>
namespace kth::domain::chain {
struct KD_API header : header_basis, hash_memoizer<header> {                                 // NOLINT(cppcoreguidelines-special-member-functions)
public:
    using list = std::vector<header>;
    using ptr = std::shared_ptr<header>;
    using const_ptr = std::shared_ptr<header const>;
    using ptr_list = std::vector<header>;
    using const_ptr_list = std::vector<const_ptr>;

    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    struct validation_t {
        size_t height = 0;
        uint32_t median_time_past = 0;
    };

    // Constructors.
    //-----------------------------------------------------------------------------
    using header_basis::header_basis; // inherit constructors from header_basis

    header() = default;

    explicit
    header(header_basis const& basis)
        : header_basis(basis)
    {}

    /// This class is copy constructible and copy assignable.
    // Note(kth): Cannot be defaulted because the std::mutex data member.
    header(header const& x);
    header& operator=(header const& x);


    // Deserialization.
    //-----------------------------------------------------------------------------


    static
    expect<header> from_data(byte_reader& reader, bool wire = true);

    // Serialization.
    //-----------------------------------------------------------------------------

    data_chunk to_data(bool wire = true) const;
    // void to_data(data_sink& stream, bool wire=true) const;
    void to_data(data_sink& stream, bool wire = true) const;

    template <typename W>
    void to_data(W& sink, bool wire = true) const {
        header_basis::to_data(sink, wire);

        if ( ! wire) {
            sink.write_4_bytes_little_endian(validation.median_time_past);
        }
    }

    // Properties (size, accessors, cache).
    //-----------------------------------------------------------------------------

    size_t serialized_size(bool wire = true) const;

    void set_version(uint32_t value);
    void set_previous_block_hash(hash_digest const& value);
    void set_merkle(hash_digest const& value);
    void set_timestamp(uint32_t value);
    void set_bits(uint32_t value);
    void set_nonce(uint32_t value);

    // hash_digest hash() const;
    hash_digest hash_pow() const;

#if defined(KTH_CURRENCY_LTC)
    hash_digest litecoin_proof_of_work_hash() const;
#endif  //KTH_CURRENCY_LTC

    // Validation.
    //-----------------------------------------------------------------------------

    bool is_valid_proof_of_work(bool retarget = true) const;

    code check(bool retarget = false) const;
    code accept(chain_state const& state) const;


    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    mutable validation_t validation{};

// protected:
    // So that block may call reset from its own.
    // friend class block;

    void reset();
    // void invalidate_cache() const;

// private:
//     mutable upgrade_mutex mutex_;
//     mutable std::shared_ptr<hash_digest> hash_;
};

} // namespace kth::domain::chain

#endif
