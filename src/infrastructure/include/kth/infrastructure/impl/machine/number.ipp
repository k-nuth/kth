// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_MACHINE_NUMBER_IPP
#define KTH_INFRASTUCTURE_MACHINE_NUMBER_IPP

#include <cstdint>
#include <cstdlib>
#include <stdexcept>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/limits.hpp>

namespace kth::infrastructure::machine {

static uint64_t const negative_bit = number::negative_mask;
static uint64_t const unsigned_max_int64 = kth::max_int64;
static uint64_t const absolute_min_int64 = kth::min_int64;

inline
bool is_negative(data_chunk const& data) {
    return (data.back() & number::negative_mask) != 0;
}

// proteded
inline
number::number(int64_t value)
    : value_(value)
{}

inline //static
std::expected<number, error::error_code_t> number::from_int(int64_t value) {
    if (value == kth::min_int64) {
        return std::unexpected(error::out_of_range);
    }
    return number(value);
}


// Properties
//-----------------------------------------------------------------------------

// Return true if the value is valid given the maximum size.
inline
bool number::valid(size_t max_size) {
    return data().size() <= max_size;
}

// The data is interpreted as little-endian.
inline
bool number::set_data(data_chunk const& data, size_t max_size) {
    if (data.size() > max_size) {
        return false;
    }

    value_ = 0;

    if (data.empty()) {
        return true;
    }

    // This is "from little endian" with a variable buffer.
    for (size_t i = 0; i != data.size(); ++i) {
        value_ |= static_cast<int64_t>(data[i]) << (8 * i);
    }

    if (is_negative(data)) {
        auto const last_shift = 8 * (data.size() - 1);
        auto const mask = ~(negative_bit << last_shift);
        value_ = -1 * (static_cast<int64_t>(value_ & mask));
    }

    return true;
}

// The result is little-endian.
inline
data_chunk number::data() const {
    if (value_ == 0) {
        return {};
    }

    data_chunk data;
    bool const set_negative = value_ < 0;
    uint64_t absolute = set_negative ? -value_ : value_;

    // This is "to little endian" with a minimal buffer.
    while (absolute != 0) {
        data.push_back(static_cast<uint8_t>(absolute));
        absolute >>= 8;
    }

    auto const negative_bit_set = is_negative(data);

    // If the most significant byte is >= 0x80 and the value is negative,
    // push a new 0x80 byte that will be popped off when converting to
    // an integral.
    if (negative_bit_set && set_negative) {
        data.push_back(number::negative_mask);

    // If the most significant byte is >= 0x80 and the value is positive,
    // push a new zero-byte to make the significant byte < 0x80 again.
    } else if (negative_bit_set) {
        data.push_back(0);

    // If the most significant byte is < 0x80 and the value is negative,
    // add 0x80 to it, since it will be subtracted and interpreted as
    // a negative when converting to an integral.
    } else if (set_negative) {
        data.back() |= number::negative_mask;
    }

    return data;
}

inline
int32_t number::int32() const {
    return domain_constrain<int32_t>(value_);
}

inline
int64_t number::int64() const {
    return value_;
}

// Stack Helpers
//-----------------------------------------------------------------------------

inline
bool number::is_true() const {
    return value_ != 0;
}

inline
bool number::is_false() const {
    return value_ == 0;
}

// Operators
//-----------------------------------------------------------------------------

inline
bool number::operator>(int64_t value) const {
    return value_ > value;
}

inline
bool number::operator<(int64_t value) const {
    return value_ < value;
}

inline
bool number::operator>=(int64_t value) const {
    return value_ >= value;
}

inline
bool number::operator<=(int64_t value) const {
    return value_ <= value;
}

inline
bool number::operator==(int64_t value) const {
    return value_ == value;
}

inline
bool number::operator!=(int64_t value) const {
    return value_ != value;
}

inline
bool number::operator>(number const& x) const {
    return operator>(x.value_);
}

inline
bool number::operator<(number const& x) const {
    return operator<(x.value_);
}

inline
bool number::operator>=(number const& x) const {
    return operator>=(x.value_);
}

inline
bool number::operator<=(number const& x) const {
    return operator<=(x.value_);
}

inline
bool number::operator==(number const& x) const {
    return operator==(x.value_);
}

inline
bool number::operator!=(number const& x) const {
    return operator!=(x.value_);
}

inline
number number::operator+(int64_t value) const {
    KTH_ASSERT_MSG(value == 0 ||
        (value > 0 && value_ <= max_int64 - value) ||
        (value < 0 && value_ >= min_int64 - value), "overflow");

    return number(value_ + value);
}

inline
number number::operator-(int64_t value) const {
    KTH_ASSERT_MSG(value == 0 ||
        (value > 0 && value_ >= min_int64 + value) ||
        (value < 0 && value_ <= max_int64 + value), "underflow");

    return number(value_ - value);
}

inline
number number::operator+(number const& x) const {
    return operator+(x.value_);
}

inline
number number::operator-(number const& x) const {
    return operator-(x.value_);
}

inline
number number::operator+() const {
    return *this;
}

inline
number number::operator-() const {
    KTH_ASSERT_MSG(value_ != min_int64, "out of range");

    return number(-value_);
}

inline
number number::operator*(number const& x) const {
    return number(value_ * x.value_);
}

inline
number number::operator/(number const& x) const {
    KTH_ASSERT_MSG(x.value_ != 0, "division by zero");
    return number(value_ / x.value_);
}

inline
number number::operator%(number const& x) const {
    KTH_ASSERT_MSG(x.value_ != 0, "division by zero");
    return number(value_ % x.value_);
}

inline
number& number::operator+=(number const& x) {
    return operator+=(x.value_);
}

inline
number& number::operator-=(number const& x) {
    return operator-=(x.value_);
}

inline
number& number::operator+=(int64_t value) {
    KTH_ASSERT_MSG(value == 0 ||
        (value > 0 && value_ <= max_int64 - value) ||
        (value < 0 && value_ >= min_int64 - value), "overflow");

    value_ += value;
    return *this;
}

inline
number& number::operator-=(int64_t value) {
    KTH_ASSERT_MSG(value == 0 ||
        (value > 0 && value_ >= min_int64 + value) ||
        (value < 0 && value_ <= max_int64 + value), "underflow");

    value_ -= value;
    return *this;
}

inline
number& number::operator*=(number const& x) {
    value_ *= x.value_;
    return *this;
}

inline
number& number::operator/=(number const& x) {
    KTH_ASSERT_MSG(x.value_ != 0, "division by zero");
    value_ /= x.value_;
    return *this;
}

inline
number& number::operator%=(number const& x) {
    KTH_ASSERT_MSG(x.value_ != 0, "division by zero");
    value_ %= x.value_;
    return *this;
}

inline
bool number::safe_add(number const& x) {
    int64_t val;
    bool const res = __builtin_add_overflow(value_, x.value_, &val);
    if (res) {
        return false;
    }
    value_ = val;
    return true;
}

inline
bool number::safe_add(int64_t x) {
    int64_t val;
    bool const res = __builtin_add_overflow(value_, x, &val);
    if (res) {
        return false;
    }
    value_ = val;
    return true;
}

inline
bool number::safe_sub(number const& x) {
    int64_t val;
    bool const res = __builtin_sub_overflow(value_, x.value_, &val);
    if (res) {
        return false;
    }
    value_ = val;
    return true;
}

inline
bool number::safe_sub(int64_t x) {
    int64_t val;
    bool const res = __builtin_sub_overflow(value_, x, &val);
    if (res) {
        return false;
    }
    value_ = val;
    return true;
}

inline
bool number::safe_mul(number const& x) {
    int64_t val;
    bool const res = __builtin_mul_overflow(value_, x.value_, &val);
    if (res) {
        return false;
    }
    value_ = val;
    return true;
}

inline
bool number::safe_mul(int64_t x) {
    int64_t val;
    bool const res = __builtin_mul_overflow(value_, x, &val);
    if (res) {
        return false;
    }
    value_ = val;
    return true;
}

// static
inline
std::expected<number, code> number::safe_add(number const& x, number const& y) {
    int64_t val;
    bool const res = __builtin_add_overflow(x.value_, y.value_, &val);
    if (res) {
        return std::unexpected(error::overflow);
    }
    return number(val);
}

// static
inline
std::expected<number, code> number::safe_sub(number const& x, number const& y) {
    int64_t val;
    bool const res = __builtin_sub_overflow(x.value_, y.value_, &val);
    if (res) {
        return std::unexpected(error::overflow);
    }
    return number(val);
}

inline
std::expected<number, code> number::safe_mul(number const& x, number const& y) {
    int64_t val;
    bool const res = __builtin_mul_overflow(x.value_, y.value_, &val);
    if (res) {
        return std::unexpected(error::overflow);
    }
    return number(val);
}



// Minimally encoded
//-----------------------------------------------------------------------------

// bool ScriptNumEncoding::IsMinimallyEncoded(const std::vector<uint8_t> &vch, size_t maxIntegerSize) {
//     if (vch.size() > maxIntegerSize) {
//         return false;
//     }

//     if (vch.size() > 0) {
//         // Check that the number is encoded with the minimum possible number
//         // of bytes.
//         //
//         // If the most-significant-byte - excluding the sign bit - is zero
//         // then we're not minimal. Note how this test also rejects the
//         // negative-zero encoding, 0x80.
//         if ((vch.back() & 0x7f) == 0) {
//             // One exception: if there's more than one byte and the most
//             // significant bit of the second-most-significant-byte is set it
//             // would conflict with the sign bit. An example of this case is
//             // +-255, which encode to 0xff00 and 0xff80 respectively.
//             // (big-endian).
//             if (vch.size() <= 1 || (vch[vch.size() - 2] & 0x80) == 0) {
//                 return false;
//             }
//         }
//     }

//     return true;
// }

inline
bool number::is_minimally_encoded(data_chunk const& data, size_t max_integer_size) {
    if (data.size() > max_integer_size) {
        return false;
    }

    return data.empty() ||
        (data.back() & 0x7f) != 0 ||
        (data.size() > 1 && (data[data.size() - 2] & 0x80) != 0);


    // if ( ! data.empty()) {
    //     // Check if the number is encoded with the minimum possible number of bytes.
    //     if ((data.back() & 0x7f) == 0) {
    //         if (data.size() <= 1 || (data[data.size() - 2] & 0x80) == 0) {
    //             return false;
    //         }
    //     }
    // }

    // return true;
}



// bool ScriptNumEncoding::MinimallyEncode(std::vector<uint8_t> &data) {
//     if (data.size() == 0) {
//         return false;
//     }

//     // If the last byte is not 0x00 or 0x80, we are minimally encoded.
//     uint8_t const last = data.back();
//     if (last & 0x7f) {
//         return false;
//     }

//     // If the script is one byte long, then we have a zero, which encodes as an
//     // empty array.
//     if (data.size() == 1) {
//         data = {};
//         return true;
//     }

//     // If the next byte has it sign bit set, then we are minimaly encoded.
//     if (data[data.size() - 2] & 0x80) {
//         return false;
//     }

//     // We are not minimally encoded, we need to figure out how much to trim.
//     for (size_t i = data.size() - 1; i > 0; i--) {
//         // We found a non zero byte, time to encode.
//         if (data[i - 1] != 0) {
//             if (data[i - 1] & 0x80) {
//                 // We found a byte with it sign bit set so we need one more
//                 // byte.
//                 data[i++] = last;
//             } else {
//                 // the sign bit is clear, we can use it.
//                 data[i - 1] |= last;
//             }

//             data.resize(i);
//             return true;
//         }
//     }

//     // If we the whole thing is zeros, then we have a zero.
//     data = {};
//     return true;
// }

inline
bool number::minimally_encode(data_chunk& data) {
    if (data.empty()) {
        return false;
    }

    uint8_t const last = data.back();
    if ((last & 0x7f) != 0) {
        return false;  // Already minimally encoded.
    }

    if (data.size() == 1) {
        data.clear();
        return true;  // Zero, encoded as an empty array.
    }

    if ((data[data.size() - 2] & 0x80) != 0) {
        return false;  // Already minimally encoded.
    }

    for (size_t i = data.size() - 1; i > 0; --i) {
        if (data[i - 1] != 0) {
            if ((data[i - 1] & 0x80) != 0) {
                data[i++] = last;  // Need one more byte.
            } else {
                data[i - 1] |= last;  // Use the available sign bit.
            }
            data.resize(i);
            return true;
        }
    }

    data.clear();  // All bytes are zeros, represents the number zero.
    return true;
}


} // namespace kth::infrastructure::machine

#endif
