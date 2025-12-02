// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CHECKSUM_HPP
#define KTH_INFRASTUCTURE_CHECKSUM_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

static constexpr size_t checksum_size = sizeof(uint32_t);

#define WRAP_SIZE(payload_size) (payload_size + checksum_size + 1)
#define UNWRAP_SIZE(payload_size) (payload_size - checksum_size - 1)

/**
 * Concatenate several data spans into a single fixed size array and append a
 * checksum.
 */
template <size_t Size>
bool build_checked_array(byte_array<Size>& out, const std::initializer_list<byte_span>& spans);

/**
 * Appends a four-byte checksum into the end of an array.
 * Returns false if the array is too small to contain the checksum.
 */
template <size_t Size>
bool insert_checksum(byte_array<Size>& out);

/**
 * Unwrap a wrapped payload.
 * @param[out] out_version   The version byte of the wrapped data.
 * @param[out] out_payload   The payload of the wrapped data.
 * @param[in]  wrapped       The wrapped data to unwrap.
 * @return                   True if input checksum validates.
 */
template <size_t Size>
bool unwrap(uint8_t& out_version, byte_array<UNWRAP_SIZE(Size)>& out_payload,
    const std::array<uint8_t, Size>& wrapped);

/**
 * Unwrap a wrapped payload and return the checksum.
 * @param[out] out_version   The version byte of the wrapped data.
 * @param[out] out_payload   The payload of the wrapped data.
 * @param[out] out_checksum  The validated checksum of the wrapped data.
 * @param[in]  wrapped       The wrapped data to unwrap.
 * @return                   True if input checksum validates.
 */
template <size_t Size>
bool unwrap(uint8_t& out_version,
    byte_array<UNWRAP_SIZE(Size)>& out_payload, uint32_t& out_checksum,
    const std::array<uint8_t, Size>& wrapped);

/**
 * Wrap arbitrary data.
 * @param[in]  version  The version byte for the wrapped data.
 * @param[out] payload  The payload to wrap.
 * @return              The wrapped data.
 */
template <size_t Size>
std::array<uint8_t, WRAP_SIZE(Size)> wrap(uint8_t version,
    const std::array<uint8_t, Size>& payload);

/**
 * Appends a four-byte checksum of a data chunk to itself.
 */
KI_API void append_checksum(data_chunk& data);

/**
 * Generate a bitcoin hash checksum. Last 4 bytes of sha256(sha256(data))
 *
 * int(sha256(sha256(data))[-4:])
 */
KI_API uint32_t bitcoin_checksum(byte_span data);

/**
 * Verifies the last four bytes of a data chunk are a valid checksum of the
 * earlier bytes. This is typically used to verify base58 data.
 */
KI_API bool verify_checksum(byte_span data);

} // namespace kth

#include <kth/infrastructure/impl/math/checksum.ipp>

#endif

