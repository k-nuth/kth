// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CHECKSUM_IPP
#define KTH_INFRASTUCTURE_CHECKSUM_IPP

#include <algorithm>
#include <cstddef>
#include <initializer_list>

#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>

namespace kth {

template <size_t Size>
bool build_checked_array(byte_array<Size>& out, std::initializer_list<byte_span> const& spans) {
    return build_array(out, spans) && insert_checksum(out);
}

template <size_t Size>
bool insert_checksum(byte_array<Size>& out) {
    if (out.size() < checksum_size) {
        return false;
    }

    data_chunk body(out.begin(), out.end() - checksum_size);
    auto const checksum = to_little_endian(bitcoin_checksum(body));
    std::copy_n(checksum.begin(), checksum_size, out.end() - checksum_size);
    return true;
}

// std::array<> is used in place of byte_array<> to enable Size deduction.
template <size_t Size>
bool unwrap(uint8_t& out_version, std::array<uint8_t, UNWRAP_SIZE(Size)>& out_payload, std::array<uint8_t, Size> const& wrapped) {
    uint32_t unused;
    return unwrap(out_version, out_payload, unused, wrapped);
}

// std::array<> is used in place of byte_array<> to enable Size deduction.
template <size_t Size>
bool unwrap(uint8_t& out_version, std::array<uint8_t, UNWRAP_SIZE(Size)>& out_payload, uint32_t& out_checksum, std::array<uint8_t, Size> const& wrapped) {
    if ( ! verify_checksum(wrapped)) {
        return false;
    }

    out_version = slice<0, 1>(wrapped)[0];
    out_payload = slice<1, Size - checksum_size>(wrapped);
    auto const bytes = slice<Size - checksum_size, Size>(wrapped);
    out_checksum = from_little_endian_unsafe<uint32_t>(bytes.begin());
    return true;
}

// std::array<> is used in place of byte_array<> to enable Size deduction.
template <size_t Size>
std::array<uint8_t, WRAP_SIZE(Size)> wrap(uint8_t version, std::array<uint8_t, Size> const& payload) {
    byte_array<WRAP_SIZE(Size)> out;
    build_array(out, { to_array(version), payload });
    insert_checksum(out);
    return out;
}

} // namespace kth

#endif
