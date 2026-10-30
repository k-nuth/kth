// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_TEST_VMB_VECTORS_HPP
#define KTH_TEST_VMB_VECTORS_HPP
#include <cstdint>
#include <span>

namespace kth::test {
struct vmb_vector {
    char const* ident;
    char const* description;
    char const* tx_hex;
    char const* utxos_hex;
    uint32_t input_num;
    bool script_only;
};
struct vmb_vector_group {
    char const* source;
    std::span<vmb_vector const> rows;
    uint64_t flags;
    bool expect_pass;
};
std::span<vmb_vector_group const> vmb_2022_invalid_vectors();
std::span<vmb_vector_group const> vmb_2022_nonstandard_vectors();
std::span<vmb_vector_group const> vmb_2022_standard_vectors();
std::span<vmb_vector_group const> vmb_2023_invalid_vectors();
std::span<vmb_vector_group const> vmb_2023_nonstandard_vectors();
std::span<vmb_vector_group const> vmb_2023_standard_vectors();
std::span<vmb_vector_group const> vmb_2025_invalid_vectors();
std::span<vmb_vector_group const> vmb_2025_nonstandard_vectors();
std::span<vmb_vector_group const> vmb_2025_standard_vectors();
std::span<vmb_vector_group const> vmb_2026_invalid_vectors();
std::span<vmb_vector_group const> vmb_2026_nonstandard_vectors();
std::span<vmb_vector_group const> vmb_2026_standard_vectors();
} // namespace kth::test
#endif
