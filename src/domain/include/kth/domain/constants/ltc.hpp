// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONSTANTS_LTC_HPP_
#define KTH_DOMAIN_CONSTANTS_LTC_HPP_

#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/config/checkpoint.hpp>

namespace kth {

constexpr size_t max_block_size = 1000000;  //one million bytes

//0x00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
constexpr uint32_t retarget_proof_of_work_limit = 0x1e0fffff;
constexpr uint32_t no_retarget_proof_of_work_limit = 0x207fffff;
constexpr size_t max_block_sigops = max_block_size / max_sigops_factor;
constexpr uint32_t target_spacing_seconds = 10 * 15;
constexpr uint32_t target_timespan_seconds = 2 * 7 * 24 * 60 * 15;

// Mainnet activation parameters (bip34-style activations).
constexpr size_t mainnet_active = 750;
constexpr size_t mainnet_enforce = 950;
constexpr size_t mainnet_sample = 1000;

// Testnet activation parameters (bip34-style activations).
constexpr size_t testnet_active = 51;
constexpr size_t testnet_enforce = 75;
constexpr size_t testnet_sample = 100;

// Regtest (arbitrary) frozen activation heights (frozen_activations).
constexpr size_t regtest_bip65_freeze = 1351;
constexpr size_t regtest_bip66_freeze = 1251;
constexpr size_t regtest_bip34_freeze = 100000000;

// Mainnet frozen activation heights (frozen_activations).
constexpr size_t mainnet_bip65_freeze = MAX_UINT32;  //Not Active
constexpr size_t mainnet_bip66_freeze = MAX_UINT32;  //Not Active
constexpr size_t mainnet_bip34_freeze = 710000;

// Testnet frozen activation heights (frozen_activations).
constexpr size_t testnet_bip65_freeze = MAX_UINT32;  //Not Active
constexpr size_t testnet_bip66_freeze = MAX_UINT32;  //Not Active
constexpr size_t testnet_bip34_freeze = 76;          //Always Active

// Block 514 is the first testnet block after date-based activation.
// Block 173805 is the first mainnet block after date-based activation.
constexpr uint32_t bip16_activation_time = 0x4f779a80;


constexpr uint32_t bip9_version_bit0 = 1U << 0;
constexpr uint32_t bip9_version_bit1 = 1U << 1;
constexpr uint32_t bip9_version_base = 0x20000000;


// Block 170060 was mined with an invalid p2sh (code shipped late).
// bitcointalk.org/index.php?topic=63165.msg788832#msg788832
static
const infrastructure::config::checkpoint mainnet_bip16_exception_checkpoint{
    "00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22", 170060};

// github.com/bitcoin/bips/blob/master/bip-0030.mediawiki#specification
static
const infrastructure::config::checkpoint mainnet_bip30_exception_checkpoint1{
    // TODO(legacy): figure out why this block validates without an exception.
    "00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec", 91842};

static
const infrastructure::config::checkpoint mainnet_bip30_exception_checkpoint2{
    "00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721", 91880};

// Hard fork to stop checking unspent duplicates above fixed bip34 activation.
static
const infrastructure::config::checkpoint mainnet_bip34_active_checkpoint{
    "fa09d204a83a768ed5a7c8d441fa62f2043abf420cff1226c7b4329aeb9d51cf", 710000};

static
const infrastructure::config::checkpoint testnet_bip34_active_checkpoint{
    "8075c771ed8b495ffd943980a95f702ab34fce3c8c54e379548bda33cc8c0573", 76};

static
const infrastructure::config::checkpoint regtest_bip34_active_checkpoint{
    "fa09d204a83a768ed5a7c8d441fa62f2043abf420cff1226c7b4329aeb9d51cf", 100000000};

// These cannot be reactivated in a future branch due to window expiration.
static
const infrastructure::config::checkpoint mainnet_bip9_bit0_active_checkpoint{
    "b50ce9202c152e481ca509156028af954654ed13e4b0656eb497554aa753db0b", 1201535};
static
const infrastructure::config::checkpoint testnet_bip9_bit0_active_checkpoint{
    "4966625a4b2851d9fdee139e56211a0d88575f59ed816ff5e6a63deb4e3e29a0", 0};
static
const infrastructure::config::checkpoint regtest_bip9_bit0_active_checkpoint{
    // The activation window is fixed and closed, so assume genesis activation.
    "06226e46111a0b59caaf126043eb5bbf28c34f3a5e332a1fc7b2b73cf188910f", 0};
// These cannot be reactivated in a future branch due to window expiration.
static
const infrastructure::config::checkpoint mainnet_bip9_bit1_active_checkpoint{
    "b50ce9202c152e481ca509156028af954654ed13e4b0656eb497554aa753db0b", 1201535};
static
const infrastructure::config::checkpoint testnet_bip9_bit1_active_checkpoint{
    "4966625a4b2851d9fdee139e56211a0d88575f59ed816ff5e6a63deb4e3e29a0", 0};
static
const infrastructure::config::checkpoint regtest_bip9_bit1_active_checkpoint{
    // The activation window is fixed and closed, so assume genesis activation.
    "06226e46111a0b59caaf126043eb5bbf28c34f3a5e332a1fc7b2b73cf188910f", 0};

constexpr uint64_t retarget_subsidy_interval = 840000;
constexpr uint64_t no_retarget_subsidy_interval = 150;

} // namespace kth

#endif // KTH_DOMAIN_CONSTANTS_LTC_HPP_
