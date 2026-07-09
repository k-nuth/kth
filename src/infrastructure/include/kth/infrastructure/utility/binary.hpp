// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_BINARY_HPP
#define KTH_INFRASTRUCTURE_BINARY_HPP

#include <bit>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/data.hpp>

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
    [[nodiscard]] static
    std::expected<binary, kth::code> parse_from(std::string_view bit_string);

    binary() = default;

    binary(size_type size, uint32_t number);
    binary(size_type size, byte_span blocks);

    void resize(size_type size);
    [[nodiscard]] bool operator[](size_type index) const;
    [[nodiscard]] data_chunk const& blocks() const noexcept;

    /// Base2 encoding used by `fmt::formatter<binary>`.
    [[nodiscard]] std::string to_string() const;

    /// size in bits
    [[nodiscard]] size_type size() const noexcept;
    void append(binary const& post);
    void prepend(binary const& prior);
    void shift_left(size_type distance);
    void shift_right(size_type distance);
    [[nodiscard]] binary substring(size_type start, size_type length) const;

    [[nodiscard]] bool is_prefix_of(byte_span field) const;
    [[nodiscard]] bool is_prefix_of(uint32_t field) const;
    [[nodiscard]] bool is_prefix_of(binary const& field) const;

    binary& operator=(binary const& x) = default;

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
    static
    bool is_base2(std::string_view text) noexcept;

    static
    uint8_t shift_block_right(uint8_t next, uint8_t current, uint8_t prior, size_type original_offset, size_type intended_offset);

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
