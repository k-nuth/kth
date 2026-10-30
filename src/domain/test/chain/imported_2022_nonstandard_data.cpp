// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

// Each data fragment below has exactly one owning translation unit.
#include "vmb_vectors.hpp"
#include "vmb_auto_2022_nonstandard_before_chip_cashtokens.cpp"

namespace kth::test {
namespace {
static constexpr vmb_vector_group groups[] = {
    data_vmb_2022_nonstandard_0::group,
};
} // namespace

std::span<vmb_vector_group const> vmb_2022_nonstandard_vectors() {
    return groups;
}
} // namespace kth::test
