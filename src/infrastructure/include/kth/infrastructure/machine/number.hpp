// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_MACHINE_NUMBER_HPP
#define KTH_INFRASTUCTURE_MACHINE_NUMBER_HPP

#include <cstddef>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <expected>

namespace kth::infrastructure::machine {

/**
 * Numeric opcodes (OP_1ADD, etc) are restricted to operating on
 * 4-byte integers. The semantics are subtle, though: operands must be
 * in the range [-2^31 +1...2^31 -1], but results may overflow (and are
 * valid as long as they are not used in a subsequent numeric operation).
 *
 * number enforces those semantics by storing results as
 * an int64 and allowing out-of-range values to be returned as a vector of
 * bytes but throwing an exception if arithmetic is done or the result is
 * interpreted as an integer.
 */
struct KI_API number {
    static constexpr uint8_t positive_0 = 0;
    static constexpr uint8_t positive_1 = 1;
    static constexpr uint8_t positive_2 = 2;
    static constexpr uint8_t positive_3 = 3;
    static constexpr uint8_t positive_4 = 4;
    static constexpr uint8_t positive_5 = 5;
    static constexpr uint8_t positive_6 = 6;
    static constexpr uint8_t positive_7 = 7;
    static constexpr uint8_t positive_8 = 8;
    static constexpr uint8_t positive_9 = 9;
    static constexpr uint8_t positive_10 = 10;
    static constexpr uint8_t positive_11 = 11;
    static constexpr uint8_t positive_12 = 12;
    static constexpr uint8_t positive_13 = 13;
    static constexpr uint8_t positive_14 = 14;
    static constexpr uint8_t positive_15 = 15;
    static constexpr uint8_t positive_16 = 16;

    static constexpr uint8_t negative_mask = 0x80;
    static constexpr uint8_t negative_1 = negative_mask | positive_1;
    static constexpr uint8_t negative_0 = negative_mask | positive_0;

    /// Construct with zero value.
    number() = default;

    static
    std::expected<number, error::error_code_t> from_int(int64_t value);

    /// Return true if the value is valid given the maximum size.
    bool valid(size_t max_size);

    /// Replace the value derived from a byte vector with LSB first ordering.
    bool set_data(data_chunk const& data, size_t max_size);

    // Properties
    //-------------------------------------------------------------------------

    /// Return the value as a byte vector with LSB first ordering.
    data_chunk data() const;

    /// Return the value bounded by the limits of int32.
    int32_t int32() const;

    /// Return the unbounded value.
    int64_t int64() const;

    // Stack Helpers
    //-------------------------------------------------------------------------

    /// Return value as stack boolean (nonzero is true).
    bool is_true() const;

    /// Return value as stack boolean (zero is false).
    bool is_false() const;

    // Operators
    //-------------------------------------------------------------------------

    //*************************************************************************
    // CONSENSUS: script::number implements consensus critical overflow
    // behavior for all operators, specifically [-, +, +=, -=].
    //*************************************************************************

    bool operator>(int64_t value) const;
    bool operator<(int64_t value) const;
    bool operator>=(int64_t value) const;
    bool operator<=(int64_t value) const;
    bool operator==(int64_t value) const;
    bool operator!=(int64_t value) const;

    bool operator>(number const& x) const;
    bool operator<(number const& x) const;
    bool operator>=(number const& x) const;
    bool operator<=(number const& x) const;
    bool operator==(number const& x) const;
    bool operator!=(number const& x) const;

    number operator+() const;
    number operator-() const;
    number operator+(int64_t value) const;
    number operator-(int64_t value) const;
    number operator+(number const& x) const;
    number operator-(number const& x) const;
    number operator*(number const& x) const;
    number operator/(number const& x) const;
    number operator%(number const& x) const;

    number& operator+=(int64_t value);
    number& operator-=(int64_t value);
    number& operator+=(number const& x);
    number& operator-=(number const& x);
    number& operator*=(number const& x);
    number& operator/=(number const& x);
    number& operator%=(number const& x);

    // Safe arithmetic
    //-------------------------------------------------------------------------
    bool safe_add(number const& x);
    bool safe_add(int64_t x);
    bool safe_sub(number const& x);
    bool safe_sub(int64_t x);
    bool safe_mul(number const& x);
    bool safe_mul(int64_t x);

    static
    std::expected<number, code> safe_add(number const& x, number const& y);

    static
    std::expected<number, code> safe_sub(number const& x, number const& y);

    static
    std::expected<number, code> safe_mul(number const& x, number const& y);


    // Minimally encoded
    //-------------------------------------------------------------------------
    static
    bool is_minimally_encoded(data_chunk const& data, size_t max_integer_size);

    static
    bool minimally_encode(data_chunk& data);

private:
    /// Construct with specified value.
    explicit
    number(int64_t value);

    int64_t value_ = 0;
};

} // namespace kth::infrastructure::machine


#include <kth/infrastructure/impl/machine/number.ipp>

#endif
