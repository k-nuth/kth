// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/header_basis.hpp>

#include <chrono>
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

namespace detail {

#pragma pack(push, 1)
struct header_packed {
    uint32_t version;
    byte previous_block_hash[32];
    byte merkle_root[32];
    uint32_t timestamp;
    uint32_t bits;
    uint32_t nonce;
};
#pragma pack(pop)

} // namespace detail

// Use system clock because we require accurate time of day.
using wall_clock = std::chrono::system_clock;

// Constructors.
//-----------------------------------------------------------------------------

header_basis::header_basis(uint32_t version, hash_digest const& previous_block_hash, hash_digest const& merkle, uint32_t timestamp, uint32_t bits, uint32_t nonce)
    : version_(version), previous_block_hash_(previous_block_hash), merkle_(merkle), timestamp_(timestamp), bits_(bits), nonce_(nonce)
{}

// Operators.
//-----------------------------------------------------------------------------

// friend
bool operator==(header_basis const& x, header_basis const& y) {
    return (x.version_ == y.version_) && (x.previous_block_hash_ == y.previous_block_hash_) &&
           (x.merkle_ == y.merkle_) && (x.timestamp_ == y.timestamp_) && (x.bits_ == y.bits_) &&
           (x.nonce_ == y.nonce_);
}

//friend
bool operator!=(header_basis const& x, header_basis const& y) {
    return !(x == y);
}

// protected
void header_basis::reset() {
    version_ = 0;
    previous_block_hash_.fill(0);
    merkle_.fill(0);
    timestamp_ = 0;
    bits_ = 0;
    nonce_ = 0;
}

bool header_basis::is_valid() const {
    return (version_ != 0) ||
           (previous_block_hash_ != null_hash) ||
           (merkle_ != null_hash) ||
           (timestamp_ != 0) ||
           (bits_ != 0) ||
           (nonce_ != 0);
}

// Serialization.
//-----------------------------------------------------------------------------

expect<header_basis> header_basis::from_data(byte_reader& reader, bool /*wire*/) {
    auto const packed_exp = reader.read_packed<detail::header_packed>();
    if ( ! packed_exp) {
        return std::unexpected(packed_exp.error());
    }
    auto const& packed = *packed_exp;
    header_basis header;
    header.set_version(packed.version);
    std::memcpy(header.previous_block_hash_.data(), packed.previous_block_hash, hash_size);
    std::memcpy(header.merkle_.data(), packed.merkle_root, hash_size);
    header.set_timestamp(packed.timestamp);
    header.set_bits(packed.bits);
    header.set_nonce(packed.nonce);
    return header;
}

data_chunk header_basis::to_data(bool wire) const {
    data_chunk data;
    auto const size = serialized_size(wire);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream, wire);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void header_basis::to_data(data_sink& stream, bool wire) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, wire);
}

// Accessors.
//-----------------------------------------------------------------------------

uint32_t header_basis::version() const {
    return version_;
}

void header_basis::set_version(uint32_t value) {
    version_ = value;
}

hash_digest& header_basis::previous_block_hash() {
    return previous_block_hash_;
}

hash_digest const& header_basis::previous_block_hash() const {
    return previous_block_hash_;
}

void header_basis::set_previous_block_hash(hash_digest const& value) {
    previous_block_hash_ = value;
}

hash_digest& header_basis::merkle() {
    return merkle_;
}

hash_digest const& header_basis::merkle() const {
    return merkle_;
}

void header_basis::set_merkle(hash_digest const& value) {
    merkle_ = value;
}

uint32_t header_basis::timestamp() const {
    return timestamp_;
}

void header_basis::set_timestamp(uint32_t value) {
    timestamp_ = value;
}

uint32_t header_basis::bits() const {
    return bits_;
}

void header_basis::set_bits(uint32_t value) {
    bits_ = value;
}

uint32_t header_basis::nonce() const {
    return nonce_;
}

void header_basis::set_nonce(uint32_t value) {
    nonce_ = value;
}

// Cache.
//-----------------------------------------------------------------------------

hash_digest hash(header_basis const& header) {
    return bitcoin_hash(header.to_data());
}

#if defined(KTH_CURRENCY_LTC)
hash_digest litecoin_proof_of_work_hash(header_basis const& header) {
    return litecoin_hash(header.to_data());
}
#endif  //KTH_CURRENCY_LTC

uint256_t header_basis::proof(uint32_t bits) {
    compact const header_bits(bits);

    if (header_bits.is_overflowed()) {
        return 0;
    }

    uint256_t const& target = header_bits.big();

    //*************************************************************************
    // CONSENSUS: satoshi will throw division by zero in the case where the
    // target is (2^256)-1 as the overflow will result in a zero divisor.
    // While actually achieving this work is improbable, this method operates
    // on user data method and therefore must be guarded.
    //*************************************************************************
    auto const divisor = target + 1;

    // We need to compute 2**256 / (target + 1), but we can't represent 2**256
    // as it's too large for uint256. However as 2**256 is at least as large as
    // target + 1, it is equal to ((2**256 - target - 1) / (target + 1)) + 1, or
    // (~target / (target + 1)) + 1.
    return (divisor == 0) ? 0 : (~target / divisor) + 1;
}

uint256_t header_basis::proof() const {
    return proof(bits());
}

// Validation helpers.
//-----------------------------------------------------------------------------

/// BUGBUG: bitcoin 32bit unix time: en.wikipedia.org/wiki/Year_2038_problem
bool header_basis::is_valid_timestamp() const {
    static auto const two_hours = std::chrono::seconds(timestamp_future_seconds);
    auto const time = wall_clock::from_time_t(timestamp_);
    auto const future = wall_clock::now() + two_hours;
    return time <= future;
}

// [CheckProofOfWork]
bool header_basis::is_valid_proof_of_work(hash_digest const& hash, bool retarget) const {
    compact const bits(bits_);
    static uint256_t const pow_limit(compact{work_limit(retarget)});

    if (bits.is_overflowed()) {
        return false;
    }

    // uint256_t target(bits);
    uint256_t const& target = bits.big();

    // Ensure claimed work is within limits.
    if (target < 1 || target > pow_limit) {
        return false;
    }

    // Ensure actual work is at least claimed amount (smaller is more work).
    return to_uint256(hash) <= target;
}

// bool header_basis::is_valid_proof_of_work(bool retarget) const {

// #if defined(KTH_CURRENCY_LTC)
//     return to_uint256(litecoin_proof_of_work_hash(*this)) <= target;
// #else
//     return to_uint256(hash(*this)) <= target;
// #endif

// }

// Validation.
//-----------------------------------------------------------------------------

code header_basis::check(hash_digest const& hash, bool retarget) const {
    if ( ! is_valid_proof_of_work(hash, retarget)) {
        return error::invalid_proof_of_work;
    }

    if ( ! is_valid_timestamp()) {
        return error::futuristic_timestamp;
    }

    return error::success;
}

code header_basis::accept(chain_state const& state, hash_digest const& hash) const {
    if (bits_ != state.work_required()) {
        return error::incorrect_proof_of_work;
    }

    if (state.is_checkpoint_conflict(hash)) {
        return error::checkpoints_failed;
    }

    if (state.is_under_checkpoint()) {
        return error::success;
    }

    if (version_ < state.minimum_version()) {
        return error::old_version_block;
    }

    if (timestamp_ <= state.median_time_past()) {
        return error::timestamp_too_early;
    }

    return error::success;
}

} // namespace kth::domain::chain
