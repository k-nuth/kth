// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

// Each data fragment below has exactly one owning translation unit.
#include "vmb_vectors.hpp"
#include "vmb_auto_2026_standard_chip_benchmarks_bitwise.cpp"
#include "vmb_auto_2026_standard_chip_bitwise.cpp"
#include "vmb_auto_2026_standard_chip_flow_control.cpp"
#include "vmb_auto_2026_standard_chip_functions.cpp"
#include "vmb_auto_2026_standard_chip_loops.cpp"
#include "vmb_auto_2026_standard_chip_p2s.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_arithmetic_add_sub.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_arithmetic_div_mod.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_arithmetic_mul.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_baseline.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_bitwise.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_hashing.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_roll.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_signature_checking_bms_ecdsa.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_signature_checking_bms_schnorr.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_signature_checking_p2pk.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_signature_checking_p2pkh.cpp"
#include "vmb_auto_2026_standard_core_benchmarks_stack.cpp"
#include "vmb_auto_2026_standard_core_bigint_0notequal.cpp"
#include "vmb_auto_2026_standard_core_bigint_1add.cpp"
#include "vmb_auto_2026_standard_core_bigint_1sub.cpp"
#include "vmb_auto_2026_standard_core_bigint_abs.cpp"
#include "vmb_auto_2026_standard_core_bigint_add.cpp"
#include "vmb_auto_2026_standard_core_bigint_basics.cpp"
#include "vmb_auto_2026_standard_core_bigint_bin2num.cpp"
#include "vmb_auto_2026_standard_core_bigint_booland.cpp"
#include "vmb_auto_2026_standard_core_bigint_boolor.cpp"
#include "vmb_auto_2026_standard_core_bigint_div.cpp"
#include "vmb_auto_2026_standard_core_bigint_greaterthan.cpp"
#include "vmb_auto_2026_standard_core_bigint_greaterthanorequal.cpp"
#include "vmb_auto_2026_standard_core_bigint_lessthan.cpp"
#include "vmb_auto_2026_standard_core_bigint_lessthanorequal.cpp"
#include "vmb_auto_2026_standard_core_bigint_limits_binary.cpp"
#include "vmb_auto_2026_standard_core_bigint_limits_ternary.cpp"
#include "vmb_auto_2026_standard_core_bigint_limits_unary.cpp"
#include "vmb_auto_2026_standard_core_bigint_max.cpp"
#include "vmb_auto_2026_standard_core_bigint_min.cpp"
#include "vmb_auto_2026_standard_core_bigint_mod.cpp"
#include "vmb_auto_2026_standard_core_bigint_mul.cpp"
#include "vmb_auto_2026_standard_core_bigint_negate.cpp"
#include "vmb_auto_2026_standard_core_bigint_not.cpp"
#include "vmb_auto_2026_standard_core_bigint_num2bin.cpp"
#include "vmb_auto_2026_standard_core_bigint_numequal.cpp"
#include "vmb_auto_2026_standard_core_bigint_numequalverify.cpp"
#include "vmb_auto_2026_standard_core_bigint_numnotequal.cpp"
#include "vmb_auto_2026_standard_core_bigint_sub.cpp"
#include "vmb_auto_2026_standard_core_bigint_within.cpp"
#include "vmb_auto_2026_standard_core_cashtokens.cpp"
#include "vmb_auto_2026_standard_core_conditionals.cpp"
#include "vmb_auto_2026_standard_core_data_signatures.cpp"
#include "vmb_auto_2026_standard_core_disabled.cpp"
#include "vmb_auto_2026_standard_core_formatting.cpp"
#include "vmb_auto_2026_standard_core_hashing.cpp"
#include "vmb_auto_2026_standard_core_inspection.cpp"
#include "vmb_auto_2026_standard_core_limits.cpp"
#include "vmb_auto_2026_standard_core_nop.cpp"
#include "vmb_auto_2026_standard_core_push_bytes.cpp"
#include "vmb_auto_2026_standard_core_push_data.cpp"
#include "vmb_auto_2026_standard_core_push_data_limits.cpp"
#include "vmb_auto_2026_standard_core_push_minimal.cpp"
#include "vmb_auto_2026_standard_core_push_numbers.cpp"
#include "vmb_auto_2026_standard_core_push_ops.cpp"
#include "vmb_auto_2026_standard_core_signature_checking_multisig_m_of_15.cpp"
#include "vmb_auto_2026_standard_core_signature_checking_multisig_m_of_20.cpp"
#include "vmb_auto_2026_standard_core_signature_checking_multisig_m_of_3.cpp"
#include "vmb_auto_2026_standard_core_signature_checking_multisig_pubkey_validation.cpp"
#include "vmb_auto_2026_standard_core_signature_checking_multisig_signing_serialization.cpp"
#include "vmb_auto_2026_standard_core_signing_serialization.cpp"

namespace kth::test {
namespace {
static constexpr vmb_vector_group groups[] = {
    data_vmb_2026_standard_0::group,
    data_vmb_2026_standard_1::group,
    data_vmb_2026_standard_2::group,
    data_vmb_2026_standard_3::group,
    data_vmb_2026_standard_4::group,
    data_vmb_2026_standard_5::group,
    data_vmb_2026_standard_6::group,
    data_vmb_2026_standard_7::group,
    data_vmb_2026_standard_8::group,
    data_vmb_2026_standard_9::group,
    data_vmb_2026_standard_10::group,
    data_vmb_2026_standard_11::group,
    data_vmb_2026_standard_12::group,
    data_vmb_2026_standard_13::group,
    data_vmb_2026_standard_14::group,
    data_vmb_2026_standard_15::group,
    data_vmb_2026_standard_16::group,
    data_vmb_2026_standard_17::group,
    data_vmb_2026_standard_18::group,
    data_vmb_2026_standard_19::group,
    data_vmb_2026_standard_20::group,
    data_vmb_2026_standard_21::group,
    data_vmb_2026_standard_22::group,
    data_vmb_2026_standard_23::group,
    data_vmb_2026_standard_24::group,
    data_vmb_2026_standard_25::group,
    data_vmb_2026_standard_26::group,
    data_vmb_2026_standard_27::group,
    data_vmb_2026_standard_28::group,
    data_vmb_2026_standard_29::group,
    data_vmb_2026_standard_30::group,
    data_vmb_2026_standard_31::group,
    data_vmb_2026_standard_32::group,
    data_vmb_2026_standard_33::group,
    data_vmb_2026_standard_34::group,
    data_vmb_2026_standard_35::group,
    data_vmb_2026_standard_36::group,
    data_vmb_2026_standard_37::group,
    data_vmb_2026_standard_38::group,
    data_vmb_2026_standard_39::group,
    data_vmb_2026_standard_40::group,
    data_vmb_2026_standard_41::group,
    data_vmb_2026_standard_42::group,
    data_vmb_2026_standard_43::group,
    data_vmb_2026_standard_44::group,
    data_vmb_2026_standard_45::group,
    data_vmb_2026_standard_46::group,
    data_vmb_2026_standard_47::group,
    data_vmb_2026_standard_48::group,
    data_vmb_2026_standard_49::group,
    data_vmb_2026_standard_50::group,
    data_vmb_2026_standard_51::group,
    data_vmb_2026_standard_52::group,
    data_vmb_2026_standard_53::group,
    data_vmb_2026_standard_54::group,
    data_vmb_2026_standard_55::group,
    data_vmb_2026_standard_56::group,
    data_vmb_2026_standard_57::group,
    data_vmb_2026_standard_58::group,
    data_vmb_2026_standard_59::group,
    data_vmb_2026_standard_60::group,
    data_vmb_2026_standard_61::group,
    data_vmb_2026_standard_62::group,
    data_vmb_2026_standard_63::group,
    data_vmb_2026_standard_64::group,
    data_vmb_2026_standard_65::group,
    data_vmb_2026_standard_66::group,
    data_vmb_2026_standard_67::group,
};
} // namespace

std::span<vmb_vector_group const> vmb_2026_standard_vectors() {
    return groups;
}
} // namespace kth::test
