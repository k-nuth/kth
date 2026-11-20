// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_BIT_WRITER_HPP
#define KTH_INFRASTRUCTURE_BIT_WRITER_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/expect.hpp>

namespace kth {

// The bytes a stream of `bits` bits takes, its last byte completed.
[[nodiscard]] constexpr
size_t bytes_for_bits(size_t bits) noexcept {
    return bits / 8 + (bits % 8 != 0 ? 1 : 0);
}

// Symmetric counterpart to `bit_reader`, and to `byte_writer` one level down:
// writes a stream of bits into a caller-owned buffer. Within each byte the
// first bit written is the most significant one.
//
// The buffer must be pre-sized to the length the caller intends to emit
// (`bytes_for_bits` of the bits it will write). Every write is bounds-checked
// and returns `expect<void>`; one that does not fit writes nothing.
//
// Bits are gathered in a 64-bit window and stored a whole byte at a time, so a
// write is a shift and a mask. The bits of an unfinished byte are stored by
// `flush()`, which the caller owes the stream before reading the buffer.
struct bit_writer {
    // The most bits one write_bits() call takes.
    static constexpr size_t max_bits_per_write = 64;

    constexpr explicit
    bit_writer(std::span<uint8_t> buffer) noexcept
        : buffer_(buffer)
    {}

    // Bits written so far, the ones `flush()` still has to store included.
    [[nodiscard]] constexpr
    size_t bits_written() const noexcept { return position_ * 8 + held_; }

    // Bits the buffer still has room for.
    [[nodiscard]] constexpr
    size_t remaining_bits() const noexcept { return (buffer_.size() - position_) * 8 - held_; }

    [[nodiscard]] constexpr
    bool is_exhausted() const noexcept { return remaining_bits() == 0; }

    [[nodiscard]] constexpr
    bool can_write(size_t count) const noexcept { return count <= remaining_bits(); }

    [[nodiscard]] constexpr
    expect<void> write_bit(bool one) noexcept {
        if ( ! can_write(1)) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        put(one ? 1 : 0, 1);
        return {};
    }

    // The low `count` bits of `value`, the most significant of them first.
    // `count` is at most max_bits_per_write; the bits of `value` above them
    // are ignored.
    [[nodiscard]] constexpr
    expect<void> write_bits(uint64_t value, size_t count) noexcept {
        if (count > max_bits_per_write || ! can_write(count)) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        // The window keeps up to 7 bits of an unfinished byte, so a wide value
        // goes in two parts.
        if (count > half) {
            put(value >> half, count - half);
            put(value, half);
            return {};
        }
        put(value, count);
        return {};
    }

    // `ones` 1 bits and then a 0 bit: what `bit_reader::read_unary` reads back.
    [[nodiscard]] constexpr
    expect<void> write_unary(uint64_t ones) noexcept {
        // `ones + 1` would wrap for the largest count and pass the check.
        if (ones >= remaining_bits()) {
            return std::unexpected(error::write_past_end_of_buffer);
        }
        while (ones >= half) {
            put(~uint64_t{0}, half);
            ones -= half;
        }
        // The rest of the run and its closing 0 bit, in one write.
        put(((uint64_t{1} << ones) - 1) << 1, size_t(ones) + 1);
        return {};
    }

    // Stores the bits of an unfinished byte, completed with 0 bits. The stream
    // goes on at the next byte. Their room was counted when they were written.
    constexpr
    void flush() noexcept {
        if (held_ == 0) {
            return;
        }
        buffer_[position_++] = uint8_t(window_ << (8 - held_));
        held_ = 0;
    }

private:
    static constexpr size_t half = 32;

    // Up to `half` bits, which the caller has checked fit in the buffer.
    constexpr
    void put(uint64_t value, size_t count) noexcept {
        if (count == 0) {
            return;
        }
        // Whatever the window holds above its pending bits is never stored:
        // each byte is cut out of the pending ones.
        window_ = (window_ << count) | (value & ((uint64_t{1} << count) - 1));
        held_ += count;
        while (held_ >= 8) {
            held_ -= 8;
            buffer_[position_++] = uint8_t(window_ >> held_);
        }
    }

    std::span<uint8_t> buffer_;
    // The next byte of the buffer to store.
    size_t position_ = 0;
    // Its low `held_` bits are written and not yet stored.
    uint64_t window_ = 0;
    // Always less than 8 between calls.
    size_t held_ = 0;
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_BIT_WRITER_HPP
