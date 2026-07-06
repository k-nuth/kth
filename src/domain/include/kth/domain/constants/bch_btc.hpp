// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONSTANTS_BCH_BTC_HPP_
#define KTH_DOMAIN_CONSTANTS_BCH_BTC_HPP_

#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/config/checkpoint.hpp>

namespace kth {

constexpr size_t max_work_bits = 0x1d00ffff;
constexpr uint32_t retarget_proof_of_work_limit = 0x1d00ffff;
constexpr uint32_t no_retarget_proof_of_work_limit = 0x207fffff;
// This may not be flexible, keep internal.
//0x00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff

constexpr uint32_t target_spacing_seconds = 10 * 60;
constexpr uint32_t target_timespan_seconds = 2 * 7 * 24 * 60 * 60;

// Mainnet activation parameters (bip34-style activations).
constexpr size_t mainnet_active = 750;
constexpr size_t mainnet_enforce = 950;
constexpr size_t mainnet_sample = 1000;

// Testnet activation parameters (bip34-style activations).
constexpr size_t testnet_active = 51;
constexpr size_t testnet_enforce = 75;
constexpr size_t testnet_sample = 100;

// Mainnet frozen activation heights (frozen_activations).
constexpr size_t mainnet_bip65_freeze = 388381;
constexpr size_t mainnet_bip66_freeze = 363725;
constexpr size_t mainnet_bip34_freeze = 227931;

// Testnet frozen activation heights (frozen_activations).
constexpr size_t testnet_bip65_freeze = 581885;
constexpr size_t testnet_bip66_freeze = 330776;
constexpr size_t testnet_bip34_freeze = 21111;

// Regtest (arbitrary) frozen activation heights (frozen_activations).
constexpr size_t regtest_bip65_freeze = 1351;
constexpr size_t regtest_bip66_freeze = 1251;
constexpr size_t regtest_bip34_freeze = 0;

// Block 514 is the first testnet block after date-based activation.
// Block 166832 is the first mainnet block after date-based activation.
constexpr uint32_t bip16_activation_time = 0x4f3af580;

// Block 170060 was mined with an invalid p2sh (code shipped late).
// bitcointalk.org/index.php?topic=63165.msg788832#msg788832
static
const infrastructure::config::checkpoint mainnet_bip16_exception_checkpoint{
    "00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22"_hash, 170060};

// github.com/bitcoin/bips/blob/master/bip-0030.mediawiki#specification
static
const infrastructure::config::checkpoint mainnet_bip30_exception_checkpoint1{
    "00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec"_hash, 91842};

static
const infrastructure::config::checkpoint mainnet_bip30_exception_checkpoint2{
    "00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721"_hash, 91880};

// bip90 stops checking unspent duplicates above this bip34 activation.
static
const infrastructure::config::checkpoint mainnet_bip34_active_checkpoint{
    "000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8"_hash, 227931};
static
const infrastructure::config::checkpoint testnet_bip34_active_checkpoint{
    "0000000023b3a96d3484e5abb3755c413e7d41500f8e2a5c3f0dd01299cd8ef8"_hash, 21111};
static
const infrastructure::config::checkpoint regtest_bip34_active_checkpoint{
    // Since bip90 assumes a historical bip34 activation block, use genesis.
    "06226e46111a0b59caaf126043eb5bbf28c34f3a5e332a1fc7b2b73cf188910f"_hash, 0};


constexpr uint64_t retarget_subsidy_interval = 210000;
constexpr uint64_t no_retarget_subsidy_interval = 150;

} // namespace kth

#endif // KTH_DOMAIN_CONSTANTS_BCH_BTC_HPP_
