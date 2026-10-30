// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

// Script machine enums (kth/domain/machine).

#ifndef KTH_CAPI_CONVERSIONS_MACHINE_HPP_
#define KTH_CAPI_CONVERSIONS_MACHINE_HPP_

#include <type_traits>

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
inline constexpr kth::domain::machine::opcode opcode_to_cpp(kth_opcode_t x) noexcept {
    return ecast<kth::domain::machine::opcode>(x);
}

inline constexpr kth_opcode_t opcode_to_c(kth::domain::machine::opcode x) noexcept {
    return ecast<kth_opcode_t>(x);
}

// Numeric ABI: every named enumerator converts to its counterpart, so the C and
// C++ values cannot drift apart while the names keep matching.
static_assert(sizeof(std::underlying_type_t<kth::domain::machine::opcode>) <= sizeof(kth_opcode_t));
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_0) == kth_opcode_push_size_0);
static_assert(opcode_to_cpp(kth_opcode_push_size_0) == kth::domain::machine::opcode::push_size_0);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_1) == kth_opcode_push_size_1);
static_assert(opcode_to_cpp(kth_opcode_push_size_1) == kth::domain::machine::opcode::push_size_1);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_2) == kth_opcode_push_size_2);
static_assert(opcode_to_cpp(kth_opcode_push_size_2) == kth::domain::machine::opcode::push_size_2);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_3) == kth_opcode_push_size_3);
static_assert(opcode_to_cpp(kth_opcode_push_size_3) == kth::domain::machine::opcode::push_size_3);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_4) == kth_opcode_push_size_4);
static_assert(opcode_to_cpp(kth_opcode_push_size_4) == kth::domain::machine::opcode::push_size_4);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_5) == kth_opcode_push_size_5);
static_assert(opcode_to_cpp(kth_opcode_push_size_5) == kth::domain::machine::opcode::push_size_5);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_6) == kth_opcode_push_size_6);
static_assert(opcode_to_cpp(kth_opcode_push_size_6) == kth::domain::machine::opcode::push_size_6);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_7) == kth_opcode_push_size_7);
static_assert(opcode_to_cpp(kth_opcode_push_size_7) == kth::domain::machine::opcode::push_size_7);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_8) == kth_opcode_push_size_8);
static_assert(opcode_to_cpp(kth_opcode_push_size_8) == kth::domain::machine::opcode::push_size_8);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_9) == kth_opcode_push_size_9);
static_assert(opcode_to_cpp(kth_opcode_push_size_9) == kth::domain::machine::opcode::push_size_9);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_10) == kth_opcode_push_size_10);
static_assert(opcode_to_cpp(kth_opcode_push_size_10) == kth::domain::machine::opcode::push_size_10);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_11) == kth_opcode_push_size_11);
static_assert(opcode_to_cpp(kth_opcode_push_size_11) == kth::domain::machine::opcode::push_size_11);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_12) == kth_opcode_push_size_12);
static_assert(opcode_to_cpp(kth_opcode_push_size_12) == kth::domain::machine::opcode::push_size_12);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_13) == kth_opcode_push_size_13);
static_assert(opcode_to_cpp(kth_opcode_push_size_13) == kth::domain::machine::opcode::push_size_13);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_14) == kth_opcode_push_size_14);
static_assert(opcode_to_cpp(kth_opcode_push_size_14) == kth::domain::machine::opcode::push_size_14);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_15) == kth_opcode_push_size_15);
static_assert(opcode_to_cpp(kth_opcode_push_size_15) == kth::domain::machine::opcode::push_size_15);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_16) == kth_opcode_push_size_16);
static_assert(opcode_to_cpp(kth_opcode_push_size_16) == kth::domain::machine::opcode::push_size_16);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_17) == kth_opcode_push_size_17);
static_assert(opcode_to_cpp(kth_opcode_push_size_17) == kth::domain::machine::opcode::push_size_17);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_18) == kth_opcode_push_size_18);
static_assert(opcode_to_cpp(kth_opcode_push_size_18) == kth::domain::machine::opcode::push_size_18);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_19) == kth_opcode_push_size_19);
static_assert(opcode_to_cpp(kth_opcode_push_size_19) == kth::domain::machine::opcode::push_size_19);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_20) == kth_opcode_push_size_20);
static_assert(opcode_to_cpp(kth_opcode_push_size_20) == kth::domain::machine::opcode::push_size_20);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_21) == kth_opcode_push_size_21);
static_assert(opcode_to_cpp(kth_opcode_push_size_21) == kth::domain::machine::opcode::push_size_21);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_22) == kth_opcode_push_size_22);
static_assert(opcode_to_cpp(kth_opcode_push_size_22) == kth::domain::machine::opcode::push_size_22);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_23) == kth_opcode_push_size_23);
static_assert(opcode_to_cpp(kth_opcode_push_size_23) == kth::domain::machine::opcode::push_size_23);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_24) == kth_opcode_push_size_24);
static_assert(opcode_to_cpp(kth_opcode_push_size_24) == kth::domain::machine::opcode::push_size_24);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_25) == kth_opcode_push_size_25);
static_assert(opcode_to_cpp(kth_opcode_push_size_25) == kth::domain::machine::opcode::push_size_25);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_26) == kth_opcode_push_size_26);
static_assert(opcode_to_cpp(kth_opcode_push_size_26) == kth::domain::machine::opcode::push_size_26);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_27) == kth_opcode_push_size_27);
static_assert(opcode_to_cpp(kth_opcode_push_size_27) == kth::domain::machine::opcode::push_size_27);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_28) == kth_opcode_push_size_28);
static_assert(opcode_to_cpp(kth_opcode_push_size_28) == kth::domain::machine::opcode::push_size_28);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_29) == kth_opcode_push_size_29);
static_assert(opcode_to_cpp(kth_opcode_push_size_29) == kth::domain::machine::opcode::push_size_29);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_30) == kth_opcode_push_size_30);
static_assert(opcode_to_cpp(kth_opcode_push_size_30) == kth::domain::machine::opcode::push_size_30);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_31) == kth_opcode_push_size_31);
static_assert(opcode_to_cpp(kth_opcode_push_size_31) == kth::domain::machine::opcode::push_size_31);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_32) == kth_opcode_push_size_32);
static_assert(opcode_to_cpp(kth_opcode_push_size_32) == kth::domain::machine::opcode::push_size_32);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_33) == kth_opcode_push_size_33);
static_assert(opcode_to_cpp(kth_opcode_push_size_33) == kth::domain::machine::opcode::push_size_33);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_34) == kth_opcode_push_size_34);
static_assert(opcode_to_cpp(kth_opcode_push_size_34) == kth::domain::machine::opcode::push_size_34);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_35) == kth_opcode_push_size_35);
static_assert(opcode_to_cpp(kth_opcode_push_size_35) == kth::domain::machine::opcode::push_size_35);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_36) == kth_opcode_push_size_36);
static_assert(opcode_to_cpp(kth_opcode_push_size_36) == kth::domain::machine::opcode::push_size_36);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_37) == kth_opcode_push_size_37);
static_assert(opcode_to_cpp(kth_opcode_push_size_37) == kth::domain::machine::opcode::push_size_37);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_38) == kth_opcode_push_size_38);
static_assert(opcode_to_cpp(kth_opcode_push_size_38) == kth::domain::machine::opcode::push_size_38);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_39) == kth_opcode_push_size_39);
static_assert(opcode_to_cpp(kth_opcode_push_size_39) == kth::domain::machine::opcode::push_size_39);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_40) == kth_opcode_push_size_40);
static_assert(opcode_to_cpp(kth_opcode_push_size_40) == kth::domain::machine::opcode::push_size_40);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_41) == kth_opcode_push_size_41);
static_assert(opcode_to_cpp(kth_opcode_push_size_41) == kth::domain::machine::opcode::push_size_41);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_42) == kth_opcode_push_size_42);
static_assert(opcode_to_cpp(kth_opcode_push_size_42) == kth::domain::machine::opcode::push_size_42);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_43) == kth_opcode_push_size_43);
static_assert(opcode_to_cpp(kth_opcode_push_size_43) == kth::domain::machine::opcode::push_size_43);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_44) == kth_opcode_push_size_44);
static_assert(opcode_to_cpp(kth_opcode_push_size_44) == kth::domain::machine::opcode::push_size_44);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_45) == kth_opcode_push_size_45);
static_assert(opcode_to_cpp(kth_opcode_push_size_45) == kth::domain::machine::opcode::push_size_45);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_46) == kth_opcode_push_size_46);
static_assert(opcode_to_cpp(kth_opcode_push_size_46) == kth::domain::machine::opcode::push_size_46);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_47) == kth_opcode_push_size_47);
static_assert(opcode_to_cpp(kth_opcode_push_size_47) == kth::domain::machine::opcode::push_size_47);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_48) == kth_opcode_push_size_48);
static_assert(opcode_to_cpp(kth_opcode_push_size_48) == kth::domain::machine::opcode::push_size_48);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_49) == kth_opcode_push_size_49);
static_assert(opcode_to_cpp(kth_opcode_push_size_49) == kth::domain::machine::opcode::push_size_49);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_50) == kth_opcode_push_size_50);
static_assert(opcode_to_cpp(kth_opcode_push_size_50) == kth::domain::machine::opcode::push_size_50);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_51) == kth_opcode_push_size_51);
static_assert(opcode_to_cpp(kth_opcode_push_size_51) == kth::domain::machine::opcode::push_size_51);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_52) == kth_opcode_push_size_52);
static_assert(opcode_to_cpp(kth_opcode_push_size_52) == kth::domain::machine::opcode::push_size_52);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_53) == kth_opcode_push_size_53);
static_assert(opcode_to_cpp(kth_opcode_push_size_53) == kth::domain::machine::opcode::push_size_53);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_54) == kth_opcode_push_size_54);
static_assert(opcode_to_cpp(kth_opcode_push_size_54) == kth::domain::machine::opcode::push_size_54);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_55) == kth_opcode_push_size_55);
static_assert(opcode_to_cpp(kth_opcode_push_size_55) == kth::domain::machine::opcode::push_size_55);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_56) == kth_opcode_push_size_56);
static_assert(opcode_to_cpp(kth_opcode_push_size_56) == kth::domain::machine::opcode::push_size_56);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_57) == kth_opcode_push_size_57);
static_assert(opcode_to_cpp(kth_opcode_push_size_57) == kth::domain::machine::opcode::push_size_57);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_58) == kth_opcode_push_size_58);
static_assert(opcode_to_cpp(kth_opcode_push_size_58) == kth::domain::machine::opcode::push_size_58);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_59) == kth_opcode_push_size_59);
static_assert(opcode_to_cpp(kth_opcode_push_size_59) == kth::domain::machine::opcode::push_size_59);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_60) == kth_opcode_push_size_60);
static_assert(opcode_to_cpp(kth_opcode_push_size_60) == kth::domain::machine::opcode::push_size_60);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_61) == kth_opcode_push_size_61);
static_assert(opcode_to_cpp(kth_opcode_push_size_61) == kth::domain::machine::opcode::push_size_61);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_62) == kth_opcode_push_size_62);
static_assert(opcode_to_cpp(kth_opcode_push_size_62) == kth::domain::machine::opcode::push_size_62);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_63) == kth_opcode_push_size_63);
static_assert(opcode_to_cpp(kth_opcode_push_size_63) == kth::domain::machine::opcode::push_size_63);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_64) == kth_opcode_push_size_64);
static_assert(opcode_to_cpp(kth_opcode_push_size_64) == kth::domain::machine::opcode::push_size_64);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_65) == kth_opcode_push_size_65);
static_assert(opcode_to_cpp(kth_opcode_push_size_65) == kth::domain::machine::opcode::push_size_65);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_66) == kth_opcode_push_size_66);
static_assert(opcode_to_cpp(kth_opcode_push_size_66) == kth::domain::machine::opcode::push_size_66);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_67) == kth_opcode_push_size_67);
static_assert(opcode_to_cpp(kth_opcode_push_size_67) == kth::domain::machine::opcode::push_size_67);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_68) == kth_opcode_push_size_68);
static_assert(opcode_to_cpp(kth_opcode_push_size_68) == kth::domain::machine::opcode::push_size_68);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_69) == kth_opcode_push_size_69);
static_assert(opcode_to_cpp(kth_opcode_push_size_69) == kth::domain::machine::opcode::push_size_69);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_70) == kth_opcode_push_size_70);
static_assert(opcode_to_cpp(kth_opcode_push_size_70) == kth::domain::machine::opcode::push_size_70);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_71) == kth_opcode_push_size_71);
static_assert(opcode_to_cpp(kth_opcode_push_size_71) == kth::domain::machine::opcode::push_size_71);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_72) == kth_opcode_push_size_72);
static_assert(opcode_to_cpp(kth_opcode_push_size_72) == kth::domain::machine::opcode::push_size_72);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_73) == kth_opcode_push_size_73);
static_assert(opcode_to_cpp(kth_opcode_push_size_73) == kth::domain::machine::opcode::push_size_73);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_74) == kth_opcode_push_size_74);
static_assert(opcode_to_cpp(kth_opcode_push_size_74) == kth::domain::machine::opcode::push_size_74);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_size_75) == kth_opcode_push_size_75);
static_assert(opcode_to_cpp(kth_opcode_push_size_75) == kth::domain::machine::opcode::push_size_75);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_one_size) == kth_opcode_push_one_size);
static_assert(opcode_to_cpp(kth_opcode_push_one_size) == kth::domain::machine::opcode::push_one_size);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_two_size) == kth_opcode_push_two_size);
static_assert(opcode_to_cpp(kth_opcode_push_two_size) == kth::domain::machine::opcode::push_two_size);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_four_size) == kth_opcode_push_four_size);
static_assert(opcode_to_cpp(kth_opcode_push_four_size) == kth::domain::machine::opcode::push_four_size);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_negative_1) == kth_opcode_push_negative_1);
static_assert(opcode_to_cpp(kth_opcode_push_negative_1) == kth::domain::machine::opcode::push_negative_1);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_80) == kth_opcode_reserved_80);
static_assert(opcode_to_cpp(kth_opcode_reserved_80) == kth::domain::machine::opcode::reserved_80);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_1) == kth_opcode_push_positive_1);
static_assert(opcode_to_cpp(kth_opcode_push_positive_1) == kth::domain::machine::opcode::push_positive_1);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_2) == kth_opcode_push_positive_2);
static_assert(opcode_to_cpp(kth_opcode_push_positive_2) == kth::domain::machine::opcode::push_positive_2);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_3) == kth_opcode_push_positive_3);
static_assert(opcode_to_cpp(kth_opcode_push_positive_3) == kth::domain::machine::opcode::push_positive_3);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_4) == kth_opcode_push_positive_4);
static_assert(opcode_to_cpp(kth_opcode_push_positive_4) == kth::domain::machine::opcode::push_positive_4);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_5) == kth_opcode_push_positive_5);
static_assert(opcode_to_cpp(kth_opcode_push_positive_5) == kth::domain::machine::opcode::push_positive_5);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_6) == kth_opcode_push_positive_6);
static_assert(opcode_to_cpp(kth_opcode_push_positive_6) == kth::domain::machine::opcode::push_positive_6);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_7) == kth_opcode_push_positive_7);
static_assert(opcode_to_cpp(kth_opcode_push_positive_7) == kth::domain::machine::opcode::push_positive_7);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_8) == kth_opcode_push_positive_8);
static_assert(opcode_to_cpp(kth_opcode_push_positive_8) == kth::domain::machine::opcode::push_positive_8);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_9) == kth_opcode_push_positive_9);
static_assert(opcode_to_cpp(kth_opcode_push_positive_9) == kth::domain::machine::opcode::push_positive_9);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_10) == kth_opcode_push_positive_10);
static_assert(opcode_to_cpp(kth_opcode_push_positive_10) == kth::domain::machine::opcode::push_positive_10);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_11) == kth_opcode_push_positive_11);
static_assert(opcode_to_cpp(kth_opcode_push_positive_11) == kth::domain::machine::opcode::push_positive_11);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_12) == kth_opcode_push_positive_12);
static_assert(opcode_to_cpp(kth_opcode_push_positive_12) == kth::domain::machine::opcode::push_positive_12);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_13) == kth_opcode_push_positive_13);
static_assert(opcode_to_cpp(kth_opcode_push_positive_13) == kth::domain::machine::opcode::push_positive_13);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_14) == kth_opcode_push_positive_14);
static_assert(opcode_to_cpp(kth_opcode_push_positive_14) == kth::domain::machine::opcode::push_positive_14);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_15) == kth_opcode_push_positive_15);
static_assert(opcode_to_cpp(kth_opcode_push_positive_15) == kth::domain::machine::opcode::push_positive_15);
static_assert(opcode_to_c(kth::domain::machine::opcode::push_positive_16) == kth_opcode_push_positive_16);
static_assert(opcode_to_cpp(kth_opcode_push_positive_16) == kth::domain::machine::opcode::push_positive_16);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop) == kth_opcode_nop);
static_assert(opcode_to_cpp(kth_opcode_nop) == kth::domain::machine::opcode::nop);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_98) == kth_opcode_reserved_98);
static_assert(opcode_to_cpp(kth_opcode_reserved_98) == kth::domain::machine::opcode::reserved_98);
static_assert(opcode_to_c(kth::domain::machine::opcode::if_) == kth_opcode_if_);
static_assert(opcode_to_cpp(kth_opcode_if_) == kth::domain::machine::opcode::if_);
static_assert(opcode_to_c(kth::domain::machine::opcode::notif) == kth_opcode_notif);
static_assert(opcode_to_cpp(kth_opcode_notif) == kth::domain::machine::opcode::notif);
static_assert(opcode_to_c(kth::domain::machine::opcode::op_begin) == kth_opcode_op_begin);
static_assert(opcode_to_cpp(kth_opcode_op_begin) == kth::domain::machine::opcode::op_begin);
static_assert(opcode_to_c(kth::domain::machine::opcode::disabled_verif) == kth_opcode_op_begin);
static_assert(opcode_to_cpp(kth_opcode_op_begin) == kth::domain::machine::opcode::disabled_verif);
static_assert(opcode_to_c(kth::domain::machine::opcode::op_until) == kth_opcode_op_until);
static_assert(opcode_to_cpp(kth_opcode_op_until) == kth::domain::machine::opcode::op_until);
static_assert(opcode_to_c(kth::domain::machine::opcode::disabled_vernotif) == kth_opcode_op_until);
static_assert(opcode_to_cpp(kth_opcode_op_until) == kth::domain::machine::opcode::disabled_vernotif);
static_assert(opcode_to_c(kth::domain::machine::opcode::else_) == kth_opcode_else_);
static_assert(opcode_to_cpp(kth_opcode_else_) == kth::domain::machine::opcode::else_);
static_assert(opcode_to_c(kth::domain::machine::opcode::endif) == kth_opcode_endif);
static_assert(opcode_to_cpp(kth_opcode_endif) == kth::domain::machine::opcode::endif);
static_assert(opcode_to_c(kth::domain::machine::opcode::verify) == kth_opcode_verify);
static_assert(opcode_to_cpp(kth_opcode_verify) == kth::domain::machine::opcode::verify);
static_assert(opcode_to_c(kth::domain::machine::opcode::return_) == kth_opcode_return_);
static_assert(opcode_to_cpp(kth_opcode_return_) == kth::domain::machine::opcode::return_);
static_assert(opcode_to_c(kth::domain::machine::opcode::toaltstack) == kth_opcode_toaltstack);
static_assert(opcode_to_cpp(kth_opcode_toaltstack) == kth::domain::machine::opcode::toaltstack);
static_assert(opcode_to_c(kth::domain::machine::opcode::fromaltstack) == kth_opcode_fromaltstack);
static_assert(opcode_to_cpp(kth_opcode_fromaltstack) == kth::domain::machine::opcode::fromaltstack);
static_assert(opcode_to_c(kth::domain::machine::opcode::drop2) == kth_opcode_drop2);
static_assert(opcode_to_cpp(kth_opcode_drop2) == kth::domain::machine::opcode::drop2);
static_assert(opcode_to_c(kth::domain::machine::opcode::dup2) == kth_opcode_dup2);
static_assert(opcode_to_cpp(kth_opcode_dup2) == kth::domain::machine::opcode::dup2);
static_assert(opcode_to_c(kth::domain::machine::opcode::dup3) == kth_opcode_dup3);
static_assert(opcode_to_cpp(kth_opcode_dup3) == kth::domain::machine::opcode::dup3);
static_assert(opcode_to_c(kth::domain::machine::opcode::over2) == kth_opcode_over2);
static_assert(opcode_to_cpp(kth_opcode_over2) == kth::domain::machine::opcode::over2);
static_assert(opcode_to_c(kth::domain::machine::opcode::rot2) == kth_opcode_rot2);
static_assert(opcode_to_cpp(kth_opcode_rot2) == kth::domain::machine::opcode::rot2);
static_assert(opcode_to_c(kth::domain::machine::opcode::swap2) == kth_opcode_swap2);
static_assert(opcode_to_cpp(kth_opcode_swap2) == kth::domain::machine::opcode::swap2);
static_assert(opcode_to_c(kth::domain::machine::opcode::ifdup) == kth_opcode_ifdup);
static_assert(opcode_to_cpp(kth_opcode_ifdup) == kth::domain::machine::opcode::ifdup);
static_assert(opcode_to_c(kth::domain::machine::opcode::depth) == kth_opcode_depth);
static_assert(opcode_to_cpp(kth_opcode_depth) == kth::domain::machine::opcode::depth);
static_assert(opcode_to_c(kth::domain::machine::opcode::drop) == kth_opcode_drop);
static_assert(opcode_to_cpp(kth_opcode_drop) == kth::domain::machine::opcode::drop);
static_assert(opcode_to_c(kth::domain::machine::opcode::dup) == kth_opcode_dup);
static_assert(opcode_to_cpp(kth_opcode_dup) == kth::domain::machine::opcode::dup);
static_assert(opcode_to_c(kth::domain::machine::opcode::nip) == kth_opcode_nip);
static_assert(opcode_to_cpp(kth_opcode_nip) == kth::domain::machine::opcode::nip);
static_assert(opcode_to_c(kth::domain::machine::opcode::over) == kth_opcode_over);
static_assert(opcode_to_cpp(kth_opcode_over) == kth::domain::machine::opcode::over);
static_assert(opcode_to_c(kth::domain::machine::opcode::pick) == kth_opcode_pick);
static_assert(opcode_to_cpp(kth_opcode_pick) == kth::domain::machine::opcode::pick);
static_assert(opcode_to_c(kth::domain::machine::opcode::roll) == kth_opcode_roll);
static_assert(opcode_to_cpp(kth_opcode_roll) == kth::domain::machine::opcode::roll);
static_assert(opcode_to_c(kth::domain::machine::opcode::rot) == kth_opcode_rot);
static_assert(opcode_to_cpp(kth_opcode_rot) == kth::domain::machine::opcode::rot);
static_assert(opcode_to_c(kth::domain::machine::opcode::swap) == kth_opcode_swap);
static_assert(opcode_to_cpp(kth_opcode_swap) == kth::domain::machine::opcode::swap);
static_assert(opcode_to_c(kth::domain::machine::opcode::tuck) == kth_opcode_tuck);
static_assert(opcode_to_cpp(kth_opcode_tuck) == kth::domain::machine::opcode::tuck);
static_assert(opcode_to_c(kth::domain::machine::opcode::cat) == kth_opcode_cat);
static_assert(opcode_to_cpp(kth_opcode_cat) == kth::domain::machine::opcode::cat);
static_assert(opcode_to_c(kth::domain::machine::opcode::split) == kth_opcode_split);
static_assert(opcode_to_cpp(kth_opcode_split) == kth::domain::machine::opcode::split);
static_assert(opcode_to_c(kth::domain::machine::opcode::num2bin) == kth_opcode_num2bin);
static_assert(opcode_to_cpp(kth_opcode_num2bin) == kth::domain::machine::opcode::num2bin);
static_assert(opcode_to_c(kth::domain::machine::opcode::bin2num) == kth_opcode_bin2num);
static_assert(opcode_to_cpp(kth_opcode_bin2num) == kth::domain::machine::opcode::bin2num);
static_assert(opcode_to_c(kth::domain::machine::opcode::size) == kth_opcode_size);
static_assert(opcode_to_cpp(kth_opcode_size) == kth::domain::machine::opcode::size);
static_assert(opcode_to_c(kth::domain::machine::opcode::op_invert) == kth_opcode_op_invert);
static_assert(opcode_to_cpp(kth_opcode_op_invert) == kth::domain::machine::opcode::op_invert);
static_assert(opcode_to_c(kth::domain::machine::opcode::disabled_invert) == kth_opcode_op_invert);
static_assert(opcode_to_cpp(kth_opcode_op_invert) == kth::domain::machine::opcode::disabled_invert);
static_assert(opcode_to_c(kth::domain::machine::opcode::and_) == kth_opcode_and_);
static_assert(opcode_to_cpp(kth_opcode_and_) == kth::domain::machine::opcode::and_);
static_assert(opcode_to_c(kth::domain::machine::opcode::or_) == kth_opcode_or_);
static_assert(opcode_to_cpp(kth_opcode_or_) == kth::domain::machine::opcode::or_);
static_assert(opcode_to_c(kth::domain::machine::opcode::xor_) == kth_opcode_xor_);
static_assert(opcode_to_cpp(kth_opcode_xor_) == kth::domain::machine::opcode::xor_);
static_assert(opcode_to_c(kth::domain::machine::opcode::equal) == kth_opcode_equal);
static_assert(opcode_to_cpp(kth_opcode_equal) == kth::domain::machine::opcode::equal);
static_assert(opcode_to_c(kth::domain::machine::opcode::equalverify) == kth_opcode_equalverify);
static_assert(opcode_to_cpp(kth_opcode_equalverify) == kth::domain::machine::opcode::equalverify);
static_assert(opcode_to_c(kth::domain::machine::opcode::op_define) == kth_opcode_op_define);
static_assert(opcode_to_cpp(kth_opcode_op_define) == kth::domain::machine::opcode::op_define);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_137) == kth_opcode_op_define);
static_assert(opcode_to_cpp(kth_opcode_op_define) == kth::domain::machine::opcode::reserved_137);
static_assert(opcode_to_c(kth::domain::machine::opcode::op_invoke) == kth_opcode_op_invoke);
static_assert(opcode_to_cpp(kth_opcode_op_invoke) == kth::domain::machine::opcode::op_invoke);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_138) == kth_opcode_op_invoke);
static_assert(opcode_to_cpp(kth_opcode_op_invoke) == kth::domain::machine::opcode::reserved_138);
static_assert(opcode_to_c(kth::domain::machine::opcode::add1) == kth_opcode_add1);
static_assert(opcode_to_cpp(kth_opcode_add1) == kth::domain::machine::opcode::add1);
static_assert(opcode_to_c(kth::domain::machine::opcode::sub1) == kth_opcode_sub1);
static_assert(opcode_to_cpp(kth_opcode_sub1) == kth::domain::machine::opcode::sub1);
static_assert(opcode_to_c(kth::domain::machine::opcode::op_lshiftnum) == kth_opcode_op_lshiftnum);
static_assert(opcode_to_cpp(kth_opcode_op_lshiftnum) == kth::domain::machine::opcode::op_lshiftnum);
static_assert(opcode_to_c(kth::domain::machine::opcode::disabled_mul2) == kth_opcode_op_lshiftnum);
static_assert(opcode_to_cpp(kth_opcode_op_lshiftnum) == kth::domain::machine::opcode::disabled_mul2);
static_assert(opcode_to_c(kth::domain::machine::opcode::op_rshiftnum) == kth_opcode_op_rshiftnum);
static_assert(opcode_to_cpp(kth_opcode_op_rshiftnum) == kth::domain::machine::opcode::op_rshiftnum);
static_assert(opcode_to_c(kth::domain::machine::opcode::disabled_div2) == kth_opcode_op_rshiftnum);
static_assert(opcode_to_cpp(kth_opcode_op_rshiftnum) == kth::domain::machine::opcode::disabled_div2);
static_assert(opcode_to_c(kth::domain::machine::opcode::negate) == kth_opcode_negate);
static_assert(opcode_to_cpp(kth_opcode_negate) == kth::domain::machine::opcode::negate);
static_assert(opcode_to_c(kth::domain::machine::opcode::abs) == kth_opcode_abs);
static_assert(opcode_to_cpp(kth_opcode_abs) == kth::domain::machine::opcode::abs);
static_assert(opcode_to_c(kth::domain::machine::opcode::not_) == kth_opcode_not_);
static_assert(opcode_to_cpp(kth_opcode_not_) == kth::domain::machine::opcode::not_);
static_assert(opcode_to_c(kth::domain::machine::opcode::nonzero) == kth_opcode_nonzero);
static_assert(opcode_to_cpp(kth_opcode_nonzero) == kth::domain::machine::opcode::nonzero);
static_assert(opcode_to_c(kth::domain::machine::opcode::add) == kth_opcode_add);
static_assert(opcode_to_cpp(kth_opcode_add) == kth::domain::machine::opcode::add);
static_assert(opcode_to_c(kth::domain::machine::opcode::sub) == kth_opcode_sub);
static_assert(opcode_to_cpp(kth_opcode_sub) == kth::domain::machine::opcode::sub);
static_assert(opcode_to_c(kth::domain::machine::opcode::mul) == kth_opcode_mul);
static_assert(opcode_to_cpp(kth_opcode_mul) == kth::domain::machine::opcode::mul);
static_assert(opcode_to_c(kth::domain::machine::opcode::div) == kth_opcode_div);
static_assert(opcode_to_cpp(kth_opcode_div) == kth::domain::machine::opcode::div);
static_assert(opcode_to_c(kth::domain::machine::opcode::mod) == kth_opcode_mod);
static_assert(opcode_to_cpp(kth_opcode_mod) == kth::domain::machine::opcode::mod);
static_assert(opcode_to_c(kth::domain::machine::opcode::op_lshiftbin) == kth_opcode_op_lshiftbin);
static_assert(opcode_to_cpp(kth_opcode_op_lshiftbin) == kth::domain::machine::opcode::op_lshiftbin);
static_assert(opcode_to_c(kth::domain::machine::opcode::disabled_lshift) == kth_opcode_op_lshiftbin);
static_assert(opcode_to_cpp(kth_opcode_op_lshiftbin) == kth::domain::machine::opcode::disabled_lshift);
static_assert(opcode_to_c(kth::domain::machine::opcode::op_rshiftbin) == kth_opcode_op_rshiftbin);
static_assert(opcode_to_cpp(kth_opcode_op_rshiftbin) == kth::domain::machine::opcode::op_rshiftbin);
static_assert(opcode_to_c(kth::domain::machine::opcode::disabled_rshift) == kth_opcode_op_rshiftbin);
static_assert(opcode_to_cpp(kth_opcode_op_rshiftbin) == kth::domain::machine::opcode::disabled_rshift);
static_assert(opcode_to_c(kth::domain::machine::opcode::booland) == kth_opcode_booland);
static_assert(opcode_to_cpp(kth_opcode_booland) == kth::domain::machine::opcode::booland);
static_assert(opcode_to_c(kth::domain::machine::opcode::boolor) == kth_opcode_boolor);
static_assert(opcode_to_cpp(kth_opcode_boolor) == kth::domain::machine::opcode::boolor);
static_assert(opcode_to_c(kth::domain::machine::opcode::numequal) == kth_opcode_numequal);
static_assert(opcode_to_cpp(kth_opcode_numequal) == kth::domain::machine::opcode::numequal);
static_assert(opcode_to_c(kth::domain::machine::opcode::numequalverify) == kth_opcode_numequalverify);
static_assert(opcode_to_cpp(kth_opcode_numequalverify) == kth::domain::machine::opcode::numequalverify);
static_assert(opcode_to_c(kth::domain::machine::opcode::numnotequal) == kth_opcode_numnotequal);
static_assert(opcode_to_cpp(kth_opcode_numnotequal) == kth::domain::machine::opcode::numnotequal);
static_assert(opcode_to_c(kth::domain::machine::opcode::lessthan) == kth_opcode_lessthan);
static_assert(opcode_to_cpp(kth_opcode_lessthan) == kth::domain::machine::opcode::lessthan);
static_assert(opcode_to_c(kth::domain::machine::opcode::greaterthan) == kth_opcode_greaterthan);
static_assert(opcode_to_cpp(kth_opcode_greaterthan) == kth::domain::machine::opcode::greaterthan);
static_assert(opcode_to_c(kth::domain::machine::opcode::lessthanorequal) == kth_opcode_lessthanorequal);
static_assert(opcode_to_cpp(kth_opcode_lessthanorequal) == kth::domain::machine::opcode::lessthanorequal);
static_assert(opcode_to_c(kth::domain::machine::opcode::greaterthanorequal) == kth_opcode_greaterthanorequal);
static_assert(opcode_to_cpp(kth_opcode_greaterthanorequal) == kth::domain::machine::opcode::greaterthanorequal);
static_assert(opcode_to_c(kth::domain::machine::opcode::min) == kth_opcode_min);
static_assert(opcode_to_cpp(kth_opcode_min) == kth::domain::machine::opcode::min);
static_assert(opcode_to_c(kth::domain::machine::opcode::max) == kth_opcode_max);
static_assert(opcode_to_cpp(kth_opcode_max) == kth::domain::machine::opcode::max);
static_assert(opcode_to_c(kth::domain::machine::opcode::within) == kth_opcode_within);
static_assert(opcode_to_cpp(kth_opcode_within) == kth::domain::machine::opcode::within);
static_assert(opcode_to_c(kth::domain::machine::opcode::ripemd160) == kth_opcode_ripemd160);
static_assert(opcode_to_cpp(kth_opcode_ripemd160) == kth::domain::machine::opcode::ripemd160);
static_assert(opcode_to_c(kth::domain::machine::opcode::sha1) == kth_opcode_sha1);
static_assert(opcode_to_cpp(kth_opcode_sha1) == kth::domain::machine::opcode::sha1);
static_assert(opcode_to_c(kth::domain::machine::opcode::sha256) == kth_opcode_sha256);
static_assert(opcode_to_cpp(kth_opcode_sha256) == kth::domain::machine::opcode::sha256);
static_assert(opcode_to_c(kth::domain::machine::opcode::hash160) == kth_opcode_hash160);
static_assert(opcode_to_cpp(kth_opcode_hash160) == kth::domain::machine::opcode::hash160);
static_assert(opcode_to_c(kth::domain::machine::opcode::hash256) == kth_opcode_hash256);
static_assert(opcode_to_cpp(kth_opcode_hash256) == kth::domain::machine::opcode::hash256);
static_assert(opcode_to_c(kth::domain::machine::opcode::codeseparator) == kth_opcode_codeseparator);
static_assert(opcode_to_cpp(kth_opcode_codeseparator) == kth::domain::machine::opcode::codeseparator);
static_assert(opcode_to_c(kth::domain::machine::opcode::checksig) == kth_opcode_checksig);
static_assert(opcode_to_cpp(kth_opcode_checksig) == kth::domain::machine::opcode::checksig);
static_assert(opcode_to_c(kth::domain::machine::opcode::checksigverify) == kth_opcode_checksigverify);
static_assert(opcode_to_cpp(kth_opcode_checksigverify) == kth::domain::machine::opcode::checksigverify);
static_assert(opcode_to_c(kth::domain::machine::opcode::checkmultisig) == kth_opcode_checkmultisig);
static_assert(opcode_to_cpp(kth_opcode_checkmultisig) == kth::domain::machine::opcode::checkmultisig);
static_assert(opcode_to_c(kth::domain::machine::opcode::checkmultisigverify) == kth_opcode_checkmultisigverify);
static_assert(opcode_to_cpp(kth_opcode_checkmultisigverify) == kth::domain::machine::opcode::checkmultisigverify);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop1) == kth_opcode_nop1);
static_assert(opcode_to_cpp(kth_opcode_nop1) == kth::domain::machine::opcode::nop1);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop2) == kth_opcode_nop2);
static_assert(opcode_to_cpp(kth_opcode_nop2) == kth::domain::machine::opcode::nop2);
static_assert(opcode_to_c(kth::domain::machine::opcode::checklocktimeverify) == kth_opcode_nop2);
static_assert(opcode_to_cpp(kth_opcode_nop2) == kth::domain::machine::opcode::checklocktimeverify);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop3) == kth_opcode_nop3);
static_assert(opcode_to_cpp(kth_opcode_nop3) == kth::domain::machine::opcode::nop3);
static_assert(opcode_to_c(kth::domain::machine::opcode::checksequenceverify) == kth_opcode_nop3);
static_assert(opcode_to_cpp(kth_opcode_nop3) == kth::domain::machine::opcode::checksequenceverify);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop4) == kth_opcode_nop4);
static_assert(opcode_to_cpp(kth_opcode_nop4) == kth::domain::machine::opcode::nop4);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop5) == kth_opcode_nop5);
static_assert(opcode_to_cpp(kth_opcode_nop5) == kth::domain::machine::opcode::nop5);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop6) == kth_opcode_nop6);
static_assert(opcode_to_cpp(kth_opcode_nop6) == kth::domain::machine::opcode::nop6);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop7) == kth_opcode_nop7);
static_assert(opcode_to_cpp(kth_opcode_nop7) == kth::domain::machine::opcode::nop7);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop8) == kth_opcode_nop8);
static_assert(opcode_to_cpp(kth_opcode_nop8) == kth::domain::machine::opcode::nop8);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop9) == kth_opcode_nop9);
static_assert(opcode_to_cpp(kth_opcode_nop9) == kth::domain::machine::opcode::nop9);
static_assert(opcode_to_c(kth::domain::machine::opcode::nop10) == kth_opcode_nop10);
static_assert(opcode_to_cpp(kth_opcode_nop10) == kth::domain::machine::opcode::nop10);
static_assert(opcode_to_c(kth::domain::machine::opcode::checkdatasig) == kth_opcode_checkdatasig);
static_assert(opcode_to_cpp(kth_opcode_checkdatasig) == kth::domain::machine::opcode::checkdatasig);
static_assert(opcode_to_c(kth::domain::machine::opcode::checkdatasigverify) == kth_opcode_checkdatasigverify);
static_assert(opcode_to_cpp(kth_opcode_checkdatasigverify) == kth::domain::machine::opcode::checkdatasigverify);
static_assert(opcode_to_c(kth::domain::machine::opcode::reverse_bytes) == kth_opcode_reverse_bytes);
static_assert(opcode_to_cpp(kth_opcode_reverse_bytes) == kth::domain::machine::opcode::reverse_bytes);
static_assert(opcode_to_c(kth::domain::machine::opcode::input_index) == kth_opcode_input_index);
static_assert(opcode_to_cpp(kth_opcode_input_index) == kth::domain::machine::opcode::input_index);
static_assert(opcode_to_c(kth::domain::machine::opcode::active_bytecode) == kth_opcode_active_bytecode);
static_assert(opcode_to_cpp(kth_opcode_active_bytecode) == kth::domain::machine::opcode::active_bytecode);
static_assert(opcode_to_c(kth::domain::machine::opcode::tx_version) == kth_opcode_tx_version);
static_assert(opcode_to_cpp(kth_opcode_tx_version) == kth::domain::machine::opcode::tx_version);
static_assert(opcode_to_c(kth::domain::machine::opcode::tx_input_count) == kth_opcode_tx_input_count);
static_assert(opcode_to_cpp(kth_opcode_tx_input_count) == kth::domain::machine::opcode::tx_input_count);
static_assert(opcode_to_c(kth::domain::machine::opcode::tx_output_count) == kth_opcode_tx_output_count);
static_assert(opcode_to_cpp(kth_opcode_tx_output_count) == kth::domain::machine::opcode::tx_output_count);
static_assert(opcode_to_c(kth::domain::machine::opcode::tx_locktime) == kth_opcode_tx_locktime);
static_assert(opcode_to_cpp(kth_opcode_tx_locktime) == kth::domain::machine::opcode::tx_locktime);
static_assert(opcode_to_c(kth::domain::machine::opcode::utxo_value) == kth_opcode_utxo_value);
static_assert(opcode_to_cpp(kth_opcode_utxo_value) == kth::domain::machine::opcode::utxo_value);
static_assert(opcode_to_c(kth::domain::machine::opcode::utxo_bytecode) == kth_opcode_utxo_bytecode);
static_assert(opcode_to_cpp(kth_opcode_utxo_bytecode) == kth::domain::machine::opcode::utxo_bytecode);
static_assert(opcode_to_c(kth::domain::machine::opcode::outpoint_tx_hash) == kth_opcode_outpoint_tx_hash);
static_assert(opcode_to_cpp(kth_opcode_outpoint_tx_hash) == kth::domain::machine::opcode::outpoint_tx_hash);
static_assert(opcode_to_c(kth::domain::machine::opcode::outpoint_index) == kth_opcode_outpoint_index);
static_assert(opcode_to_cpp(kth_opcode_outpoint_index) == kth::domain::machine::opcode::outpoint_index);
static_assert(opcode_to_c(kth::domain::machine::opcode::input_bytecode) == kth_opcode_input_bytecode);
static_assert(opcode_to_cpp(kth_opcode_input_bytecode) == kth::domain::machine::opcode::input_bytecode);
static_assert(opcode_to_c(kth::domain::machine::opcode::input_sequence_number) == kth_opcode_input_sequence_number);
static_assert(opcode_to_cpp(kth_opcode_input_sequence_number) == kth::domain::machine::opcode::input_sequence_number);
static_assert(opcode_to_c(kth::domain::machine::opcode::output_value) == kth_opcode_output_value);
static_assert(opcode_to_cpp(kth_opcode_output_value) == kth::domain::machine::opcode::output_value);
static_assert(opcode_to_c(kth::domain::machine::opcode::output_bytecode) == kth_opcode_output_bytecode);
static_assert(opcode_to_cpp(kth_opcode_output_bytecode) == kth::domain::machine::opcode::output_bytecode);
static_assert(opcode_to_c(kth::domain::machine::opcode::utxo_token_category) == kth_opcode_utxo_token_category);
static_assert(opcode_to_cpp(kth_opcode_utxo_token_category) == kth::domain::machine::opcode::utxo_token_category);
static_assert(opcode_to_c(kth::domain::machine::opcode::utxo_token_commitment) == kth_opcode_utxo_token_commitment);
static_assert(opcode_to_cpp(kth_opcode_utxo_token_commitment) == kth::domain::machine::opcode::utxo_token_commitment);
static_assert(opcode_to_c(kth::domain::machine::opcode::utxo_token_amount) == kth_opcode_utxo_token_amount);
static_assert(opcode_to_cpp(kth_opcode_utxo_token_amount) == kth::domain::machine::opcode::utxo_token_amount);
static_assert(opcode_to_c(kth::domain::machine::opcode::output_token_category) == kth_opcode_output_token_category);
static_assert(opcode_to_cpp(kth_opcode_output_token_category) == kth::domain::machine::opcode::output_token_category);
static_assert(opcode_to_c(kth::domain::machine::opcode::output_token_commitment) == kth_opcode_output_token_commitment);
static_assert(opcode_to_cpp(kth_opcode_output_token_commitment) == kth::domain::machine::opcode::output_token_commitment);
static_assert(opcode_to_c(kth::domain::machine::opcode::output_token_amount) == kth_opcode_output_token_amount);
static_assert(opcode_to_cpp(kth_opcode_output_token_amount) == kth::domain::machine::opcode::output_token_amount);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_212) == kth_opcode_reserved_212);
static_assert(opcode_to_cpp(kth_opcode_reserved_212) == kth::domain::machine::opcode::reserved_212);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_213) == kth_opcode_reserved_213);
static_assert(opcode_to_cpp(kth_opcode_reserved_213) == kth::domain::machine::opcode::reserved_213);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_214) == kth_opcode_reserved_214);
static_assert(opcode_to_cpp(kth_opcode_reserved_214) == kth::domain::machine::opcode::reserved_214);
static_assert(opcode_to_c(kth::domain::machine::opcode::first_undefined_op_value) == kth_opcode_reserved_214);
static_assert(opcode_to_cpp(kth_opcode_reserved_214) == kth::domain::machine::opcode::first_undefined_op_value);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_215) == kth_opcode_reserved_215);
static_assert(opcode_to_cpp(kth_opcode_reserved_215) == kth::domain::machine::opcode::reserved_215);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_216) == kth_opcode_reserved_216);
static_assert(opcode_to_cpp(kth_opcode_reserved_216) == kth::domain::machine::opcode::reserved_216);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_217) == kth_opcode_reserved_217);
static_assert(opcode_to_cpp(kth_opcode_reserved_217) == kth::domain::machine::opcode::reserved_217);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_218) == kth_opcode_reserved_218);
static_assert(opcode_to_cpp(kth_opcode_reserved_218) == kth::domain::machine::opcode::reserved_218);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_219) == kth_opcode_reserved_219);
static_assert(opcode_to_cpp(kth_opcode_reserved_219) == kth::domain::machine::opcode::reserved_219);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_220) == kth_opcode_reserved_220);
static_assert(opcode_to_cpp(kth_opcode_reserved_220) == kth::domain::machine::opcode::reserved_220);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_221) == kth_opcode_reserved_221);
static_assert(opcode_to_cpp(kth_opcode_reserved_221) == kth::domain::machine::opcode::reserved_221);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_222) == kth_opcode_reserved_222);
static_assert(opcode_to_cpp(kth_opcode_reserved_222) == kth::domain::machine::opcode::reserved_222);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_223) == kth_opcode_reserved_223);
static_assert(opcode_to_cpp(kth_opcode_reserved_223) == kth::domain::machine::opcode::reserved_223);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_224) == kth_opcode_reserved_224);
static_assert(opcode_to_cpp(kth_opcode_reserved_224) == kth::domain::machine::opcode::reserved_224);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_225) == kth_opcode_reserved_225);
static_assert(opcode_to_cpp(kth_opcode_reserved_225) == kth::domain::machine::opcode::reserved_225);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_226) == kth_opcode_reserved_226);
static_assert(opcode_to_cpp(kth_opcode_reserved_226) == kth::domain::machine::opcode::reserved_226);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_227) == kth_opcode_reserved_227);
static_assert(opcode_to_cpp(kth_opcode_reserved_227) == kth::domain::machine::opcode::reserved_227);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_228) == kth_opcode_reserved_228);
static_assert(opcode_to_cpp(kth_opcode_reserved_228) == kth::domain::machine::opcode::reserved_228);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_229) == kth_opcode_reserved_229);
static_assert(opcode_to_cpp(kth_opcode_reserved_229) == kth::domain::machine::opcode::reserved_229);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_230) == kth_opcode_reserved_230);
static_assert(opcode_to_cpp(kth_opcode_reserved_230) == kth::domain::machine::opcode::reserved_230);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_231) == kth_opcode_reserved_231);
static_assert(opcode_to_cpp(kth_opcode_reserved_231) == kth::domain::machine::opcode::reserved_231);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_232) == kth_opcode_reserved_232);
static_assert(opcode_to_cpp(kth_opcode_reserved_232) == kth::domain::machine::opcode::reserved_232);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_233) == kth_opcode_reserved_233);
static_assert(opcode_to_cpp(kth_opcode_reserved_233) == kth::domain::machine::opcode::reserved_233);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_234) == kth_opcode_reserved_234);
static_assert(opcode_to_cpp(kth_opcode_reserved_234) == kth::domain::machine::opcode::reserved_234);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_235) == kth_opcode_reserved_235);
static_assert(opcode_to_cpp(kth_opcode_reserved_235) == kth::domain::machine::opcode::reserved_235);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_236) == kth_opcode_reserved_236);
static_assert(opcode_to_cpp(kth_opcode_reserved_236) == kth::domain::machine::opcode::reserved_236);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_237) == kth_opcode_reserved_237);
static_assert(opcode_to_cpp(kth_opcode_reserved_237) == kth::domain::machine::opcode::reserved_237);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_238) == kth_opcode_reserved_238);
static_assert(opcode_to_cpp(kth_opcode_reserved_238) == kth::domain::machine::opcode::reserved_238);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_239) == kth_opcode_reserved_239);
static_assert(opcode_to_cpp(kth_opcode_reserved_239) == kth::domain::machine::opcode::reserved_239);
static_assert(opcode_to_c(kth::domain::machine::opcode::special_token_prefix) == kth_opcode_reserved_239);
static_assert(opcode_to_cpp(kth_opcode_reserved_239) == kth::domain::machine::opcode::special_token_prefix);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_240) == kth_opcode_reserved_240);
static_assert(opcode_to_cpp(kth_opcode_reserved_240) == kth::domain::machine::opcode::reserved_240);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_241) == kth_opcode_reserved_241);
static_assert(opcode_to_cpp(kth_opcode_reserved_241) == kth::domain::machine::opcode::reserved_241);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_242) == kth_opcode_reserved_242);
static_assert(opcode_to_cpp(kth_opcode_reserved_242) == kth::domain::machine::opcode::reserved_242);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_243) == kth_opcode_reserved_243);
static_assert(opcode_to_cpp(kth_opcode_reserved_243) == kth::domain::machine::opcode::reserved_243);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_244) == kth_opcode_reserved_244);
static_assert(opcode_to_cpp(kth_opcode_reserved_244) == kth::domain::machine::opcode::reserved_244);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_245) == kth_opcode_reserved_245);
static_assert(opcode_to_cpp(kth_opcode_reserved_245) == kth::domain::machine::opcode::reserved_245);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_246) == kth_opcode_reserved_246);
static_assert(opcode_to_cpp(kth_opcode_reserved_246) == kth::domain::machine::opcode::reserved_246);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_247) == kth_opcode_reserved_247);
static_assert(opcode_to_cpp(kth_opcode_reserved_247) == kth::domain::machine::opcode::reserved_247);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_248) == kth_opcode_reserved_248);
static_assert(opcode_to_cpp(kth_opcode_reserved_248) == kth::domain::machine::opcode::reserved_248);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_249) == kth_opcode_reserved_249);
static_assert(opcode_to_cpp(kth_opcode_reserved_249) == kth::domain::machine::opcode::reserved_249);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_250) == kth_opcode_reserved_250);
static_assert(opcode_to_cpp(kth_opcode_reserved_250) == kth::domain::machine::opcode::reserved_250);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_251) == kth_opcode_reserved_251);
static_assert(opcode_to_cpp(kth_opcode_reserved_251) == kth::domain::machine::opcode::reserved_251);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_252) == kth_opcode_reserved_252);
static_assert(opcode_to_cpp(kth_opcode_reserved_252) == kth::domain::machine::opcode::reserved_252);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_253) == kth_opcode_reserved_253);
static_assert(opcode_to_cpp(kth_opcode_reserved_253) == kth::domain::machine::opcode::reserved_253);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_254) == kth_opcode_reserved_254);
static_assert(opcode_to_cpp(kth_opcode_reserved_254) == kth::domain::machine::opcode::reserved_254);
static_assert(opcode_to_c(kth::domain::machine::opcode::reserved_255) == kth_opcode_reserved_255);
static_assert(opcode_to_cpp(kth_opcode_reserved_255) == kth::domain::machine::opcode::reserved_255);
static_assert(opcode_to_c(kth::domain::machine::opcode::invalidopcode) == kth_opcode_reserved_255);
static_assert(opcode_to_cpp(kth_opcode_reserved_255) == kth::domain::machine::opcode::invalidopcode);

// `script_flags` is a bitfield: any bit pattern is a meaningful (if
// possibly unknown) value and the interpreter ignores bits it does not
// recognise, so validating against a fixed set of cases would be wrong.
inline constexpr kth::domain::machine::script_flags script_flags_to_cpp(kth_script_flags_t x) noexcept {
    return ecast<kth::domain::machine::script_flags>(x);
}

inline constexpr kth_script_flags_t script_flags_to_c(kth::domain::machine::script_flags x) noexcept {
    return ecast<kth_script_flags_t>(x);
}

// Numeric ABI: every named enumerator converts to its counterpart, so the C and
// C++ values cannot drift apart while the names keep matching.
static_assert(sizeof(std::underlying_type_t<kth::domain::machine::script_flags>) <= sizeof(kth_script_flags_t));
static_assert(script_flags_to_c(kth::domain::machine::script_flags::no_rules) == kth_script_flags_no_rules);
static_assert(script_flags_to_cpp(kth_script_flags_no_rules) == kth::domain::machine::script_flags::no_rules);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::easy_blocks) == kth_script_flags_easy_blocks);
static_assert(script_flags_to_cpp(kth_script_flags_easy_blocks) == kth::domain::machine::script_flags::easy_blocks);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip16_rule) == kth_script_flags_bip16_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip16_rule) == kth::domain::machine::script_flags::bip16_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip30_rule) == kth_script_flags_bip30_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip30_rule) == kth::domain::machine::script_flags::bip30_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip34_rule) == kth_script_flags_bip34_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip34_rule) == kth::domain::machine::script_flags::bip34_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip66_rule) == kth_script_flags_bip66_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip66_rule) == kth::domain::machine::script_flags::bip66_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip65_rule) == kth_script_flags_bip65_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip65_rule) == kth::domain::machine::script_flags::bip65_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip90_rule) == kth_script_flags_bip90_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip90_rule) == kth::domain::machine::script_flags::bip90_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::allow_collisions) == kth_script_flags_allow_collisions);
static_assert(script_flags_to_cpp(kth_script_flags_allow_collisions) == kth::domain::machine::script_flags::allow_collisions);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip68_rule) == kth_script_flags_bip68_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip68_rule) == kth::domain::machine::script_flags::bip68_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip112_rule) == kth_script_flags_bip112_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip112_rule) == kth::domain::machine::script_flags::bip112_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip113_rule) == kth_script_flags_bip113_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip113_rule) == kth::domain::machine::script_flags::bip113_rule);
#if defined(KTH_CURRENCY_BCH)
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_strictenc) == kth_script_flags_bch_strictenc);
static_assert(script_flags_to_cpp(kth_script_flags_bch_strictenc) == kth::domain::machine::script_flags::bch_strictenc);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_sighash_forkid) == kth_script_flags_bch_sighash_forkid);
static_assert(script_flags_to_cpp(kth_script_flags_bch_sighash_forkid) == kth::domain::machine::script_flags::bch_sighash_forkid);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_low_s) == kth_script_flags_bch_low_s);
static_assert(script_flags_to_cpp(kth_script_flags_bch_low_s) == kth::domain::machine::script_flags::bch_low_s);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_nullfail) == kth_script_flags_bch_nullfail);
static_assert(script_flags_to_cpp(kth_script_flags_bch_nullfail) == kth::domain::machine::script_flags::bch_nullfail);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_reactivated_opcodes) == kth_script_flags_bch_reactivated_opcodes);
static_assert(script_flags_to_cpp(kth_script_flags_bch_reactivated_opcodes) == kth::domain::machine::script_flags::bch_reactivated_opcodes);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_sigpushonly) == kth_script_flags_bch_sigpushonly);
static_assert(script_flags_to_cpp(kth_script_flags_bch_sigpushonly) == kth::domain::machine::script_flags::bch_sigpushonly);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_cleanstack) == kth_script_flags_bch_cleanstack);
static_assert(script_flags_to_cpp(kth_script_flags_bch_cleanstack) == kth::domain::machine::script_flags::bch_cleanstack);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_schnorr_multisig) == kth_script_flags_bch_schnorr_multisig);
static_assert(script_flags_to_cpp(kth_script_flags_bch_schnorr_multisig) == kth::domain::machine::script_flags::bch_schnorr_multisig);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_minimaldata) == kth_script_flags_bch_minimaldata);
static_assert(script_flags_to_cpp(kth_script_flags_bch_minimaldata) == kth::domain::machine::script_flags::bch_minimaldata);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_enforce_sigchecks) == kth_script_flags_bch_enforce_sigchecks);
static_assert(script_flags_to_cpp(kth_script_flags_bch_enforce_sigchecks) == kth::domain::machine::script_flags::bch_enforce_sigchecks);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_64bit_integers) == kth_script_flags_bch_64bit_integers);
static_assert(script_flags_to_cpp(kth_script_flags_bch_64bit_integers) == kth::domain::machine::script_flags::bch_64bit_integers);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_native_introspection) == kth_script_flags_bch_native_introspection);
static_assert(script_flags_to_cpp(kth_script_flags_bch_native_introspection) == kth::domain::machine::script_flags::bch_native_introspection);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_p2sh_32) == kth_script_flags_bch_p2sh_32);
static_assert(script_flags_to_cpp(kth_script_flags_bch_p2sh_32) == kth::domain::machine::script_flags::bch_p2sh_32);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_tokens) == kth_script_flags_bch_tokens);
static_assert(script_flags_to_cpp(kth_script_flags_bch_tokens) == kth::domain::machine::script_flags::bch_tokens);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_vm_limits) == kth_script_flags_bch_vm_limits);
static_assert(script_flags_to_cpp(kth_script_flags_bch_vm_limits) == kth::domain::machine::script_flags::bch_vm_limits);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_bigint) == kth_script_flags_bch_bigint);
static_assert(script_flags_to_cpp(kth_script_flags_bch_bigint) == kth::domain::machine::script_flags::bch_bigint);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_loops) == kth_script_flags_bch_loops);
static_assert(script_flags_to_cpp(kth_script_flags_bch_loops) == kth::domain::machine::script_flags::bch_loops);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_subroutines) == kth_script_flags_bch_subroutines);
static_assert(script_flags_to_cpp(kth_script_flags_bch_subroutines) == kth::domain::machine::script_flags::bch_subroutines);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_bitwise_ops) == kth_script_flags_bch_bitwise_ops);
static_assert(script_flags_to_cpp(kth_script_flags_bch_bitwise_ops) == kth::domain::machine::script_flags::bch_bitwise_ops);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_p2s) == kth_script_flags_bch_p2s);
static_assert(script_flags_to_cpp(kth_script_flags_bch_p2s) == kth::domain::machine::script_flags::bch_p2s);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_2027_may) == kth_script_flags_bch_2027_may);
static_assert(script_flags_to_cpp(kth_script_flags_bch_2027_may) == kth::domain::machine::script_flags::bch_2027_may);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_vm_limits_standard) == kth_script_flags_bch_vm_limits_standard);
static_assert(script_flags_to_cpp(kth_script_flags_bch_vm_limits_standard) == kth::domain::machine::script_flags::bch_vm_limits_standard);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_minimalif) == kth_script_flags_bch_minimalif);
static_assert(script_flags_to_cpp(kth_script_flags_bch_minimalif) == kth::domain::machine::script_flags::bch_minimalif);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_input_sigchecks) == kth_script_flags_bch_input_sigchecks);
static_assert(script_flags_to_cpp(kth_script_flags_bch_input_sigchecks) == kth::domain::machine::script_flags::bch_input_sigchecks);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_discourage_upgradable_nops) == kth_script_flags_bch_discourage_upgradable_nops);
static_assert(script_flags_to_cpp(kth_script_flags_bch_discourage_upgradable_nops) == kth::domain::machine::script_flags::bch_discourage_upgradable_nops);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bch_disallow_segwit_recovery) == kth_script_flags_bch_disallow_segwit_recovery);
static_assert(script_flags_to_cpp(kth_script_flags_bch_disallow_segwit_recovery) == kth::domain::machine::script_flags::bch_disallow_segwit_recovery);
#else
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip141_rule) == kth_script_flags_bip141_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip141_rule) == kth::domain::machine::script_flags::bip141_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip143_rule) == kth_script_flags_bip143_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip143_rule) == kth::domain::machine::script_flags::bip143_rule);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::bip147_rule) == kth_script_flags_bip147_rule);
static_assert(script_flags_to_cpp(kth_script_flags_bip147_rule) == kth::domain::machine::script_flags::bip147_rule);
#endif
static_assert(script_flags_to_c(kth::domain::machine::script_flags::retarget) == kth_script_flags_retarget);
static_assert(script_flags_to_cpp(kth_script_flags_retarget) == kth::domain::machine::script_flags::retarget);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::unverified) == kth_script_flags_unverified);
static_assert(script_flags_to_cpp(kth_script_flags_unverified) == kth::domain::machine::script_flags::unverified);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::all_policy_flags) == kth_script_flags_all_policy_flags);
static_assert(script_flags_to_cpp(kth_script_flags_all_policy_flags) == kth::domain::machine::script_flags::all_policy_flags);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::all_rules) == kth_script_flags_all_rules);
static_assert(script_flags_to_cpp(kth_script_flags_all_rules) == kth::domain::machine::script_flags::all_rules);
static_assert(script_flags_to_c(kth::domain::machine::script_flags::all_bits) == kth_script_flags_all_bits);
static_assert(script_flags_to_cpp(kth_script_flags_all_bits) == kth::domain::machine::script_flags::all_bits);

// Out-of-range C values map to `non_standard`.
inline constexpr kth::domain::machine::script_pattern script_pattern_to_cpp(kth_script_pattern_t x) noexcept {
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

inline constexpr kth_script_pattern_t script_pattern_to_c(kth::domain::machine::script_pattern x) noexcept {
    return ecast<kth_script_pattern_t>(x);
}

// Numeric ABI: every named enumerator converts to its counterpart, so the C and
// C++ values cannot drift apart while the names keep matching.
static_assert(sizeof(std::underlying_type_t<kth::domain::machine::script_pattern>) <= sizeof(kth_script_pattern_t));
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::null_data) == kth_script_pattern_null_data);
static_assert(script_pattern_to_cpp(kth_script_pattern_null_data) == kth::domain::machine::script_pattern::null_data);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::pay_to_multisig) == kth_script_pattern_pay_to_multisig);
static_assert(script_pattern_to_cpp(kth_script_pattern_pay_to_multisig) == kth::domain::machine::script_pattern::pay_to_multisig);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::pay_to_public_key) == kth_script_pattern_pay_to_public_key);
static_assert(script_pattern_to_cpp(kth_script_pattern_pay_to_public_key) == kth::domain::machine::script_pattern::pay_to_public_key);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::pay_to_public_key_hash) == kth_script_pattern_pay_to_public_key_hash);
static_assert(script_pattern_to_cpp(kth_script_pattern_pay_to_public_key_hash) == kth::domain::machine::script_pattern::pay_to_public_key_hash);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::pay_to_script_hash) == kth_script_pattern_pay_to_script_hash);
static_assert(script_pattern_to_cpp(kth_script_pattern_pay_to_script_hash) == kth::domain::machine::script_pattern::pay_to_script_hash);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::pay_to_script_hash_32) == kth_script_pattern_pay_to_script_hash_32);
static_assert(script_pattern_to_cpp(kth_script_pattern_pay_to_script_hash_32) == kth::domain::machine::script_pattern::pay_to_script_hash_32);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::pay_to_script) == kth_script_pattern_pay_to_script);
static_assert(script_pattern_to_cpp(kth_script_pattern_pay_to_script) == kth::domain::machine::script_pattern::pay_to_script);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::sign_multisig) == kth_script_pattern_sign_multisig);
static_assert(script_pattern_to_cpp(kth_script_pattern_sign_multisig) == kth::domain::machine::script_pattern::sign_multisig);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::sign_public_key) == kth_script_pattern_sign_public_key);
static_assert(script_pattern_to_cpp(kth_script_pattern_sign_public_key) == kth::domain::machine::script_pattern::sign_public_key);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::sign_public_key_hash) == kth_script_pattern_sign_public_key_hash);
static_assert(script_pattern_to_cpp(kth_script_pattern_sign_public_key_hash) == kth::domain::machine::script_pattern::sign_public_key_hash);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::sign_script_hash) == kth_script_pattern_sign_script_hash);
static_assert(script_pattern_to_cpp(kth_script_pattern_sign_script_hash) == kth::domain::machine::script_pattern::sign_script_hash);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::witness_reservation) == kth_script_pattern_witness_reservation);
static_assert(script_pattern_to_cpp(kth_script_pattern_witness_reservation) == kth::domain::machine::script_pattern::witness_reservation);
static_assert(script_pattern_to_c(kth::domain::machine::script_pattern::non_standard) == kth_script_pattern_non_standard);
static_assert(script_pattern_to_cpp(kth_script_pattern_non_standard) == kth::domain::machine::script_pattern::non_standard);

} // namespace kth

#endif /* KTH_CAPI_CONVERSIONS_MACHINE_HPP_ */
