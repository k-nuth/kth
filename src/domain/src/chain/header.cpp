// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/header.hpp>

#include <cstddef>
#include <utility>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/compact.hpp>
#include <kth/domain/constants.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::chain {

// Constructors.
//-----------------------------------------------------------------------------

header::header(header const& x)
    : header_basis(x)
    , validation(x.validation)
{}

header& header::operator=(header const& x) {
    header_basis::operator=(x);
    validation = x.validation;
    return *this;
}

// protected
void header::reset() {
    header_basis::reset();
    invalidate();
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<header> header::from_data(byte_reader& reader, bool wire) {
    auto const basis = header_basis::from_data(reader, wire);
    if ( ! basis) {
        return std::unexpected(basis.error());
    }
    header hdr {*basis};

    if ( ! wire) {
        auto const mtp = reader.read_little_endian<uint32_t>();
        if ( ! mtp) {
            return std::unexpected(mtp.error());
        }
        hdr.validation.median_time_past = *mtp;
    }

    return hdr;
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk header::to_data(bool wire) const {
    data_chunk data;
    auto const size = serialized_size(wire);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream, wire);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void header::to_data(data_sink& stream, bool wire) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, wire);
}

// Size.
//-----------------------------------------------------------------------------

size_t header::serialized_size(bool wire) const {
    return satoshi_fixed_size() + (wire ? 0 : sizeof(uint32_t));
}

// Accessors.
//-----------------------------------------------------------------------------

void header::set_version(uint32_t value) {
    header_basis::set_version(value);
    invalidate();
}

void header::set_previous_block_hash(hash_digest const& value) {
    header_basis::set_previous_block_hash(value);
    invalidate();
}

void header::set_merkle(hash_digest const& value) {
    header_basis::set_merkle(value);
    invalidate();
}

void header::set_timestamp(uint32_t value) {
    header_basis::set_timestamp(value);
    invalidate();
}

void header::set_bits(uint32_t value) {
    header_basis::set_bits(value);
    invalidate();
}

void header::set_nonce(uint32_t value) {
    header_basis::set_nonce(value);
    invalidate();
}

#if defined(KTH_CURRENCY_LTC)
hash_digest header::litecoin_proof_of_work_hash() const {
    return litecoin_hash(to_data());
}
#endif  //KTH_CURRENCY_LTC

inline
hash_digest header::hash_pow() const {
#if defined(KTH_CURRENCY_LTC)
    return litecoin_proof_of_work_hash();
#else
    return hash();
#endif

}

// Validation helpers.
//-----------------------------------------------------------------------------

// [CheckProofOfWork]
bool header::is_valid_proof_of_work(bool retarget) const {
    return header_basis::is_valid_proof_of_work(hash_pow(), retarget);
}

// Validation.
//-----------------------------------------------------------------------------

code header::check(bool retarget) const {
    return header_basis::check(hash_pow(), retarget);
}

code header::accept(chain_state const& state) const {
    return header_basis::accept(state, hash_pow());
}

} // namespace kth::domain::chain
