// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_16_IPP
#define KTH_INFRASTUCTURE_BASE_16_IPP

#include <kth/infrastructure/utility/assert.hpp>

namespace kth {

// For template implementation only, do not call directly.
KI_API bool decode_base16_private(uint8_t* out, size_t out_size,
    char const* in);

template <size_t Size>
bool decode_base16(byte_array<Size>& out, std::string_view in)
{
    if (in.size() != 2 * Size) {
        return false;
}

    byte_array<Size> result;
    if ( ! decode_base16_private(result.data(), result.size(), in.data())) {
        return false;
}

    out = result;
    return true;
}

template <size_t Size>
byte_array<(Size - 1) / 2> base16_literal(char const (&string)[Size])
{
    byte_array<(Size - 1) / 2> out;
    DEBUG_ONLY(auto const success =) decode_base16_private(out.data(),
        out.size(), string);
    KTH_ASSERT(success);
    return out;
}

} // namespace kth

#endif
