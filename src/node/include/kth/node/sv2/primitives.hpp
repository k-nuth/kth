// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_PRIMITIVES_HPP
#define KTH_NODE_SV2_PRIMITIVES_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

// Stratum V2 variable-length primitives (sv2-spec 03-Protocol-Overview). Each is
// a byte payload behind a little-endian length prefix whose width bounds the
// payload: B0_255 (U8), B0_64K (U16), B0_16M (U24). STR0_255 is a B0_255 carrying
// UTF-8 text. The fixed-width integers (U8/U16/U32/U64) and U24 are already
// covered by byte_reader / byte_writer and framing.hpp.

namespace kth::node::sv2 {

constexpr size_t b0_255_max = 0xffu;
constexpr size_t b0_64k_max = 0xffffu;
constexpr size_t b0_16m_max = 0xffffffu;

// write_* fail (error::operation_failed) when the payload exceeds the prefix's
// range. read_* return a view into the reader's buffer, valid for its lifetime.
[[nodiscard]] expect<void> write_bytes_u8(byte_writer& sink, byte_span value);
[[nodiscard]] expect<void> write_bytes_u16(byte_writer& sink, byte_span value);
[[nodiscard]] expect<void> write_bytes_u24(byte_writer& sink, byte_span value);

[[nodiscard]] expect<byte_span> read_bytes_u8(byte_reader& source);
[[nodiscard]] expect<byte_span> read_bytes_u16(byte_reader& source);
[[nodiscard]] expect<byte_span> read_bytes_u24(byte_reader& source);

// STR0_255: a U8-length-prefixed UTF-8 string. Unlike byte_reader::read_string,
// interior and trailing NUL bytes are preserved (SV2 strings are not NUL-padded).
[[nodiscard]] expect<void> write_string_u8(byte_writer& sink, std::string_view value);
[[nodiscard]] expect<std::string> read_string_u8(byte_reader& source);

// Sequences: a little-endian count followed by that many elements. write_* fail
// when the element count overflows the count prefix.
//
// SEQ0_255[U256]: a U8 count of 32-byte values (e.g. a merkle path).
[[nodiscard]] expect<void> write_hash_seq_u8(byte_writer& sink, std::vector<hash_digest> const& items);
[[nodiscard]] expect<std::vector<hash_digest>> read_hash_seq_u8(byte_reader& source);

// SEQ0_64K[B0_16M]: a U16 count of B0_16M byte blobs (e.g. a transaction list).
[[nodiscard]] expect<void> write_bytes_seq_u16(byte_writer& sink, std::vector<data_chunk> const& items);
[[nodiscard]] expect<std::vector<data_chunk>> read_bytes_seq_u16(byte_reader& source);

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_PRIMITIVES_HPP
