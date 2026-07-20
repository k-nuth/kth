// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_85_IPP
#define KTH_INFRASTUCTURE_BASE_85_IPP

#include <algorithm>
#include <cstddef>
#include <expected>
#include <string_view>

#include <kth/infrastructure/utility/data.hpp>

namespace kth {

template <size_t Size>
std::expected<byte_array<Size>, base85_errc> decode_base85(std::string_view in) {
    // Every 5 characters produce 4 bytes; refuse straight away if the
    // requested output size can't come from ANY valid input length.
    if (in.size() * 4 / 5 != Size) {
        return std::unexpected(base85_errc::invalid_length);
    }
    byte_array<Size> out;
    auto const written = decode_base85(in, out);
    if ( ! written) {
        return std::unexpected(written.error());
    }
    // `written` must equal `Size` on success (the length guard above
    // makes that a compile-time-derived invariant), but assert to
    // catch any future drift in the span-form impl.
    if (*written != Size) {
        return std::unexpected(base85_errc::invalid_length);
    }
    return out;
}

} // namespace kth

#endif
