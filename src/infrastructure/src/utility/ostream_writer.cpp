// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/ostream_writer.hpp>

#include <algorithm>
#include <iostream>

#include <kth/infrastructure/constants.hpp>
// #include <kth/infrastructure/math/limits.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/reader.hpp>


namespace kth {

ostream_writer::ostream_writer(std::ostream& stream)
    : stream_(stream)
{}

// ostream_writer::ostream_writer(data_sink& stream)
//     : stream_(stream)
// {}

// Context.
//-----------------------------------------------------------------------------

ostream_writer::operator bool() const {
    return (bool)stream_;
}

bool ostream_writer::operator!() const {
    return !stream_;
}

// Hashes.
//-----------------------------------------------------------------------------

void ostream_writer::write_hash(hash_digest const& value) {
    stream_.write(reinterpret_cast<char const*>(value.data()), value.size());
}

void ostream_writer::write_short_hash(short_hash const& value) {
    stream_.write(reinterpret_cast<char const*>(value.data()), value.size());
}

void ostream_writer::write_mini_hash(mini_hash const& value) {
    stream_.write(reinterpret_cast<char const*>(value.data()), value.size());
}

// Big Endian Integers.
//-----------------------------------------------------------------------------

void ostream_writer::write_2_bytes_big_endian(uint16_t value) {
    write_big_endian<uint16_t>(value);
}

void ostream_writer::write_4_bytes_big_endian(uint32_t value) {
    write_big_endian<uint32_t>(value);
}

void ostream_writer::write_8_bytes_big_endian(uint64_t value) {
    write_big_endian<uint64_t>(value);
}

void ostream_writer::write_variable_big_endian(uint64_t value) {
    if (value < varint_two_bytes) {
        write_byte(uint8_t(value));
    } else if (value <= max_uint16) {
        write_byte(varint_two_bytes);
        write_2_bytes_big_endian(uint16_t(value));
    } else if (value <= max_uint32) {
        write_byte(varint_four_bytes);
        write_4_bytes_big_endian(uint32_t(value));
    } else {
        write_byte(varint_eight_bytes);
        write_8_bytes_big_endian(value);
    }
}

void ostream_writer::write_size_big_endian(size_t value) {
    write_variable_big_endian(value);
}

// Little Endian Integers.
//-----------------------------------------------------------------------------

void ostream_writer::write_error_code(code const& ec) {
    write_4_bytes_little_endian(uint32_t(ec.value()));
}

void ostream_writer::write_2_bytes_little_endian(uint16_t value) {
    write_little_endian<uint16_t>(value);
}

void ostream_writer::write_4_bytes_little_endian(uint32_t value) {
    write_little_endian<uint32_t>(value);
}

void ostream_writer::write_8_bytes_little_endian(uint64_t value) {
    write_little_endian<uint64_t>(value);
}

void ostream_writer::write_variable_little_endian(uint64_t value) {
    if (value < varint_two_bytes) {
        write_byte(uint8_t(value));
    } else if (value <= max_uint16) {
        write_byte(varint_two_bytes);
        write_2_bytes_little_endian(uint16_t(value));
    } else if (value <= max_uint32) {
        write_byte(varint_four_bytes);
        write_4_bytes_little_endian(uint32_t(value));
    } else {
        write_byte(varint_eight_bytes);
        write_8_bytes_little_endian(value);
    }
}

void ostream_writer::write_size_little_endian(size_t value) {
    write_variable_little_endian(value);
}

// Bytes.
//-----------------------------------------------------------------------------

void ostream_writer::write_byte(uint8_t value) {
    stream_.put(value);
}

void ostream_writer::write_bytes(data_chunk const& data) {
    auto const size = data.size();

    if (size > 0) {
        stream_.write(reinterpret_cast<char const*>(data.data()), size);
    }
}

void ostream_writer::write_bytes(uint8_t const* data, size_t size) {
    auto buffer = reinterpret_cast<char const*>(data);
    stream_.write(buffer, size);
}

void ostream_writer::write_string(std::string const& value, size_t size) {
    auto const length = std::min(size, value.size());
    write_bytes(reinterpret_cast<uint8_t const*>(value.data()), length);
    data_chunk padding(floor_subtract(size, length), string_terminator);
    write_bytes(padding);
}

void ostream_writer::write_string(std::string const& value) {
    write_variable_little_endian(value.size());
    stream_.write(value.data(), value.size());
}

void ostream_writer::skip(size_t size) {
    // TODO: verify.
    stream_.seekp(size, std::ios_base::cur);
}

} // namespace kth
