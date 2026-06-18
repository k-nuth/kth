// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/header_members.hpp>

#include <chrono>
#include <cstddef>
#include <cstring>

#include <boost/unordered/concurrent_flat_map.hpp>
#include <spdlog/spdlog.h>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/compact.hpp>
#include <kth/domain/constants.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::chain {

// Hash statistics tracking for header_members
namespace {
    boost::concurrent_flat_map<hash_digest, size_t> header_members_hash_call_counts;
    std::atomic<size_t> header_members_total_hash_calls{0};
    constexpr size_t header_members_log_interval = 10000;  // Log every N hash calls
} // namespace

namespace detail {

#pragma pack(push, 1)
struct header_members_packed {
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

// Deserialization.
//-----------------------------------------------------------------------------

expect<header> header::from_data(byte_reader& reader, bool /*wire*/) {
    auto const packed_exp = reader.read_packed<detail::header_members_packed>();
    if ( ! packed_exp) {
        return std::unexpected(packed_exp.error());
    }
    auto const& packed = *packed_exp;

    hash_digest previous_block_hash{};
    std::memcpy(previous_block_hash.data(), packed.previous_block_hash, hash_size);

    hash_digest merkle{};
    std::memcpy(merkle.data(), packed.merkle_root, hash_size);

    return header{
        packed.version,
        previous_block_hash,
        merkle,
        packed.timestamp,
        packed.bits,
        packed.nonce
    };
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

// Hash function.
//-----------------------------------------------------------------------------

hash_digest hash(header const& hdr) {
#if defined(KTH_CURRENCY_LTC)
    auto const result = litecoin_hash(hdr.to_data());
#else
    auto const result = bitcoin_hash(hdr.to_data());
#endif

    // Track hash call statistics
    header_members_hash_call_counts.emplace_or_visit(result, 1, [](auto& pair) { ++pair.second; });

    auto const current_total = ++header_members_total_hash_calls;
    if (current_total % header_members_log_interval == 0) {
        size_t unique_hashes = 0;
        size_t max_calls = 0;
        hash_digest most_called_hash{};

        header_members_hash_call_counts.visit_all([&](auto const& pair) {
            ++unique_hashes;
            if (pair.second > max_calls) {
                max_calls = pair.second;
                most_called_hash = pair.first;
            }
        });

        spdlog::info("[hash_stats_members] Total calls: {}, Unique hashes: {}, Duplicate calls: {}, Most called hash: {} ({} times)",
            current_total,
            unique_hashes,
            current_total - unique_hashes,
            encode_hash(most_called_hash),
            max_calls);
    }

    return result;
}

// Proof computation.
//-----------------------------------------------------------------------------

uint256_t header::proof(uint32_t bits) {
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

uint256_t header::proof() const {
    return proof(bits_);
}

// Validation helpers.
//-----------------------------------------------------------------------------

/// BUGBUG: bitcoin 32bit unix time: en.wikipedia.org/wiki/Year_2038_problem
bool header::is_valid_timestamp() const {
    static auto const two_hours = std::chrono::seconds(timestamp_future_seconds);
    auto const time = wall_clock::from_time_t(timestamp_);
    auto const future = wall_clock::now() + two_hours;
    return time <= future;
}

// [CheckProofOfWork]
bool header::is_valid_proof_of_work(hash_digest const& hash, bool retarget) const {
    compact const compact_bits(bits_);
    static uint256_t const pow_limit(compact{work_limit(retarget)});

    if (compact_bits.is_overflowed()) {
        return false;
    }

    uint256_t const& target = compact_bits.big();

    // Ensure claimed work is within limits.
    if (target < 1 || target > pow_limit) {
        return false;
    }

    // Ensure actual work is at least claimed amount (smaller is more work).
    return to_uint256(hash) <= target;
}

// Validation.
//-----------------------------------------------------------------------------

code header::check(hash_digest const& hash, bool retarget) const {
    if ( ! is_valid_proof_of_work(hash, retarget)) {
        return error::invalid_proof_of_work;
    }

    if ( ! is_valid_timestamp()) {
        return error::futuristic_timestamp;
    }

    return error::success;
}

code header::accept(chain_state const& state, hash_digest const& hash) const {
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
