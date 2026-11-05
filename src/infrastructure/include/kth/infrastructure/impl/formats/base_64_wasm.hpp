// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_64_WASM_HPP
#define KTH_INFRASTRUCTURE_IMPL_FORMATS_BASE_64_WASM_HPP

#include <wasm_simd128.h>

namespace kth::detail {

// Clean full groups only. The shared scalar tail handles whitespace, padding
// and short buffers. Loads/stores stay within the supplied spans.
inline size_t base64_wasm_decode_prefix(std::string_view input, std::span<uint8_t> output) noexcept {
    size_t in = 0, out = 0;
    while (input.size() - in >= 16 && output.size() - out >= 12) {
        auto const c = wasm_v128_load(input.data() + in);
        auto const upper =
            wasm_v128_and(wasm_u8x16_ge(c, wasm_i8x16_splat('A')), wasm_u8x16_le(c, wasm_i8x16_splat('Z')));
        auto const lower =
            wasm_v128_and(wasm_u8x16_ge(c, wasm_i8x16_splat('a')), wasm_u8x16_le(c, wasm_i8x16_splat('z')));
        auto const digit =
            wasm_v128_and(wasm_u8x16_ge(c, wasm_i8x16_splat('0')), wasm_u8x16_le(c, wasm_i8x16_splat('9')));
        auto const plus = wasm_i8x16_eq(c, wasm_i8x16_splat('+'));
        auto const slash = wasm_i8x16_eq(c, wasm_i8x16_splat('/'));
        auto const valid = wasm_v128_or(wasm_v128_or(upper, lower), wasm_v128_or(digit, wasm_v128_or(plus, slash)));
        if (wasm_i8x16_bitmask(valid) != 0xffff)
            break;
        auto value = wasm_v128_and(wasm_i8x16_sub(c, wasm_i8x16_splat('A')), upper);
        value = wasm_v128_or(value, wasm_v128_and(wasm_i8x16_sub(c, wasm_i8x16_splat('a' - 26)), lower));
        value = wasm_v128_or(value, wasm_v128_and(wasm_i8x16_add(c, wasm_i8x16_splat(52 - '0')), digit));
        value = wasm_v128_or(value, wasm_v128_and(plus, wasm_i8x16_splat(62)));
        value = wasm_v128_or(value, wasm_v128_and(slash, wasm_i8x16_splat(63)));
        auto packed = wasm_v128_or(wasm_v128_and(wasm_i32x4_shl(value, 2), wasm_i32x4_splat(0xfc)),
            wasm_v128_and(wasm_u32x4_shr(value, 12), wasm_i32x4_splat(3)));
        packed = wasm_v128_or(packed, wasm_v128_and(wasm_i32x4_shl(value, 4), wasm_i32x4_splat(0xf000)));
        packed = wasm_v128_or(packed, wasm_v128_and(wasm_u32x4_shr(value, 10), wasm_i32x4_splat(0x0f00)));
        packed = wasm_v128_or(packed, wasm_v128_and(wasm_i32x4_shl(value, 6), wasm_i32x4_splat(0xc00000)));
        packed = wasm_v128_or(packed, wasm_v128_and(wasm_u32x4_shr(value, 8), wasm_i32x4_splat(0x3f0000)));
        packed = wasm_i8x16_swizzle(packed, wasm_i8x16_make(0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, -1, -1, -1, -1));
        wasm_v128_store64_lane(output.data() + out, packed, 0);
        wasm_v128_store32_lane(output.data() + out + 8, packed, 2);
        in += 16;
        out += 12;
    }
    return in;
}

inline size_t base64_wasm_encode_prefix(byte_span input, std::span<char> output) noexcept {
    size_t in = 0, out = 0;
    // A full 16-byte load is required although each iteration consumes 12.
    while (input.size() - in >= 16) {
        auto value = wasm_i8x16_swizzle(
            wasm_v128_load(input.data() + in), wasm_i8x16_make(2, 1, 0, -1, 5, 4, 3, -1, 8, 7, 6, -1, 11, 10, 9, -1));
        auto indices = wasm_v128_and(wasm_u32x4_shr(value, 18), wasm_i32x4_splat(0x3f));
        indices = wasm_v128_or(indices, wasm_v128_and(wasm_u32x4_shr(value, 4), wasm_i32x4_splat(0x3f00)));
        indices = wasm_v128_or(indices, wasm_v128_and(wasm_i32x4_shl(value, 10), wasm_i32x4_splat(0x3f0000)));
        indices = wasm_v128_or(indices, wasm_v128_and(wasm_i32x4_shl(value, 24), wasm_i32x4_splat(0x3f000000)));
        auto offset = wasm_v128_bitselect(
            wasm_i8x16_splat('a' - 26), wasm_i8x16_splat('A'), wasm_u8x16_gt(indices, wasm_i8x16_splat(25)));
        offset = wasm_v128_bitselect(wasm_i8x16_splat('0' - 52), offset, wasm_u8x16_gt(indices, wasm_i8x16_splat(51)));
        offset = wasm_v128_bitselect(wasm_i8x16_splat('+' - 62), offset, wasm_i8x16_eq(indices, wasm_i8x16_splat(62)));
        offset = wasm_v128_bitselect(wasm_i8x16_splat('/' - 63), offset, wasm_i8x16_eq(indices, wasm_i8x16_splat(63)));
        wasm_v128_store(output.data() + out, wasm_i8x16_add(indices, offset));
        in += 12;
        out += 16;
    }
    return in;
}

} // namespace kth::detail

#endif
