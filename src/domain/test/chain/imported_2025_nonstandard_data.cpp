// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

// Each data fragment below has exactly one owning translation unit.
#include "vmb_vectors.hpp"
#include "vmb_auto_2025_nonstandard_chip_flow_control.cpp"
#include "vmb_auto_2025_nonstandard_core_benchmarks_arithmetic_add_sub.cpp"
#include "vmb_auto_2025_nonstandard_core_benchmarks_arithmetic_div_mod.cpp"
#include "vmb_auto_2025_nonstandard_core_benchmarks_arithmetic_mul.cpp"
#include "vmb_auto_2025_nonstandard_core_benchmarks_hashing.cpp"
#include "vmb_auto_2025_nonstandard_core_benchmarks_hashing_bytes_packed.cpp"
#include "vmb_auto_2025_nonstandard_core_benchmarks_hashing_iters_packed.cpp"
#include "vmb_auto_2025_nonstandard_core_benchmarks_stack.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_0notequal.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_1add.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_1sub.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_abs.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_add.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_basics.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_bin2num.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_booland.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_boolor.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_div.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_greaterthan.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_greaterthanorequal.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_lessthan.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_lessthanorequal.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_limits_binary.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_limits_ternary.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_limits_unary.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_max.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_min.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_mod.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_mul.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_negate.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_not.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_num2bin.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_numequal.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_numequalverify.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_numnotequal.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_sub.cpp"
#include "vmb_auto_2025_nonstandard_core_bigint_within.cpp"
#include "vmb_auto_2025_nonstandard_core_cashtokens.cpp"
#include "vmb_auto_2025_nonstandard_core_conditionals.cpp"
#include "vmb_auto_2025_nonstandard_core_data_signatures.cpp"
#include "vmb_auto_2025_nonstandard_core_disabled.cpp"
#include "vmb_auto_2025_nonstandard_core_formatting.cpp"
#include "vmb_auto_2025_nonstandard_core_hashing.cpp"
#include "vmb_auto_2025_nonstandard_core_inspection.cpp"
#include "vmb_auto_2025_nonstandard_core_limits.cpp"
#include "vmb_auto_2025_nonstandard_core_nop.cpp"
#include "vmb_auto_2025_nonstandard_core_push_bytes.cpp"
#include "vmb_auto_2025_nonstandard_core_push_data.cpp"
#include "vmb_auto_2025_nonstandard_core_push_data_limits.cpp"
#include "vmb_auto_2025_nonstandard_core_push_minimal.cpp"
#include "vmb_auto_2025_nonstandard_core_push_numbers.cpp"
#include "vmb_auto_2025_nonstandard_core_push_ops.cpp"
#include "vmb_auto_2025_nonstandard_core_signature_checking_multisig_m_of_15.cpp"
#include "vmb_auto_2025_nonstandard_core_signature_checking_multisig_m_of_20.cpp"
#include "vmb_auto_2025_nonstandard_core_signature_checking_multisig_pubkey_validation.cpp"
#include "vmb_auto_2025_nonstandard_core_signing_serialization.cpp"

namespace kth::test {
namespace {
static constexpr vmb_vector_group groups[] = {
    data_vmb_2025_nonstandard_0::group,
    data_vmb_2025_nonstandard_1::group,
    data_vmb_2025_nonstandard_2::group,
    data_vmb_2025_nonstandard_3::group,
    data_vmb_2025_nonstandard_4::group,
    data_vmb_2025_nonstandard_5::group,
    data_vmb_2025_nonstandard_6::group,
    data_vmb_2025_nonstandard_7::group,
    data_vmb_2025_nonstandard_8::group,
    data_vmb_2025_nonstandard_9::group,
    data_vmb_2025_nonstandard_10::group,
    data_vmb_2025_nonstandard_11::group,
    data_vmb_2025_nonstandard_12::group,
    data_vmb_2025_nonstandard_13::group,
    data_vmb_2025_nonstandard_14::group,
    data_vmb_2025_nonstandard_15::group,
    data_vmb_2025_nonstandard_16::group,
    data_vmb_2025_nonstandard_17::group,
    data_vmb_2025_nonstandard_18::group,
    data_vmb_2025_nonstandard_19::group,
    data_vmb_2025_nonstandard_20::group,
    data_vmb_2025_nonstandard_21::group,
    data_vmb_2025_nonstandard_22::group,
    data_vmb_2025_nonstandard_23::group,
    data_vmb_2025_nonstandard_24::group,
    data_vmb_2025_nonstandard_25::group,
    data_vmb_2025_nonstandard_26::group,
    data_vmb_2025_nonstandard_27::group,
    data_vmb_2025_nonstandard_28::group,
    data_vmb_2025_nonstandard_29::group,
    data_vmb_2025_nonstandard_30::group,
    data_vmb_2025_nonstandard_31::group,
    data_vmb_2025_nonstandard_32::group,
    data_vmb_2025_nonstandard_33::group,
    data_vmb_2025_nonstandard_34::group,
    data_vmb_2025_nonstandard_35::group,
    data_vmb_2025_nonstandard_36::group,
    data_vmb_2025_nonstandard_37::group,
    data_vmb_2025_nonstandard_38::group,
    data_vmb_2025_nonstandard_39::group,
    data_vmb_2025_nonstandard_40::group,
    data_vmb_2025_nonstandard_41::group,
    data_vmb_2025_nonstandard_42::group,
    data_vmb_2025_nonstandard_43::group,
    data_vmb_2025_nonstandard_44::group,
    data_vmb_2025_nonstandard_45::group,
    data_vmb_2025_nonstandard_46::group,
    data_vmb_2025_nonstandard_47::group,
    data_vmb_2025_nonstandard_48::group,
    data_vmb_2025_nonstandard_49::group,
    data_vmb_2025_nonstandard_50::group,
    data_vmb_2025_nonstandard_51::group,
    data_vmb_2025_nonstandard_52::group,
    data_vmb_2025_nonstandard_53::group,
    data_vmb_2025_nonstandard_54::group,
    data_vmb_2025_nonstandard_55::group,
};
} // namespace

std::span<vmb_vector_group const> vmb_2025_nonstandard_vectors() {
    return groups;
}
} // namespace kth::test
