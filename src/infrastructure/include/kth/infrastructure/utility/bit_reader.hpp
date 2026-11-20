// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_BIT_READER_HPP
#define KTH_INFRASTRUCTURE_BIT_READER_HPP

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/expect.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// Reads a byte string as a stream of bits. Within each byte the first bit read
// is the most significant one. The counterpart of `bit_writer`, and of
// `byte_reader` one level down.
//
// Every bit of the buffer can be read, the padding of the last byte included:
// where the meaningful bits end is the caller's knowledge, not the stream's.
// A read that asks for more bits than are left fails and consumes nothing.
//
// The bits not yet handed out are kept in a 64-bit window that is refilled
// several bytes at a time, so a read is a shift and a mask and a run of equal
// bits is counted with one instruction instead of one test per bit.
struct bit_reader {
    // The most bits one read_bits() call hands out.
    static constexpr size_t max_bits_per_read = 64;

    constexpr explicit
    bit_reader(byte_span buffer) noexcept
        : buffer_(buffer)
    {}

    // Bits not yet read.
    [[nodiscard]] constexpr
    size_t remaining_bits() const noexcept {
        return (buffer_.size() - position_) * 8 + held_;
    }

    [[nodiscard]] constexpr
    bool is_exhausted() const noexcept { return remaining_bits() == 0; }

    [[nodiscard]]
    expect<bool> read_bit() noexcept {
        refill();
        if (held_ == 0) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        bool const one = (window_ >> 63) != 0;
        consume(1);
        return one;
    }

    // The next `count` bits as a number, the first bit read being its most
    // significant. `count` is at most max_bits_per_read; zero bits are zero.
    [[nodiscard]]
    expect<uint64_t> read_bits(size_t count) noexcept {
        if (count > max_bits_per_read || count > remaining_bits()) {
            return std::unexpected(error::read_past_end_of_buffer);
        }
        // A refilled window holds at least 57 bits while the buffer has them,
        // so a wider read is taken in two parts.
        if (count > half) {
            auto const high = take(count - half);
            return (high << half) | take(half);
        }
        return take(count);
    }

    // The length of the run of 1 bits up to the next 0 bit. The 0 bit is
    // consumed with the run. Fails if the stream ends before a 0 bit; what is
    // left of the stream is then unspecified, as it holds no number.
    [[nodiscard]]
    expect<uint64_t> read_unary() noexcept {
        uint64_t ones = 0;
        while (true) {
            refill();
            if (held_ == 0) {
                return std::unexpected(error::read_past_end_of_buffer);
            }
            // A run that stops short of the bits the window does not hold
            // ended inside it. One that reaches them is taken up to there and
            // counted on after the refill.
            auto const run = size_t(std::countl_one(window_));
            if (run < held_) {
                consume(run + 1);
                return ones + run;
            }
            ones += held_;
            consume(held_);
        }
    }

private:
    static constexpr size_t half = 32;

    // Up to `half` bits, which the caller has checked are there.
    uint64_t take(size_t count) noexcept {
        if (count == 0) {
            return 0;
        }
        refill();
        auto const value = window_ >> (64 - count);
        consume(count);
        return value;
    }

    constexpr
    void consume(size_t count) noexcept {
        // A shift by the whole width is not defined.
        window_ = count == 64 ? 0 : window_ << count;
        held_ -= count;
    }

    // Fills the window with whole bytes. Afterwards it holds at least 57 bits,
    // or every bit that was left. A bit below the held ones is zero or a copy
    // of the bit the buffer has for that place, never anything else.
    void refill() noexcept {
        auto const room = (64 - held_) / 8;
        if (room == 0) {
            return;
        }
        auto const left = buffer_.size() - position_;
        if (left >= 8) {
            // One load of eight bytes, of which the first `room` are counted.
            // The bits of the others land below the held ones, where the next
            // refill loads those same bits again.
            uint64_t next;
            std::memcpy(&next, buffer_.data() + position_, sizeof(next));
            if (std::endian::native == std::endian::little) {
                next = std::byteswap(next);
            }
            window_ |= next >> held_;
            position_ += room;
            held_ += room * 8;
            return;
        }
        auto const count = room < left ? room : left;
        for (size_t i = 0; i < count; ++i) {
            window_ |= uint64_t{buffer_[position_++]} << (56 - held_);
            held_ += 8;
        }
    }

    byte_span buffer_;
    // The next byte of the buffer to load into the window.
    size_t position_ = 0;
    // The bits loaded and not yet read, the next one in the top bit.
    uint64_t window_ = 0;
    // How many bits of the window are still to read.
    size_t held_ = 0;
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_BIT_READER_HPP
