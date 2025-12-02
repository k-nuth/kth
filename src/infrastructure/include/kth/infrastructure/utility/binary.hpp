// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_BINARY_HPP
#define KTH_INFRASTRUCTURE_BINARY_HPP

#include <bit>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

struct KI_API binary {
    using block = uint8_t;
    using size_type = std::size_t;

    static
    constexpr size_type bits_per_block = byte_bits;

    [[nodiscard]] static constexpr
    size_type blocks_size(size_type bit_size) noexcept;

    [[nodiscard]] static
    bool is_base2(std::string_view text) noexcept;

    binary() = default;

    explicit
    binary(std::string_view bit_string);

    binary(size_type size, uint32_t number);
    binary(size_type size, byte_span blocks);

    void resize(size_type size);
    [[nodiscard]] bool operator[](size_type index) const;
    [[nodiscard]] data_chunk const& blocks() const noexcept;
    [[nodiscard]] std::string encoded() const;

    /// size in bits
    [[nodiscard]] size_type size() const noexcept;
    void append(binary const& post);
    void prepend(binary const& prior);
    void shift_left(size_type distance);
    void shift_right(size_type distance);
    [[nodiscard]] binary substring(size_type start, size_type length=max_size_t) const;

    [[nodiscard]] bool is_prefix_of(byte_span field) const;
    [[nodiscard]] bool is_prefix_of(uint32_t field) const;
    [[nodiscard]] bool is_prefix_of(binary const& field) const;

    binary& operator=(binary const& x);
    [[nodiscard]] bool operator==(binary const& x) const;
    [[nodiscard]] bool operator!=(binary const& x) const;
    [[nodiscard]] bool operator<(binary const& x) const;

    friend
    std::istream& operator>>(std::istream& in, binary& to);

    friend
    std::ostream& operator<<(std::ostream& out, binary const& of);

private:
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
        return std::hash<std::string>()(x.encoded());
    }
};

} // namespace std

#endif
