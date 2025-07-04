// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_BYTES_READER_HPP
#define KTH_INFRASTRUCTURE_BYTES_READER_HPP

#include <cstdint>
#include <vector>
#include <span>
// #include <expected>
#include <cstring>
#include <iostream>

#include <nonstd/expected.hpp>
//TODO: Mover a otro lugar

#include <kth/infrastructure/concepts.hpp>
#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

using byte = byte_span::value_type;

template <typename T>
using expect = nonstd::expected<T, code>;
using nonstd::make_unexpected;

struct byte_reader {
    explicit
    byte_reader(byte_span buffer)
        : buffer_(buffer)
        , position_(0)
    {}

    size_t buffer_size() const { return buffer_.size(); }

    size_t remaining_size() const { return buffer_.size() - position_; }

    size_t position() const { return position_; }

    expect<void> skip(size_t count) {
        if (position_ + count > buffer_.size()) {
            return make_unexpected(error::skip_past_end_of_buffer);
        }
        position_ += count;
        return {};
    }

    byte unsafe_read_byte() {
        return buffer_[position_++];
    }

    expect<byte> read_byte() {
        if (position_ >= buffer_.size()) {
            return make_unexpected(error::read_past_end_of_buffer);
        }
        return unsafe_read_byte();
    }

    expect<byte> peek_byte() {
        if (position_ >= buffer_.size()) {
            return make_unexpected(error::read_past_end_of_buffer);
        }
        return buffer_[position_];
    }

    expect<byte_span> read_bytes(size_t size) {
        if (position_ + size > buffer_.size()) {
            return make_unexpected(error::read_past_end_of_buffer);
        }
        auto start = position_;
        position_ += size;
        return buffer_.subspan(start, size);
    }

    expect<byte_span> read_remaining_bytes() {
        return read_bytes(buffer_.size() - position_);
    }

    expect<void> skip_remaining() {
        return skip(buffer_.size() - position_);
    }

    template <std::integral I>
    expect<I> read_little_endian() {
        if (position_ + sizeof(I) > buffer_.size()) {
            return make_unexpected(error::read_past_end_of_buffer);
        }
        I value = 0;
        std::memcpy(&value, buffer_.data() + position_, sizeof(I));
        position_ += sizeof(I);
        return value;
    }

    template <std::integral I>
    expect<I> read_big_endian() {
        if (position_ + sizeof(I) > buffer_.size()) {
            return make_unexpected(error::read_past_end_of_buffer);
        }
        I value = 0;
        for (size_t i = 0; i < sizeof(I); ++i) {
            value = (value << 8) | buffer_[position_++];
        }
        return value;
    }

    template <trivially_copyable T>
    expect<T> read_packed() {
        if (position_ + sizeof(T) > buffer_.size()) {
            return make_unexpected(error::read_past_end_of_buffer);
        }
        T value;
        std::memcpy(&value, buffer_.data() + position_, sizeof(T));
        position_ += sizeof(T);
        return value;
    }

    expect<uint64_t> read_variable_little_endian() {
        auto const value_exp = read_byte();
        if ( ! value_exp) {
            // return value_exp.error();
            return value_exp;
        }
        auto const value = *value_exp;

        switch (value) {
            case varint_eight_bytes: {
                return read_little_endian<uint64_t>();
            }
            case varint_four_bytes: {
                return read_little_endian<uint32_t>();
            }
            case varint_two_bytes: {
                return read_little_endian<uint16_t>();
            }
            default: {
                return value;
            }
        }
    }

    expect<size_t> read_size_little_endian() {
        // constexpr uint64_t max_size_t = std::numeric_limits<size_t>::max();

        auto const size_exp = read_variable_little_endian();
        if ( ! size_exp) {
            return size_exp;
        }
        auto const size = *size_exp;
        if (size <= max_size_t) {
            return size_t(size);
        }

        return make_unexpected(error::read_past_end_of_buffer);
    }


    // std::string istream_reader::read_string() {
    //     return read_string(read_size_little_endian());
    // }

    // // Removes trailing zeros, required for bitcoin string comparisons.
    // std::string istream_reader::read_string(size_t size) {
    //     std::string out;
    //     out.reserve(size);
    //     auto terminated = false;

    //     // Read all size characters, pushing all non-null (may be many).
    //     for (size_t index = 0; index < size && !empty(); ++index) {
    //         auto const character = read_byte();
    //         terminated |= (character == string_terminator);

    //         // Stop pushing characters at the first null.
    //         if ( ! terminated) {
    //             out.push_back(character);
    //         }
    //     }

    //     // Reduce the allocation to the number of characters pushed.
    //     out.shrink_to_fit();
    //     return out;
    // }

    // Removes trailing zeros, required for bitcoin string comparisons.
    expect<std::string> read_string(size_t size) {
        if (position_ + size > buffer_.size()) {
            return make_unexpected(error::read_past_end_of_buffer);
        }

        std::string out;
        out.reserve(size);
        auto terminated = false;

        for (size_t i = 0; i < size; ++i) {
            // No need to check for error, we know the size is correct.
            auto const character = unsafe_read_byte();
            terminated |= (character == string_terminator);

            if ( ! terminated) {
                out.push_back(character);
            }
        }
        out.shrink_to_fit();
        return out;
    }

    expect<std::string> read_string() {
        auto const size = read_size_little_endian();
        if ( ! size) {
            return make_unexpected(size.error());
        }
        return read_string(*size);
    }

    bool is_exhausted() const {
        return position_ >= buffer_.size();
    }

    void reset() {
        position_ = 0;
    }

private:
    byte_span buffer_;
    size_t position_;
};

// bool starts_with(byte_reader& reader, byte_span value) {
//     if (reader.remaining_size() < value.size()) {
//         return false;
//     }
//     return std::equal(value.begin(), value.end(), reader.buffer_.begin() + reader.position_);
// }

} // namespace kth

#endif // KTH_INFRASTRUCTURE_BYTES_READER_HPP
