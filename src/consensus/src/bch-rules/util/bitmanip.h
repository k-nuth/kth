// Copyright (c) 2019-2025 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>  //TODO(fernando): implement this
#endif

#include <span.h>

#include <cstddef>
#include <cstdint>

inline uint32_t countBits(uint32_t v) {
#if HAVE_DECL___BUILTIN_POPCOUNT
    return __builtin_popcount(v);
#else
    /**
     * Computes the number of bits set in each group of 8bits then uses a
     * multiplication to sum all of them in the 8 most significant bits and
     * return these.
     * More detailed explanation can be found at
     * https://www.playingwithpointers.com/blog/swar.html
     */
    v = v - ((v >> 1) & 0x55555555);
    v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
    return (((v + (v >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24;
#endif
}


/**
 * @brief bitShiftBlob - Bit-shift a binary byte blob left or right, as if it were 1 large (unsigned) machine word.
 * @param span - The byte blob to bit-shift in-place
 * @param nbits - The number of bits to shift left/right.
 * @param rshift - If true, shift right, if false, shift left.
 * @pre `span` should be <= std::numeric_limits<size_t>::max() / 8 in size.
 * @post `span` is bit-shifted in-place as if it were a giant span.size() * 8 bit machine word, with 0 bits shifted into
 * the right-most/left-most end. If `nbits >= span.size() * 8`, then the span is completely cleared with 0's (as a
 * "fast-path" early return).
 * @exception std::out_of_range - If `span.size() > std::numeric_limits<size_t>::max() / 8`.
 */
void bitShiftBlob(Span<std::byte> const &span, size_t nbits, bool rshift);

// Convenience overload of above using uint8_t instead of std::byte
inline void bitShiftBlob(Span<uint8_t> const &span, size_t const nbits, bool const rshift) {
    bitShiftBlob(Span<std::byte>{reinterpret_cast<std::byte *>(span.data()), span.size()},  nbits, rshift);
}
