// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/checksum.hpp>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/endian.hpp>

namespace kth {

void append_checksum(data_chunk& data)
{
    auto const checksum = bitcoin_checksum(data);
    extend_data(data, to_little_endian(checksum));
}

uint32_t bitcoin_checksum(data_slice data)
{
    auto const hash = bitcoin_hash(data);
    return from_little_endian_unsafe<uint32_t>(hash);
}

bool verify_checksum(data_slice data)
{
    if (data.size() < checksum_size) {
        return false;
    }

    auto const slice_size = data.size() - checksum_size;
    auto const checksum = from_little_endian<uint32_t>(data.subspan(slice_size).first<sizeof(uint32_t)>());
    return bitcoin_checksum(data.subspan(0, slice_size)) == checksum;
}

} // namespace kth

