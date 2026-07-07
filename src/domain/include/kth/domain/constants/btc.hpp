// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONSTANTS_BTC_HPP_
#define KTH_DOMAIN_CONSTANTS_BTC_HPP_

#include <cstddef>
#include <cstdint>

#include <kth/domain/constants/bch_btc.hpp>
#include <kth/domain/constants/common.hpp>
#include <kth/domain/constants/segwit.hpp>

namespace kth {

constexpr size_t max_block_size = 1000000;  //one million bytes
constexpr size_t max_block_sigops = max_block_size / max_sigops_factor;

constexpr uint32_t bip9_version_bit0 = 1U << 0;
constexpr uint32_t bip9_version_bit1 = 1U << 1;
constexpr uint32_t bip9_version_base = 0x20000000;


// These cannot be reactivated in a future branch due to window expiration.
static
const infrastructure::config::checkpoint mainnet_bip9_bit0_active_checkpoint{
    "000000000000000004a1b34462cb8aeebd5799177f7a29cf28f2d1961716b5b5"_hash, 419328};

static
const infrastructure::config::checkpoint testnet_bip9_bit0_active_checkpoint{
    "00000000025e930139bac5c6c31a403776da130831ab85be56578f3fa75369bb"_hash, 770112};

static
const infrastructure::config::checkpoint regtest_bip9_bit0_active_checkpoint{
    // The activation window is fixed and closed, so assume genesis activation.
    "06226e46111a0b59caaf126043eb5bbf28c34f3a5e332a1fc7b2b73cf188910f"_hash, 0};

// static
// const infrastructure::config::checkpoint testnet4_bip9_bit0_active_checkpoint{
//     // The activation window is fixed and closed, so assume genesis activation.
//     "06226e46111a0b59caaf126043eb5bbf28c34f3a5e332a1fc7b2b73cf188910f", 0};

// These cannot be reactivated in a future branch due to window expiration.
static
const infrastructure::config::checkpoint mainnet_bip9_bit1_active_checkpoint{
    "0000000000000000001c8018d9cb3b742ef25114f27563e3fc4a1902167f9893"_hash, 481824};

static
const infrastructure::config::checkpoint testnet_bip9_bit1_active_checkpoint{
    "00000000002b980fcd729daaa248fd9316a5200e9b367f4ff2c42453e84201ca"_hash, 834624};

static
const infrastructure::config::checkpoint regtest_bip9_bit1_active_checkpoint{
    // The activation window is fixed and closed, so assume genesis activation.
    "06226e46111a0b59caaf126043eb5bbf28c34f3a5e332a1fc7b2b73cf188910f"_hash, 0};

} // namespace kth

#endif
