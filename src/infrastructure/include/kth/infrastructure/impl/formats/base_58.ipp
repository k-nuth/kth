// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_58_IPP
#define KTH_INFRASTUCTURE_BASE_58_IPP

#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// For support of template implementation only, do not call directly.
KI_API bool decode_base58_private(uint8_t* out, size_t out_size,
    char const* in);

template <size_t Size>
bool decode_base58(byte_array<Size>& out, std::string_view in)
{
    byte_array<Size> result;
    if ( ! decode_base58_private(result.data(), result.size(), in.data())) {
        return false;
}

    out = result;
    return true;
}

// TODO: determine if the sizing function is always accurate.
template <size_t Size>
byte_array<Size * 733 / 1000> base58_literal(char const(&string)[Size])
{
    // log(58) / log(256), rounded up.
    byte_array<Size * 733 / 1000> out;
    DEBUG_ONLY(auto const success =) decode_base58_private(out.data(),
        out.size(), string);
    KTH_ASSERT(success);
    return out;
}

} // namespace kth

#endif
