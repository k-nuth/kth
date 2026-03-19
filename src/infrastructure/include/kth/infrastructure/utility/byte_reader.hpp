// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_BYTES_READER_HPP
#define KTH_INFRASTRUCTURE_BYTES_READER_HPP

#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include <kth/infrastructure/concepts.hpp>
#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

using byte = byte_span::value_type;

template <typename T>
using expect = std::expected<T, code>;

struct byte_reader {
    constexpr explicit
    byte_reader(byte_span buffer)
        : buffer_(buffer)
        , position_(0)
    {}

    [[nodiscard]] constexpr
    size_t buffer_size() const { return buffer_.size(); }

    [[nodiscard]] constexpr
    byte_span buffer() const { return buffer_; }

    [[nodiscard]] constexpr
    size_t remaining_size() const { return buffer_.size() - position_; }

    [[nodiscard]] constexpr
    size_t position() const { return position_; }

    [[nodiscard]] constexpr
    expect<void> skip(size_t count) {
        if (position_ + count > buffer_.size()) {
            return std::unexpected(error::skip_past_end_of_buffer);
        }
        position_ += count;
        return {};
    }

    constexpr
    void unsafe_skip_byte() {
        ++position_;
    }

    [[nodiscard]] constexpr
    expect<void> skip_byte() {
        if (position_ >= buffer_.size()) {
            return std::unexpected(error::skip_past_end_of_buffer);
        }
        unsafe_skip_byte();
        return {};
    }

    constexpr
    byte unsafe_read_byte() {
        return buffer_[position_++];
    }

    [[nodiscard]] constexpr
    expect<byte> read_byte() {
        if (position_ >= buffer_.size()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        return unsafe_read_byte();
    }

    [[nodiscard]] constexpr
    expect<byte> peek_byte() const {
        if (position_ >= buffer_.size()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        return buffer_[position_];
    }

    [[nodiscard]] constexpr
    expect<byte_span> read_bytes(size_t size) {
        if (position_ + size > buffer_.size()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        auto const start = position_;
        position_ += size;
        return buffer_.subspan(start, size);
    }

    // Read bytes directly into a destination buffer.
    // Size to read is determined by dest.size().
    [[nodiscard]]
    expect<void> read_bytes_to(std::span<uint8_t> dest) {
        if (position_ + dest.size() > buffer_.size()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        std::memcpy(dest.data(), buffer_.data() + position_, dest.size());
        position_ += dest.size();
        return {};
    }

    // Read bytes directly into a fixed-size array.
    template <size_t N>
    [[nodiscard]]
    expect<std::array<uint8_t, N>> read_array() {
        if (position_ + N > buffer_.size()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        std::array<uint8_t, N> result;
        std::memcpy(result.data(), buffer_.data() + position_, N);
        position_ += N;
        return result;
    }

    [[nodiscard]] constexpr
    expect<byte_span> read_remaining_bytes() {
        auto const start = position_;
        position_ = buffer_.size();
        return buffer_.subspan(start);
    }

    constexpr
    void skip_remaining() {
        position_ = buffer_.size();
    }

    template <std::integral I>
    [[nodiscard]]
    expect<I> read_little_endian() {
        if (position_ + sizeof(I) > buffer_.size()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        I value = 0;
        std::memcpy(&value, buffer_.data() + position_, sizeof(I));
        position_ += sizeof(I);
        return value;
    }

    template <std::integral I>
    [[nodiscard]] constexpr
    expect<I> read_big_endian() {
        if (position_ + sizeof(I) > buffer_.size()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        I value = 0;
        for (size_t i = 0; i < sizeof(I); ++i) {
            value = (value << 8) | buffer_[position_++];
        }
        return value;
    }

    template <trivially_copyable T>
    [[nodiscard]]
    expect<T> read_packed() {
        if (position_ + sizeof(T) > buffer_.size()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        T value;
        std::memcpy(&value, buffer_.data() + position_, sizeof(T));
        position_ += sizeof(T);
        return value;
    }

    [[nodiscard]]
    expect<uint64_t> read_variable_little_endian() {
        auto const value_exp = read_byte();
        if ( ! value_exp) {
            return value_exp;
        }
        auto const value = *value_exp;

        switch (value) {
            case varint_eight_bytes:
                return read_little_endian<uint64_t>();
            case varint_four_bytes:
                return read_little_endian<uint32_t>();
            case varint_two_bytes:
                return read_little_endian<uint16_t>();
            default:
                return value;
        }
    }

    [[nodiscard]]
    expect<size_t> read_size_little_endian() {
        auto const size_exp = read_variable_little_endian();
        if ( ! size_exp) {
            return size_exp;
        }
        // Note: size_t may be smaller than uint64_t on some platforms.
        // but Knuth requires sizeof(size_t) >= sizeof(uint64_t).
        return size_t(*size_exp);
    }

    // Removes trailing zeros, required for bitcoin string comparisons.
    [[nodiscard]]
    expect<std::string> read_string(size_t size) {
        if (position_ + size > buffer_.size()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }

        std::string out;
        out.reserve(size);
        bool terminated = false;

        for (size_t i = 0; i < size; ++i) {
            auto const character = unsafe_read_byte();
            terminated |= (character == string_terminator);
            if ( ! terminated) {
                out.push_back(character);
            }
        }
        out.shrink_to_fit();
        return out;
    }

    [[nodiscard]]
    expect<std::string> read_string() {
        auto const size = read_size_little_endian();
        if ( ! size) {
            return std::unexpected(size.error());
        }
        return read_string(*size);
    }

    [[nodiscard]] constexpr
    bool is_exhausted() const {
        return position_ >= buffer_.size();
    }

    constexpr
    void reset() {
        position_ = 0;
    }

private:
    byte_span buffer_;
    size_t position_;
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_BYTES_READER_HPP
