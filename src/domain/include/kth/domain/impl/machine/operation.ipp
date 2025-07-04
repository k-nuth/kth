// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_OPERATION_IPP
#define KTH_DOMAIN_MACHINE_OPERATION_IPP

#include <cstdint>

#include <kth/domain/machine/opcode.hpp>
#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>

using number = kth::infrastructure::machine::number;

namespace kth::domain::machine {

inline
operation::operation(data_chunk&& uncoded, bool minimal)
    : code_(opcode_from_data(uncoded, minimal))
    , data_(std::move(uncoded))
    , valid_( ! is_oversized(max_push_data_size_legacy))   //TODO: max_push_data_size_legacy change on 2025 May
{
    if ( ! valid_) {
        reset();
    }

    // Revert data if opcode_from_data produced a numeric encoding.
    if (minimal && !is_payload(code_)) {
        data_.clear();
        data_.shrink_to_fit();
    }
}

inline
operation::operation(data_chunk const& uncoded, bool minimal)
    : code_(opcode_from_data(uncoded, minimal))
    , data_(uncoded)
    , valid_( ! is_oversized(max_push_data_size_legacy))   //TODO: max_push_data_size_legacy change on 2025 May
{
    if ( ! valid_) {
        reset();
    }

    // Revert data if opcode_from_data produced a numeric encoding.
    if (minimal && !is_payload(code_)) {
        data_.clear();
        data_.shrink_to_fit();
    }
}

inline
operation::operation(opcode code)
    : code_(code), valid_(true)
{}

// // protected
// protected
inline
operation::operation(opcode code, data_chunk&& data, bool valid)
    : code_(code), data_(std::move(data)), valid_(valid)
{}

// protected
inline
operation::operation(opcode code, data_chunk const& data, bool valid)
    : code_(code), data_(data), valid_(valid)
{}

// Operators.
//-----------------------------------------------------------------------------

inline
bool operation::operator==(operation const& x) const {
    return (code_ == x.code_) && (data_ == x.data_);
}

inline
bool operation::operator!=(operation const& x) const {
    return !(*this == x);
}

// Properties (size, accessors, cache).
//-----------------------------------------------------------------------------

// TODO(legacy): consolidate with message implementation into common math utility.
// static
// size_t variable_uint_size(uint64_t value)
// {
//     if (value < 0xfd)
//         return 1;
//     else if (value <= 0xffff)
//         return 3;
//     else if (value <= 0xffffffff)
//         return 5;
//     else
//         return 9;
// }

inline
size_t operation::serialized_size() const {
    static constexpr auto op_size = sizeof(uint8_t);
    auto const size = data_.size();

    switch (code_) {
        case opcode::push_one_size:
            return op_size + sizeof(uint8_t) + size;
        case opcode::push_two_size:
            return op_size + sizeof(uint16_t) + size;
        case opcode::push_four_size:
            return op_size + sizeof(uint32_t) + size;
        default:
            return op_size + size;
    }
}

inline
opcode operation::code() const {
    return code_;
}

inline
data_chunk const& operation::data() const {
    return data_;
}

// Utilities.
//-----------------------------------------------------------------------------

// private
//*****************************************************************************
// CONSENSUS: op data size is limited to 520 bytes, which requires no more
// than two bytes to encode. However the four byte encoding can represent
// a value of any size, so remains valid despite the data size limit.
//*****************************************************************************
// template <typename R>
// inline
// uint32_t operation::read_data_size(opcode code, R& source) {
//     constexpr auto op_75 = static_cast<uint8_t>(opcode::push_size_75);

//     switch (code) {
//         case opcode::push_one_size:
//             return source.read_byte();
//         case opcode::push_two_size:
//             return source.read_2_bytes_little_endian();
//         case opcode::push_four_size:
//             return source.read_4_bytes_little_endian();
//         default:
//             auto const byte = static_cast<uint8_t>(code);
//             return byte <= op_75 ? byte : 0;
//     }
// }

inline
expect<uint32_t> operation::read_data_size(opcode code, byte_reader& reader) {
    constexpr auto op_75 = static_cast<uint8_t>(opcode::push_size_75);

    switch (code) {
        case opcode::push_one_size:
            return reader.read_byte();
        case opcode::push_two_size:
            return reader.read_little_endian<uint16_t>();
        case opcode::push_four_size:
            return reader.read_little_endian<uint32_t>();
        default:
            auto const byte = uint8_t(code);
            return byte <= op_75 ? byte : 0;
    }
}

inline
opcode operation::opcode_from_size(size_t size) {
    KTH_ASSERT(size <= max_uint32);
    constexpr auto op_75 = static_cast<uint8_t>(opcode::push_size_75);

    if (size <= op_75) {
        return static_cast<opcode>(size);
    }

    if (size <= max_uint8) {
        return opcode::push_one_size;
    }

    if (size <= max_uint16) {
        return opcode::push_two_size;
    }

    return opcode::push_four_size;
}

inline
opcode operation::minimal_opcode_from_data(data_chunk const& data) {
    auto const size = data.size();

    if (size == 1) {
        auto const value = data.front();

        if (value == number::negative_1) {
            return opcode::push_negative_1;
        }

        if (value == number::positive_0) {
            return opcode::push_size_0;
        }

        if (value >= number::positive_1 && value <= number::positive_16) {
            return opcode_from_positive(value);
        }
    }

    // Nominal encoding is minimal for multiple bytes and non-numeric values.
    return opcode_from_size(size);
}

inline
opcode operation::nominal_opcode_from_data(data_chunk const& data) {
    return opcode_from_size(data.size());
}

inline
opcode operation::opcode_from_data(data_chunk const& data, bool minimal) {
    return minimal ?
        minimal_opcode_from_data(data) :
        nominal_opcode_from_data(data);
}

inline
opcode operation::opcode_from_positive(uint8_t value) {
    KTH_ASSERT(value >= number::positive_1);
    KTH_ASSERT(value <= number::positive_16);
    constexpr auto op_81 = static_cast<uint8_t>(opcode::push_positive_1);
    return static_cast<opcode>(value + op_81 - 1);
}

inline
uint8_t operation::opcode_to_positive(opcode code) {
    KTH_ASSERT(is_positive(code));
    constexpr auto op_81 = static_cast<uint8_t>(opcode::push_positive_1);
    return static_cast<uint8_t>(code) - op_81 + 1;
}

// opcode: [0..79, 81..96]
inline
bool operation::is_push(opcode code) {
    constexpr auto op_80 = static_cast<uint8_t>(opcode::reserved_80);
    constexpr auto op_96 = static_cast<uint8_t>(opcode::push_positive_16);
    auto const value = static_cast<uint8_t>(code);
    return value <= op_96 && value != op_80;
}

// opcode: [1..78]
inline
bool operation::is_payload(opcode code) {
    constexpr auto op_1 = static_cast<uint8_t>(opcode::push_size_1);
    constexpr auto op_78 = static_cast<uint8_t>(opcode::push_four_size);
    auto const value = static_cast<uint8_t>(code);
    return value >= op_1 && value <= op_78;
}

// opcode: [97..255]
inline
bool operation::is_counted(opcode code) {
    constexpr auto op_97 = static_cast<uint8_t>(opcode::nop);
    auto const value = static_cast<uint8_t>(code);
    return value >= op_97;
}

// stack: [[], 1..16]
inline
bool operation::is_version(opcode code) {
    return code == opcode::push_size_0 || is_positive(code);
}

// stack: [-1, 1..16]
inline
bool operation::is_numeric(opcode code) {
    return is_positive(code) || code == opcode::push_negative_1;
}

// stack: [1..16]
inline
bool operation::is_positive(opcode code) {
    constexpr auto op_81 = static_cast<uint8_t>(opcode::push_positive_1);
    constexpr auto op_96 = static_cast<uint8_t>(opcode::push_positive_16);
    auto const value = static_cast<uint8_t>(code);
    return value >= op_81 && value <= op_96;
}

// opcode: [80, 98, 137, 138, 186..255]
inline
bool operation::is_reserved(opcode code) {
    constexpr auto op_212 = static_cast<uint8_t>(opcode::reserved_212);
    constexpr auto op_255 = static_cast<uint8_t>(opcode::reserved_255);

    switch (code) {
        case opcode::reserved_80:
        case opcode::reserved_98:
        case opcode::reserved_137:
        case opcode::reserved_138:
            return true;
        default:
            auto const value = uint8_t(code);
            return value >= op_212 && value <= op_255;
    }
}

//*****************************************************************************
// CONSENSUS: the codes VERIF and VERNOTIF are in the conditional range yet are
// not handled. As a result satoshi always processes them in the op switch.
// This causes them to always fail as unhandled. It is misleading that the
// satoshi test cases refer to these as reserved codes. These two codes behave
// exactly as the explicitly disabled codes. On the other hand VER is not within
// the satoshi conditional range test so it is in fact reserved. Presumably
// this was an unintended consequence of range testing enums.
//*****************************************************************************
inline
bool operation::is_disabled(opcode code, uint32_t active_forks) {
    // SCRIPT_64_BIT_INTEGERS = (1U << 24),
    constexpr auto script_64_bit_integers = 1U << 24;   // the flag is repeated, it is in Consensus lib.
    switch (code) {
        case opcode::disabled_invert:
        case opcode::disabled_mul2:
        case opcode::disabled_div2:
        case opcode::disabled_lshift:
        case opcode::disabled_rshift:
        case opcode::disabled_verif:
        case opcode::disabled_vernotif:
            return true;
        case opcode::mul:
            return ! is_enabled(active_forks, rule_fork::bch_gauss);
        case opcode::div:
        case opcode::mod:
        case opcode::and_:
        case opcode::or_:
        case opcode::xor_:
            return ! is_enabled(active_forks, rule_fork::bch_pythagoras);
        default:
            return false;
    }
}

// 2025 - Still disabled opcodes
// static bool IsOpcodeDisabled(opcodetype opcode, uint32_t flags) {
//     switch (opcode) {
//         case OP_INVERT:
//         case OP_2MUL:
//         case OP_2DIV:
//         case OP_LSHIFT:
//         case OP_RSHIFT:
//             // Disabled opcodes.
//             return true;
//         case OP_MUL:
//             return (flags & SCRIPT_64_BIT_INTEGERS) == 0;
//         default:
//             break;
//     }
//     return false;
// }

//*****************************************************************************
// CONSENSUS: in order to properly treat VERIF and VERNOTIF as disabled (see
// is_disabled comments) those codes must not be included here.
//*****************************************************************************
inline
bool operation::is_conditional(opcode code) {
    switch (code) {
        case opcode::if_:
        case opcode::notif:
        case opcode::else_:
        case opcode::endif:
            return true;
        default:
            return false;
    }
}

//*****************************************************************************
// CONSENSUS: this test explicitly includes the satoshi 'reserved' code.
// This affects the operation count in p2sh script evaluation.
// Presumably this was an unintended consequence of range testing enums.
//*****************************************************************************
// opcode: [0..96]
inline
bool operation::is_relaxed_push(opcode code) {
    constexpr auto op_96 = static_cast<uint8_t>(opcode::push_positive_16);
    auto const value = static_cast<uint8_t>(code);
    return value <= op_96;
}

inline
bool operation::is_push() const {
    return is_push(code_);
}

inline
bool operation::is_counted() const {
    return is_counted(code_);
}

inline
bool operation::is_version() const {
    return is_version(code_);
}

inline
bool operation::is_positive() const {
    return is_positive(code_);
}

inline
bool operation::is_disabled(uint32_t active_forks) const {
    return is_disabled(code_, active_forks);
}

inline
bool operation::is_conditional() const {
    return is_conditional(code_);
}

inline
bool operation::is_relaxed_push() const {
    return is_relaxed_push(code_);
}

inline
bool operation::is_oversized(size_t max_size) const {
    return data_.size() > max_size;
}

inline
bool operation::is_minimal_push() const {
    return code_ == minimal_opcode_from_data(data_);
}

inline
bool operation::is_nominal_push() const {
    return code_ == nominal_opcode_from_data(data_);
}

} // namespace kth::domain::machine

#endif // KTH_DOMAIN_MACHINE_OPERATION_IPP
