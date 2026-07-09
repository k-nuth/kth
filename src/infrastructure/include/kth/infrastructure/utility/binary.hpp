// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_BINARY_HPP
#define KTH_INFRASTRUCTURE_BINARY_HPP

#include <algorithm>
#include <bit>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>

namespace kth {

struct KI_API binary {
    using block = uint8_t;
    using size_type = std::size_t;

    static
    constexpr size_type bits_per_block = byte_bits;

    [[nodiscard]] static constexpr
    size_type blocks_size(size_type bit_size) noexcept {
        return bit_size == 0 ? 0 : (bit_size - 1) / bits_per_block + 1;
    }

    /// Parse a base2 string (each character `'0'` or `'1'` is one bit).
    /// Returns `error::illegal_value` on any other character.
    [[nodiscard]] static constexpr
    std::expected<binary, kth::code> parse_from(std::string_view bit_string) {
        if ( ! is_base2(bit_string)) {
            return std::unexpected(kth::error::illegal_value);
        }

        binary out;
        uint8_t block = 0;
        auto bit_iterator = binary::bits_per_block;

        for (char const representation : bit_string) {
            if (representation == '1') {
                uint8_t const bitmask = 1 << (bit_iterator - 1);
                block |= bitmask;
            }

            --bit_iterator;

            if (bit_iterator == 0) {
                out.blocks_.push_back(block);
                block = 0;
                bit_iterator = binary::bits_per_block;
            }
        }

        if (bit_iterator != binary::bits_per_block) {
            out.blocks_.push_back(block);
        }

        out.resize(bit_string.size());
        return out;
    }

    constexpr binary() = default;

    constexpr binary(size_type size, uint32_t number)
        : binary(size, byte_span{to_little_endian(number)})
    {}

    constexpr binary(size_type size, byte_span blocks) {
        blocks_.resize(blocks.size());
        if ( ! blocks_.empty()) {
            std::copy_n(blocks.begin(), blocks.size(), blocks_.begin());
            while (blocks_.size() * bits_per_block < size) {
                blocks_.push_back(0x00);
            }
        }
        resize(size);
    }

    constexpr void resize(size_type size) {
        final_block_excess_ = 0;
        blocks_.resize(blocks_size(size), 0);
        size_type const offset = size % bits_per_block;

        if (offset > 0 && ! blocks_.empty()) {
            // `blocks_.size() >= 1` here because `blocks_size(size)`
            // above allocated at least one block for `offset > 0`.
            final_block_excess_ = uint8_t(bits_per_block - offset);
            uint8_t const mask = 0xFF << final_block_excess_;
            blocks_.back() &= mask;
        }
    }

    [[nodiscard]] constexpr
    bool operator[](size_type index) const {
        // Assert is a runtime-only diagnostic; skip during constant
        // evaluation so the operator remains usable in constexpr
        // contexts (C++23 `if consteval`).
        if ( ! std::is_constant_evaluated()) {
            KTH_ASSERT(index < size());
        }
        size_type const block_index = index / bits_per_block;
        uint8_t const block_ = blocks_[block_index];
        size_type const offset = index - (block_index * bits_per_block);
        uint8_t const bitmask = 1 << (bits_per_block - offset - 1);
        return (block_ & bitmask) > 0;
    }

    [[nodiscard]] constexpr
    data_chunk const& blocks() const noexcept {
        return blocks_;
    }

    /// Base2 encoding used by `fmt::formatter<binary>`.
    [[nodiscard]] constexpr
    std::string to_string() const {
        // Walk blocks (bytes), not bits: divisions by 8 in `operator[]`
        // dominate the naive per-bit form. Uninitialized-then-write is
        // faster than push_back — we know the exact size.
        auto const n = size();
        std::string bits(n, '0');
        for (size_type i = 0; i < n; ++i) {
            auto const block_i = i >> 3;                     // i / 8
            auto const bit_pos = 7 - (i & 7);                 // i % 8, MSB-first
            if ((blocks_[block_i] >> bit_pos) & 1) {
                bits[i] = '1';
            }
        }
        return bits;
    }

    /// size in bits
    [[nodiscard]] constexpr
    size_type size() const noexcept {
        size_type const base_bit_size = blocks_.size() * bits_per_block;
        if (base_bit_size < final_block_excess_) {
            return 0;
        }
        return base_bit_size - final_block_excess_;
    }

    constexpr void append(binary const& post) {
        size_type const block_offset = size() / bits_per_block;
        size_type const offset = size() % bits_per_block;

        // overkill for byte alignment
        binary duplicate(post.size(), byte_span{post.blocks()});
        duplicate.shift_right(offset);
        resize(size() + post.size());
        data_chunk post_shift_blocks = duplicate.blocks();

        for (size_type i = 0; i < post_shift_blocks.size(); i++) {
            blocks_[block_offset + i] = blocks_[block_offset + i] | post_shift_blocks[i];
        }
    }

    constexpr void prepend(binary const& prior) {
        shift_right(prior.size());
        data_chunk prior_blocks = prior.blocks();

        for (size_type i = 0; i < prior_blocks.size(); i++) {
            blocks_[i] = blocks_[i] | prior_blocks[i];
        }
    }

    constexpr void shift_left(size_type distance) {
        size_type const initial_size = size();
        size_type const initial_block_count = blocks_.size();
        size_type destination_size = 0;

        if (distance < initial_size) {
            destination_size = initial_size - distance;
        }

        size_type const block_offset = distance / bits_per_block;
        size_type const offset = distance % bits_per_block;

        for (size_type i = 0; i < initial_block_count; i++) {
            uint8_t leading_bits = 0x00;
            uint8_t trailing_bits = 0x00;

            if ((offset != 0) && ((block_offset + i + 1) < initial_block_count)) {
                trailing_bits = blocks_[block_offset + i + 1] >> (bits_per_block - offset);
            }

            if ((block_offset + i) < initial_block_count) {
                leading_bits = blocks_[block_offset + i] << offset;
            }

            blocks_[i] = leading_bits | trailing_bits;
        }

        resize(destination_size);
    }

    constexpr void shift_right(size_type distance) {
        size_type const initial_size = size();
        size_type const initial_block_count = blocks_.size();
        size_type const offset = distance % bits_per_block;
        size_type const offset_blocks = distance / bits_per_block;
        size_type const destination_size = initial_size + distance;

        for (size_type i = 0; i < offset_blocks; i++) {
            blocks_.insert(blocks_.begin(), 0x00);
        }

        uint8_t previous = 0x00;

        for (size_type i = 0; i < initial_block_count; i++) {
            uint8_t current = blocks_[offset_blocks + i];
            uint8_t leading_bits = previous << (bits_per_block - offset);
            uint8_t trailing_bits = current >> offset;
            blocks_[offset_blocks + i] = leading_bits | trailing_bits;
            previous = current;
        }

        resize(destination_size);

        if (offset_blocks + initial_block_count < blocks_.size()) {
            blocks_[blocks_.size() - 1] = previous << (bits_per_block - offset);
        }
    }

    [[nodiscard]] constexpr
    binary substring(size_type start, size_type length) const {
        size_type current_size = size();

        if (start > current_size) {
            start = current_size;
        }

        if ((length == max_size_t) || ((start + length) > current_size)) {
            length = current_size - start;
        }

        binary result(current_size, byte_span{blocks_});
        result.shift_left(start);
        result.resize(length);
        return result;
    }

    [[nodiscard]] constexpr
    bool is_prefix_of(byte_span field) const {
        binary const truncated_prefix(size(), field);
        return *this == truncated_prefix;
    }

    [[nodiscard]] constexpr
    bool is_prefix_of(uint32_t field) const {
        return is_prefix_of(byte_span{to_little_endian(field)});
    }

    [[nodiscard]] constexpr
    bool is_prefix_of(binary const& field) const {
        return is_prefix_of(byte_span{field.blocks()});
    }

    constexpr binary& operator=(binary const& x) = default;

    /// Byte-lex over (`blocks_`, `final_block_excess_`). The class
    /// invariant guarantees unused trailing bits are zero (every
    /// mutator ends in `resize()`, which masks them), so member-wise
    /// equality matches the bit-level equality the previous manual
    /// `operator==` computed. The strong ordering `<=>` produces is
    /// canonical and consistent with `==`, but it is NOT the same as
    /// the previous string-lex order via `encoded()`: two values with
    /// the same block bytes but different bit-length (e.g. `"1"` and
    /// `"10000000"` both blocks `{0x80}` with `final_block_excess_`
    /// 7 and 0) are ordered by their excess field, so the shorter
    /// one is greater under the new order and less under the old.
    /// No caller in-tree relies on the specific pre-refactor order —
    /// map / set keys and sorted output only need a total order
    /// consistent with equality, which this satisfies.
    [[nodiscard]]
    friend auto operator<=>(binary const&, binary const&) = default;

private:
    [[nodiscard]] static constexpr
    bool is_base2(std::string_view text) noexcept {
        for (auto const character : text) {
            if (character != '0' && character != '1') {
                return false;
            }
        }
        return true;
    }

    data_chunk blocks_;
    uint8_t final_block_excess_{0};
};

} // namespace kth

namespace std {

template <>
struct hash<kth::binary> {
    size_t operator()(kth::binary const& x) const {
        // Hash raw blocks + trailing-bit metadata directly — no
        // base2 string allocation. Under the class invariant unused
        // bits are zero, so equivalent values hash the same.
        auto const& blocks = x.blocks();
        auto h = std::hash<std::string_view>()(
            std::string_view(reinterpret_cast<char const*>(blocks.data()), blocks.size()));
        h ^= std::hash<size_t>()(x.size()) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace std

#endif
