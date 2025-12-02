// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_HASH_IPP
#define KTH_INFRASTUCTURE_HASH_IPP

#include <algorithm>
#include <cstddef>

#include <kth/infrastructure/utility/data.hpp>

namespace kth {

template <size_t Size>
byte_array<Size> scrypt(byte_span data, byte_span salt, uint64_t N,
    uint32_t p, uint32_t r)
{
    auto const out = scrypt(data, salt, N, r, p, Size);
    return to_array<Size>({ out });
}

} // namespace kth

#endif
