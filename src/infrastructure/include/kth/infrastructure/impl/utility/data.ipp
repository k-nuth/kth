// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_DATA_IPP
#define KTH_INFRASTRUCTURE_DATA_IPP

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>

#include <kth/infrastructure/utility/assert.hpp>

namespace kth {

inline
one_byte to_array(uint8_t byte) {
    return byte_array<1>{ { byte } };
}

inline
data_chunk to_chunk(uint8_t byte) {
    return data_chunk{ byte };
}

inline
data_chunk build_chunk(loaf spans, size_t extra_reserve) {
    size_t size = 0;
    for (auto const span : spans) {
        size += span.size();
    }

    data_chunk out;
    out.reserve(size + extra_reserve);
    for (auto const span : spans) {
        out.insert(out.end(), span.begin(), span.end());
    }

    return out;
}

template <size_t Size>
bool build_array(byte_array<Size>& out, loaf spans) {
    size_t size = 0;
    for (auto const span : spans) {
        size += span.size();
    }

    if (size > Size) {
        return false;
    }

    auto position = out.begin();
    for (auto const span : spans) {
        std::copy(span.begin(), span.end(), position);
        position += span.size();
    }

    return true;
}

template <typename Target, typename Extension>
void extend_data(Target& bytes, const Extension& x) {
    bytes.insert(std::end(bytes), std::begin(x), std::end(x));
}

// std::array<> is used in place of byte_array<> to enable Size deduction.
template <size_t Start, size_t End, size_t Size>
byte_array<End - Start> slice(std::array<uint8_t, Size> const& bytes) {
    static_assert(End <= Size, "Slice end must not exceed array size.");
    byte_array<End - Start> out;
    std::copy(std::begin(bytes) + Start, std::begin(bytes) + End, out.begin());
    return out;
}

template <size_t Left, size_t Right>
byte_array<Left + Right> splice(std::array<uint8_t, Left> const& left, std::array<uint8_t, Right> const& right) {
    byte_array<Left + Right> out;
    /* safe to ignore */ build_array<Left + Right>(out, { left, right });
    return out;
}

template <size_t Left, size_t Middle, size_t Right>
byte_array<Left + Middle + Right> splice(std::array<uint8_t, Left> const& left,
    std::array<uint8_t, Middle> const& middle,
    std::array<uint8_t, Right> const& right) {
    byte_array<Left + Middle + Right> out;
    /* safe to ignore */ build_array(out, { left, middle, right });
    return out;
}

template <size_t Size>
byte_array_parts<Size / 2> split(byte_array<Size> const& bytes) {
    static_assert(Size != 0, "Split requires a non-zero parameter.");
    static_assert(Size % 2 == 0, "Split requires an even length parameter.");
    static size_t const half = Size / 2;
    byte_array_parts<half> out;
    std::copy_n(std::begin(bytes), half, out.left.begin());
    std::copy_n(std::begin(bytes) + half, half, out.right.begin());
    return out;
}

// unsafe
template <size_t Size>
byte_array<Size> to_array(byte_span bytes) {
    byte_array<Size> out;
    DEBUG_ONLY(auto const result =) build_array(out, { bytes });
    KTH_ASSERT(result);
    return out;
}

template <typename Source>
data_chunk to_chunk(const Source& bytes) {
    return data_chunk(std::begin(bytes), std::end(bytes));
}

template <typename Source>
bool starts_with(const typename Source::const_iterator& begin,
    const typename Source::const_iterator& end, const Source& value) {
    auto const length = std::distance(begin, end);
    return length >= 0 && size_t(length) >= value.size() &&
        std::equal(value.begin(), value.end(), begin);
}

// unsafe
template <size_t Size>
byte_array<Size> xor_data(byte_span bytes1, byte_span bytes2) {
    return xor_data<Size>(bytes1, bytes2, 0);
}

// unsafe
template <size_t Size>
byte_array<Size> xor_data(byte_span bytes1, byte_span bytes2, size_t offset) {
    return xor_data<Size>(bytes1, bytes2, offset, offset);
}

// unsafe
template <size_t Size>
byte_array<Size> xor_data(byte_span bytes1, byte_span bytes2, size_t offset1, size_t offset2) {
    KTH_ASSERT(offset1 + Size <= bytes1.size());
    KTH_ASSERT(offset2 + Size <= bytes2.size());
    auto const& data1 = bytes1.data();
    auto const& data2 = bytes2.data();

    byte_array<Size> out;
    for (size_t i = 0; i < Size; i++) {
        out[i] = data1[i + offset1] ^ data2[i + offset2];
    }
    return out;
}

} // namespace kth

#endif
