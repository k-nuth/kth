// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_HEADER_HPP
#define KTH_DOMAIN_CHAIN_HEADER_HPP

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <ranges>

#include <kth/infrastructure/math/hash.hpp>

// Header implementation selection.
// Define KTH_USE_HEADER_MEMBERS to use member-based implementation.
// Default is raw byte array implementation.

#if defined(KTH_USE_HEADER_MEMBERS)
    #include <kth/domain/chain/header_members.hpp>
#else
    #include <kth/domain/chain/header_raw.hpp>
#endif

namespace kth::domain::chain {

// =============================================================================
// Header Concepts
// =============================================================================

/// Concept for types that have a timestamp() member function.
template <typename T>
concept Timestamped = requires(T const& t) {
    { t.timestamp() } -> std::convertible_to<uint32_t>;
};

/// Concept for types that have all standard header field accessors.
template <typename T>
concept HeaderLike = requires(T const& t) {
    { t.version() } -> std::convertible_to<uint32_t>;
    { t.previous_block_hash() } -> std::convertible_to<hash_digest>;
    { t.merkle() } -> std::convertible_to<hash_digest>;
    { t.timestamp() } -> std::convertible_to<uint32_t>;
    { t.bits() } -> std::convertible_to<uint32_t>;
    { t.nonce() } -> std::convertible_to<uint32_t>;
};

/// Concept for types that can be hashed via hash() free function.
template <typename T>
concept Hashable = requires(T const& t) {
    { hash(t) } -> std::convertible_to<hash_digest>;
};

/// Concept for a range of Timestamped elements (for MTP calculation).
template <typename R>
concept TimestampedRange = std::ranges::range<R> &&
    Timestamped<std::ranges::range_value_t<R>>;

/// Concept for a range of HeaderLike elements.
template <typename R>
concept HeaderRange = std::ranges::range<R> &&
    HeaderLike<std::ranges::range_value_t<R>>;

// =============================================================================
// Median Time Past (MTP) Calculation
// =============================================================================

/// Number of blocks used for MTP calculation (Bitcoin consensus rule).
inline constexpr size_t median_time_past_blocks = 11;

/// Calculate median time past from a range of timestamped elements.
/// The range should contain the previous blocks in reverse chronological order
/// (most recent first), up to median_time_past_blocks elements.
/// Returns the median timestamp of the provided elements.
template <TimestampedRange R>
[[nodiscard]] constexpr
uint32_t median_time_past(R&& range) {
    std::array<uint32_t, median_time_past_blocks> timestamps{};
    size_t count = 0;

    for (auto const& elem : range) {
        if (count >= median_time_past_blocks) break;
        timestamps[count++] = elem.timestamp();
    }

    if (count == 0) {
        return 0;
    }

    // Sort only the elements we collected
    std::sort(timestamps.begin(), timestamps.begin() + count);

    // Return the median (middle element for odd count, lower-middle for even)
    return timestamps[count / 2];
}

/// Calculate median time past for a specific index in a range.
/// Takes the range and the index of the block for which to calculate MTP.
/// Uses up to median_time_past_blocks elements before the given index.
template <std::ranges::random_access_range R>
    requires Timestamped<std::ranges::range_value_t<R>>
[[nodiscard]] constexpr
uint32_t median_time_past_at(R&& range, size_t index) {
    std::array<uint32_t, median_time_past_blocks> timestamps{};
    size_t count = 0;

    // Collect timestamps from blocks before index (not including index)
    // Go backwards from index-1
    for (size_t i = 0; i < median_time_past_blocks && i < index; ++i) {
        timestamps[count++] = range[index - 1 - i].timestamp();
    }

    if (count == 0) {
        return 0;
    }

    std::sort(timestamps.begin(), timestamps.begin() + count);
    return timestamps[count / 2];
}

// Static assertion to verify the selected implementation satisfies the concepts
static_assert(Timestamped<header>, "header must be Timestamped");
static_assert(HeaderLike<header>, "header must be HeaderLike");

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_HEADER_HPP
