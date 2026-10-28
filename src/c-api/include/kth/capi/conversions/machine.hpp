// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

// Script machine enums (kth/domain/machine).

#ifndef KTH_CAPI_CONVERSIONS_MACHINE_HPP_
#define KTH_CAPI_CONVERSIONS_MACHINE_HPP_

#include <kth/capi/detail/enum_cast.hpp>

#include <kth/capi/chain/opcode.h>
#include <kth/capi/chain/script_flags.h>
#include <kth/capi/chain/script_pattern.h>

#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/domain/machine/script_pattern.hpp>

namespace kth {

// Every byte 0..255 is a valid opcode value; there is no "out of range" the
// way a discrete enum has.
inline constexpr kth::domain::machine::opcode opcode_to_cpp(kth_opcode_t x) {
    return ecast<kth::domain::machine::opcode>(x);
}

inline constexpr kth_opcode_t opcode_to_c(kth::domain::machine::opcode x) {
    return ecast<kth_opcode_t>(x);
}

// `script_flags` is a bitfield: any bit pattern is a meaningful (if
// possibly unknown) value and the interpreter ignores bits it does not
// recognise, so validating against a fixed set of cases would be wrong.
inline constexpr kth::domain::machine::script_flags script_flags_to_cpp(kth_script_flags_t x) {
    return ecast<kth::domain::machine::script_flags>(x);
}

inline constexpr kth_script_flags_t script_flags_to_c(kth::domain::machine::script_flags x) {
    return ecast<kth_script_flags_t>(x);
}

// Out-of-range C values map to `non_standard`.
inline constexpr kth::domain::machine::script_pattern script_pattern_to_cpp(kth_script_pattern_t x) {
    switch (x) {
        case kth_script_pattern_null_data:
            return kth::domain::machine::script_pattern::null_data;
        case kth_script_pattern_pay_to_multisig:
            return kth::domain::machine::script_pattern::pay_to_multisig;
        case kth_script_pattern_pay_to_public_key:
            return kth::domain::machine::script_pattern::pay_to_public_key;
        case kth_script_pattern_pay_to_public_key_hash:
            return kth::domain::machine::script_pattern::pay_to_public_key_hash;
        case kth_script_pattern_pay_to_script_hash:
            return kth::domain::machine::script_pattern::pay_to_script_hash;
        case kth_script_pattern_pay_to_script_hash_32:
            return kth::domain::machine::script_pattern::pay_to_script_hash_32;
        case kth_script_pattern_pay_to_script:
            return kth::domain::machine::script_pattern::pay_to_script;
        case kth_script_pattern_sign_multisig:
            return kth::domain::machine::script_pattern::sign_multisig;
        case kth_script_pattern_sign_public_key:
            return kth::domain::machine::script_pattern::sign_public_key;
        case kth_script_pattern_sign_public_key_hash:
            return kth::domain::machine::script_pattern::sign_public_key_hash;
        case kth_script_pattern_sign_script_hash:
            return kth::domain::machine::script_pattern::sign_script_hash;
        case kth_script_pattern_witness_reservation:
            return kth::domain::machine::script_pattern::witness_reservation;
        default:
        case kth_script_pattern_non_standard:
            return kth::domain::machine::script_pattern::non_standard;
    }
}

inline constexpr kth_script_pattern_t script_pattern_to_c(kth::domain::machine::script_pattern x) {
    return ecast<kth_script_pattern_t>(x);
}

} // namespace kth

#endif /* KTH_CAPI_CONVERSIONS_MACHINE_HPP_ */
