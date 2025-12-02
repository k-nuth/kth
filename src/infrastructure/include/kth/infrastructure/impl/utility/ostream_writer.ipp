// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_OSTREAM_WRITER_IPP
#define KTH_INFRASTRUCTURE_OSTREAM_WRITER_IPP

#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>

namespace kth {

template <unsigned Size>
void ostream_writer::write_forward(byte_array<Size> const& value)
{
    auto const size = value.size();
    if (size > 0) {
        stream_.write(reinterpret_cast<char const*>(value.data()), size);
}
}

template <unsigned Size>
void ostream_writer::write_reverse(byte_array<Size> const& value)
{
    for (unsigned i = 0; i < Size; i++) {
        write_byte(value[Size - (i + 1)]);
}
}

template <typename Integer>
void ostream_writer::write_big_endian(Integer value)
{
    byte_array<sizeof(Integer)> bytes = to_big_endian(value);
    write_forward<sizeof(Integer)>(bytes);
}

template <typename Integer>
void ostream_writer::write_little_endian(Integer value)
{
    byte_array<sizeof(Integer)> bytes = to_little_endian(value);
    write_forward<sizeof(Integer)>(bytes);
}

} // namespace kth

#endif
