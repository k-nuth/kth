// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_READER_HPP
#define KTH_INFRASTRUCTURE_READER_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/data.hpp>
// #include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/hash_define.hpp>

namespace kth {

/// Reader interface.
struct KI_API reader {
    /// Context.
    operator bool() const;
    bool operator!() const;
    bool is_exhausted() const;
    void invalidate();

    /// Read hashes.
    hash_digest read_hash();
    short_hash read_short_hash();
    mini_hash read_mini_hash();

    /// Read big endian integers.
    uint16_t read_2_bytes_big_endian();
    uint32_t read_4_bytes_big_endian();
    uint64_t read_8_bytes_big_endian();
    uint64_t read_variable_big_endian();
    size_t read_size_big_endian();

    /// Read little endian integers.
    code read_error_code();
    uint16_t read_2_bytes_little_endian();
    uint32_t read_4_bytes_little_endian();
    uint64_t read_8_bytes_little_endian();
    uint64_t read_variable_little_endian();
    size_t read_size_little_endian();

    /// Read/peek one byte.
    uint8_t peek_byte();
    uint8_t read_byte();

    /// Read all remaining bytes.
    data_chunk read_bytes();

    /// Read required size buffer.
    data_chunk read_bytes(size_t size);

    /// Read variable length string.
    std::string read_string();

    /// Read required length string and trim nulls.
    std::string read_string(size_t size);

    /// Advance iterator without reading.
    void skip(size_t size);
};

} // namespace kth

#endif
