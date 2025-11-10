// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/checksum.hpp>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/deserializer.hpp>
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
    return from_little_endian_unsafe<uint32_t>(hash.begin());
}

bool verify_checksum(data_slice data)
{
    if (data.size() < checksum_size) {
        return false;
}

    // TODO: create a bitcoin_checksum overload that can accept begin/end.
    auto const slice_size = data.size() - checksum_size;
    data_slice slice(data.data(), slice_size);
    auto checksum = from_little_endian_unsafe<uint32_t>(data.data() + slice_size);
    return bitcoin_checksum(slice) == checksum;
}

} // namespace kth

