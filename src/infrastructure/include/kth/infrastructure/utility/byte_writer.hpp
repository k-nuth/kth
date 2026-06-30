// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_BYTES_WRITER_HPP
#define KTH_INFRASTRUCTURE_BYTES_WRITER_HPP

#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>

#include <kth/infrastructure/concepts.hpp>
#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// Symmetric counterpart to `byte_reader`: writes the on-wire (or store)
// byte representation of domain values into a caller-owned buffer.
// The buffer must be pre-sized to the exact serialization length the
// caller intends to emit (use `T::serialized_size(...)`). Every write
// is bounds-checked and returns `expect<void>` so partial writes never
// silently overflow.
struct byte_writer {
    constexpr explicit
    byte_writer(std::span<uint8_t> buffer)
        : buffer_(buffer)
        , position_(0)
    {}

    [[nodiscard]] constexpr
    size_t buffer_size() const { return buffer_.size(); }

    [[nodiscard]] constexpr
    std::span<uint8_t> buffer() const { return buffer_; }

    [[nodiscard]] constexpr
    size_t remaining_size() const { return buffer_.size() - position_; }

    [[nodiscard]] constexpr
    size_t position() const { return position_; }

    [[nodiscard]] constexpr
    bool is_exhausted() const { return position_ >= buffer_.size(); }

    constexpr
    void reset() { position_ = 0; }

    constexpr
    void unsafe_write_byte(uint8_t b) {
        buffer_[position_++] = b;
    }

    [[nodiscard]] constexpr
    expect<void> write_byte(uint8_t b) {
        if (position_ >= buffer_.size()) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        unsafe_write_byte(b);
        return {};
    }

    [[nodiscard]]
    expect<void> write_bytes(std::span<uint8_t const> bytes) {
        if (position_ + bytes.size() > buffer_.size()) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        std::memcpy(buffer_.data() + position_, bytes.data(), bytes.size());
        position_ += bytes.size();
        return {};
    }

    template <size_t N>
    [[nodiscard]]
    expect<void> write_array(std::array<uint8_t, N> const& a) {
        if (position_ + N > buffer_.size()) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        std::memcpy(buffer_.data() + position_, a.data(), N);
        position_ += N;
        return {};
    }

    template <std::integral I>
    [[nodiscard]]
    expect<void> write_little_endian(I value) {
        if (position_ + sizeof(I) > buffer_.size()) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        std::memcpy(buffer_.data() + position_, &value, sizeof(I));
        position_ += sizeof(I);
        return {};
    }

    template <std::integral I>
    [[nodiscard]] constexpr
    expect<void> write_big_endian(I value) {
        if (position_ + sizeof(I) > buffer_.size()) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        for (size_t i = sizeof(I); i > 0; --i) {
            buffer_[position_++] = static_cast<uint8_t>(value >> ((i - 1) * 8));
        }
        return {};
    }

    template <trivially_copyable T>
    [[nodiscard]]
    expect<void> write_packed(T const& value) {
        if (position_ + sizeof(T) > buffer_.size()) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        std::memcpy(buffer_.data() + position_, &value, sizeof(T));
        position_ += sizeof(T);
        return {};
    }

    [[nodiscard]]
    expect<void> write_variable_little_endian(uint64_t value) {
        if (value < varint_two_bytes) {
            return write_byte(static_cast<uint8_t>(value));
        }
        if (value <= 0xffffull) {
            if (auto r = write_byte(varint_two_bytes); ! r) return r;
            return write_little_endian<uint16_t>(static_cast<uint16_t>(value));
        }
        if (value <= 0xffffffffull) {
            if (auto r = write_byte(varint_four_bytes); ! r) return r;
            return write_little_endian<uint32_t>(static_cast<uint32_t>(value));
        }
        if (auto r = write_byte(varint_eight_bytes); ! r) return r;
        return write_little_endian<uint64_t>(value);
    }

    [[nodiscard]]
    expect<void> write_size_little_endian(size_t value) {
        return write_variable_little_endian(static_cast<uint64_t>(value));
    }

    [[nodiscard]]
    expect<void> write_hash(hash_digest const& hash) {
        return write_array(hash);
    }

    // Writes a variable-length-prefixed string (canonical encoding):
    //   size (varint) | bytes
    [[nodiscard]]
    expect<void> write_string(std::string const& value) {
        if (auto r = write_size_little_endian(value.size()); ! r) return r;
        return write_bytes(std::span{
            reinterpret_cast<uint8_t const*>(value.data()), value.size()
        });
    }

    // Writes a fixed-size string padded with `string_terminator` to the
    // requested length. Truncates silently if `value` is longer than `size`.
    // Mirrors `byte_reader::read_string(size_t)`.
    [[nodiscard]]
    expect<void> write_string_fixed(std::string const& value, size_t size) {
        if (position_ + size > buffer_.size()) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        auto const copy = std::min(value.size(), size);
        std::memcpy(buffer_.data() + position_, value.data(), copy);
        if (copy < size) {
            std::memset(buffer_.data() + position_ + copy, string_terminator, size - copy);
        }
        position_ += size;
        return {};
    }

private:
    std::span<uint8_t> buffer_;
    size_t position_;
};

// Size of the variable-length integer (varint) wire encoding for `value`.
// Mirrors what `byte_writer::write_variable_little_endian` produces. Used by
// every `serialized_size(...)` that includes a length-prefixed field.
inline constexpr
size_t size_variable_integer(uint64_t value) {
    if (value < varint_two_bytes) return 1;
    if (value <= max_uint16) return 3;
    if (value <= max_uint32) return 5;
    return 9;
}

// Anything that knows how to write itself into a `byte_writer` and report its
// own wire size. The same parameter pack must serve both calls.
template <typename T, typename... Args>
concept Serializable = requires(T const& t, byte_writer& w, Args... args) {
    { t.serialized_size(args...) } -> std::convertible_to<size_t>;
    { t.to_data(w, args...) };
};

// Allocate a `data_chunk` sized to `obj.serialized_size(args...)`, write
// `obj.to_data(writer, args...)` into it, and return the buffer. Replaces the
// per-class `data_chunk to_data(...) const` convenience wrappers that all
// reimplemented the same allocate/write/return dance.
template <typename T, typename... Args>
    requires Serializable<T, Args...>
[[nodiscard]] inline
data_chunk to_data_chunk(T const& obj, Args... args) {
    data_chunk buf(obj.serialized_size(args...));
    byte_writer writer(buf);
    auto const r = obj.to_data(writer, args...);
    KTH_ASSERT(r.has_value());
    return buf;
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_BYTES_WRITER_HPP
