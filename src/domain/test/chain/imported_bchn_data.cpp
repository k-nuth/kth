// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

// Each data fragment below has exactly one owning translation unit.
#include "bchn_vectors.hpp"
#include "script_vm_auto_0.cpp"
#include "script_vm_auto_1.cpp"
#include "script_vm_auto_10.cpp"
#include "script_vm_auto_11.cpp"
#include "script_vm_auto_12.cpp"
#include "script_vm_auto_13.cpp"
#include "script_vm_auto_14.cpp"
#include "script_vm_auto_15.cpp"
#include "script_vm_auto_16.cpp"
#include "script_vm_auto_17.cpp"
#include "script_vm_auto_2.cpp"
#include "script_vm_auto_3.cpp"
#include "script_vm_auto_4.cpp"
#include "script_vm_auto_5.cpp"
#include "script_vm_auto_6.cpp"
#include "script_vm_auto_7.cpp"
#include "script_vm_auto_8.cpp"
#include "script_vm_auto_9.cpp"

namespace kth::test {
namespace {
static constexpr bchn_vector_group groups[] = {
    data_bchn_bchn_0::group,
    data_bchn_bchn_1::group,
    data_bchn_bchn_2::group,
    data_bchn_bchn_3::group,
    data_bchn_bchn_4::group,
    data_bchn_bchn_5::group,
    data_bchn_bchn_6::group,
    data_bchn_bchn_7::group,
    data_bchn_bchn_8::group,
    data_bchn_bchn_9::group,
    data_bchn_bchn_10::group,
    data_bchn_bchn_11::group,
    data_bchn_bchn_12::group,
    data_bchn_bchn_13::group,
    data_bchn_bchn_14::group,
    data_bchn_bchn_15::group,
    data_bchn_bchn_16::group,
    data_bchn_bchn_17::group,
};
} // namespace

std::span<bchn_vector_group const> bchn_vectors() {
    return groups;
}
} // namespace kth::test
