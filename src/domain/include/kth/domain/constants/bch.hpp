// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONSTANTS_BCH_HPP_
#define KTH_DOMAIN_CONSTANTS_BCH_HPP_

#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/config/checkpoint.hpp>

#include <kth/domain/config/network.hpp>
#include <kth/domain/constants/bch_btc.hpp>
#include <kth/domain/constants/common.hpp>

namespace kth {

// Blocks used to calculate the next difficulty
constexpr size_t bch_daa_cw_144_retarget_algorithm = 147;
constexpr size_t chain_state_timestamp_count = bch_daa_cw_144_retarget_algorithm;
constexpr size_t bch_daa_eda_blocks = 6 + 11;

namespace max_block_size {
constexpr size_t mainnet_old = 8'000'000;  // 8 million bytes
constexpr size_t mainnet_new = 32'000'000; // 32 million bytes
constexpr size_t testnet3 = 32'000'000;    // 32 million bytes
constexpr size_t regtest = 32'000'000;     // 32 million bytes
constexpr size_t testnet4 = 2'000'000;     // 2 million bytes
constexpr size_t scalenet = 256'000'000;   // 256 million bytes
constexpr size_t chipnet = 2'000'000;      // 2 million bytes
} // namespace block_size

namespace max_block_sigops {
constexpr size_t mainnet_old = max_block_size::mainnet_old / max_sigops_factor;
constexpr size_t mainnet_new = max_block_size::mainnet_new / max_sigops_factor;
constexpr size_t testnet4 = max_block_size::testnet4 / max_sigops_factor;
constexpr size_t scalenet = max_block_size::scalenet / max_sigops_factor;
constexpr size_t chipnet = max_block_size::chipnet / max_sigops_factor;
} // namespace max_block_sigops

constexpr size_t min_transaction_size_euclid = 100;     // in bytes, from 2018-Nov-15
constexpr size_t min_transaction_size_descartes = 65;   // in bytes, from 2023-May-15

constexpr uint32_t transaction_version_min = 1;
constexpr uint32_t transaction_version_max = 2;

// Maximum commitment lengths for NFT tokens (BCHN: token::MAX_CONSENSUS_COMMITMENT_LENGTH_*).
constexpr size_t max_token_commitment_length_descartes = 40;   // 2023-May
constexpr size_t max_token_commitment_length_leibniz = 128;    // 2026-May


constexpr size_t max_tx_sigchecks = 3000;
constexpr size_t block_maxbytes_maxsigchecks_ratio = 141;

constexpr size_t max_payload_size_scalenet = max_block_size::scalenet;

// Testnet4 frozen activation heights (frozen_activations).
constexpr size_t testnet4_bip65_freeze = 3;
constexpr size_t testnet4_bip66_freeze = 4;
constexpr size_t testnet4_bip34_freeze = 2;

// Scalenet frozen activation heights (frozen_activations).
constexpr size_t scalenet_bip65_freeze = 3;
constexpr size_t scalenet_bip66_freeze = 4;
constexpr size_t scalenet_bip34_freeze = 2;

// Chipnet frozen activation heights (frozen_activations).
constexpr size_t chipnet_bip65_freeze = 3;
constexpr size_t chipnet_bip66_freeze = 4;
constexpr size_t chipnet_bip34_freeze = 2;

static
const infrastructure::config::checkpoint testnet4_bip34_active_checkpoint {
    "00000000b0c65b1e03baace7d5c093db0d6aac224df01484985ffd5e86a1a20c", 2};

static
const infrastructure::config::checkpoint scalenet_bip34_active_checkpoint {
    "00000000c8c35eaac40e0089a83bf5c5d9ecf831601f98c21ed4a7cb511a07d8", 2};

static
const infrastructure::config::checkpoint chipnet_bip34_active_checkpoint {
    "00000000b0c65b1e03baace7d5c093db0d6aac224df01484985ffd5e86a1a20c", 2};

// enum class pythagoras_t : uint64_t {};
// enum class euclid_t : uint64_t {};
// enum class pisano_t : uint64_t {};
// enum class mersenne_t : uint64_t {};
// enum class fermat_t : uint64_t {};
// enum class euler_t : uint64_t {};
// enum class gauss_t : uint64_t {};
// enum class descartes_t : uint64_t {};
// enum class lobachevski_t : uint64_t {};
// enum class galois_t : uint64_t {};
enum class leibniz_t : uint64_t {};
enum class cantor_t : uint64_t {};
enum class unnamed_t : uint64_t {}; //TODO(fernando): rename it

// constexpr size_t         bch_activation_height = 478559 //478558;     // 2017-Aug-01 HF
// constexpr uint32_t       bch_daa_cw144_activation_time = 1510600000;  // 2017-Nov-13 HF
// constexpr uint32_t       bch_pythagoras_activation_time = 1526400000; // 2018-May-15 HF
// constexpr euclid_t       bch_euclid_activation_time = 1542300000;     // 2018-Nov-15 HF
// constexpr pisano_t       bch_pisano_activation_time = 1557921600;     // 2019-May-15 HF
// constexpr mersenne_t     bch_mersenne_activation_time = 1573819200;   // 2019-Nov-15 HF
// constexpr fermat_t       bch_fermat_activation_time{1589544000};      // 2020-May-15 HF
// constexpr euler_t        bch_euler_activation_time{1605441600};       // 2020-Nov-15 HF
                                                                         // 2021-May-15 (skipped, not a HF)
// constexpr gauss_t        bch_gauss_activation_time{1652616000};       // 2022-May-15 HF
// constexpr descartes_t    bch_descartes_activation_time{1684152000};   // 2023-May-15 HF
// constexpr lobachevski_t  bch_lobachevski_activation_time{1715774400}; // 2024-May-15 HF
// constexpr galois_t       bch_galois_activation_time{1747310400};      // 2025-May-15 HF
constexpr leibniz_t      bch_leibniz_activation_time{1778846400};        // 2026-May-15 HF
constexpr cantor_t       bch_cantor_activation_time{1810382400};         // 2027-May-15 HF


// Block height at which CSV (BIP68, BIP112 and BIP113) becomes active
constexpr size_t mainnet_csv_activation_height = 419329;
constexpr size_t testnet_csv_activation_height = 770113;
constexpr size_t testnet4_csv_activation_height = 6;
constexpr size_t scalenet_csv_activation_height = 6;
constexpr size_t chipnet_csv_activation_height = 6;

//2017-August-01 hard fork
constexpr size_t mainnet_uahf_activation_height = 478559;
constexpr size_t testnet_uahf_activation_height = 1155876;
constexpr size_t testnet4_uahf_activation_height = 6;
constexpr size_t scalenet_uahf_activation_height = 6;
constexpr size_t chipnet_uahf_activation_height = 6;

//2017-November-13 hard fork
constexpr size_t mainnet_daa_cw144_activation_height = 504032;
constexpr size_t testnet_daa_cw144_activation_height = 1188698;
constexpr size_t testnet4_daa_cw144_activation_height = 3001;
constexpr size_t scalenet_daa_cw144_activation_height = 3001;
constexpr size_t chipnet_daa_cw144_activation_height = 3001;

//2018-May hard fork
constexpr size_t mainnet_pythagoras_activation_height = 530356;  // Bitcoin Cash Node checkpoint: 530359, due to a historical inaccuracy in the Bitcoin ABC code: https://github.com/bitcoin-cash-node/bitcoin-cash-node/commit/97c32f461a1a6d6ca71c5958d67047a1c06d83fd#diff-ff53e63501a5e89fd650b378c9708274df8ad5d38fcffa6c64be417c4d438b6d
constexpr size_t testnet_pythagoras_activation_height = 1233070; // Bitcoin Cash Node checkpoint: 1233078
constexpr size_t testnet4_pythagoras_activation_height = 0;      // TODO(fernando): testnet4
constexpr size_t scalenet_pythagoras_activation_height = 0;      // TODO(fernando): scalenet
constexpr size_t chipnet_pythagoras_activation_height = 0;       // TODO(fernando): chipnet

//2018-November hard fork
constexpr size_t mainnet_euclid_activation_height = 556767;
constexpr size_t testnet_euclid_activation_height = 1267997;
constexpr size_t testnet4_euclid_activation_height = 4001;
constexpr size_t scalenet_euclid_activation_height = 4001;
constexpr size_t chipnet_euclid_activation_height = 4001;

//2019-May hard fork
constexpr size_t mainnet_pisano_activation_height = 582680;
constexpr size_t testnet_pisano_activation_height = 1303885;
constexpr size_t testnet4_pisano_activation_height = 0; //TODO(fernando): testnet4
constexpr size_t scalenet_pisano_activation_height = 0; //TODO(fernando): scalenet
constexpr size_t chipnet_pisano_activation_height = 0;  //TODO(fernando): chipnet

//2019-Nov hard fork
constexpr size_t mainnet_mersenne_activation_height = 609136;
constexpr size_t testnet_mersenne_activation_height = 1341712;
constexpr size_t testnet4_mersenne_activation_height = 5001;
constexpr size_t scalenet_mersenne_activation_height = 5001;
constexpr size_t chipnet_mersenne_activation_height = 5001;

//2020-May hard fork
constexpr size_t mainnet_fermat_activation_height = 635259;
constexpr size_t testnet_fermat_activation_height = 1378461;
constexpr size_t testnet4_fermat_activation_height = 0;         //Note: https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node/-/blame/master/src/chainparams.cpp#L594
constexpr size_t scalenet_fermat_activation_height = 0;         //Note: https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node/-/blame/master/src/chainparams.cpp#L594
constexpr size_t chipnet_fermat_activation_height = 0;          //Note: https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node/-/blame/master/src/chainparams.cpp#L594

// //2020-Nov hard fork, ASERT Anchor block lock up
// //Will be removed once Euler(2020-Nov) update is activated
// constexpr size_t mainnet_asert_anchor_lock_up_height = 652500;  // 000000000000000001655f282a3684de3e422290dca55a7ff84753871073c37e
// constexpr size_t testnet_asert_anchor_lock_up_height = 1408990; // 0000000000069a8d053a2f34739137cd86722bde2516f03759d9349a0c04fd2e
// constexpr size_t testnet4_asert_anchor_lock_up_height = 0;      // Genesis: 000000001dd410c49a788668ce26751718cc797474d3152a5fc073dd44fd9f7b
// constexpr size_t scalenet_asert_anchor_lock_up_height = 0;      // Genesis: 00000000e6453dc2dfe1ffa19023f86002eb11dbb8e87d0291a4599f0430be52
// constexpr size_t chipnet_asert_anchor_lock_up_height = 0;       // Genesis: 00000000e6453dc2dfe1ffa19023f86002eb11dbb8e87d0291a4599f0430be52

//2020-Nov hard fork, ASERT Anchor/Reference block
constexpr size_t mainnet_asert_anchor_block_height = 661647;        // 00000000000000000083ed4b7a780d59e3983513215518ad75654bb02deee62f
constexpr uint32_t mainnet_asert_anchor_block_bits = 0x1804dafe;
constexpr size_t mainnet_asert_anchor_block_ancestor_time = 1605447844;

constexpr size_t testnet_asert_anchor_block_height = 1421481;       // 00000000062c7f32591d883c99fc89ebe74a83287c0f2b7ffeef72e62217d40b
constexpr uint32_t testnet_asert_anchor_block_bits = 0x1d00ffff;
constexpr size_t testnet_asert_anchor_block_ancestor_time = 1605445400;

constexpr size_t testnet4_asert_anchor_block_height = 16844;        // 00000000602570ee2b66c1d3f75d404c234f8aacdcc784da97e65838a2daf0fc
constexpr uint32_t testnet4_asert_anchor_block_bits = 0x1d00ffff;
constexpr size_t testnet4_asert_anchor_block_ancestor_time = 1605451779;

constexpr size_t scalenet_asert_anchor_block_height = 16868;        // 000000008b6a607a3a731ae1df816bb828450bec67fea5e8dbcf837ed711b99a
constexpr uint32_t scalenet_asert_anchor_block_bits = 0x1d00ffff;
constexpr size_t scalenet_asert_anchor_block_ancestor_time = 1605448590;

constexpr size_t chipnet_asert_anchor_block_height = 16844;        // 00000000602570ee2b66c1d3f75d404c234f8aacdcc784da97e65838a2daf0fc
constexpr uint32_t chipnet_asert_anchor_block_bits = 0x1d00ffff;
constexpr size_t chipnet_asert_anchor_block_ancestor_time = 1605451779;

//2020-Nov hard fork
constexpr size_t mainnet_euler_activation_height = 661648;
constexpr size_t testnet_euler_activation_height = 1421482;
constexpr size_t testnet4_euler_activation_height = 16845;
constexpr size_t scalenet_euler_activation_height = 16869;
constexpr size_t chipnet_euler_activation_height = 16845;

//2021-May hard fork - There was no hard fork in May 2021

//2022-May hard fork
constexpr size_t mainnet_gauss_activation_height = 740'238;
constexpr size_t testnet_gauss_activation_height = 1'500'206;
constexpr size_t testnet4_gauss_activation_height = 95'465;
constexpr size_t scalenet_gauss_activation_height = 10'007;
constexpr size_t chipnet_gauss_activation_height = 95'465;

//2023-May hard fork
constexpr size_t mainnet_descartes_activation_height = 792'773;
constexpr size_t testnet_descartes_activation_height = 1'552'788;
constexpr size_t testnet4_descartes_activation_height = 148'044;
constexpr size_t scalenet_descartes_activation_height = 10'007;
constexpr size_t chipnet_descartes_activation_height = 121'957;

//2024-May hard fork
constexpr size_t mainnet_lobachevski_activation_height = 845'891;
constexpr size_t testnet_lobachevski_activation_height = 1'605'521;
constexpr size_t testnet4_lobachevski_activation_height = 200'741;
constexpr size_t scalenet_lobachevski_activation_height = 10'007;
constexpr size_t chipnet_lobachevski_activation_height = 174'520;

//2025-May hard fork
constexpr size_t mainnet_galois_activation_height = 898'374;
constexpr size_t testnet_galois_activation_height = 1'658'050;
constexpr size_t testnet4_galois_activation_height = 253'319;
constexpr size_t scalenet_galois_activation_height = 10'007;
constexpr size_t chipnet_galois_activation_height = 227'229;

// //2026-May hard fork
// constexpr size_t mainnet_leibniz_activation_height = ???;
// constexpr size_t testnet_leibniz_activation_height = ???;
// constexpr size_t testnet4_leibniz_activation_height = ???;
// constexpr size_t scalenet_leibniz_activation_height = ???;
// constexpr size_t chipnet_leibniz_activation_height = ???;

// //2027-May hard fork
// constexpr size_t mainnet_cantor_activation_height = ???;
// constexpr size_t testnet_cantor_activation_height = ???;
// constexpr size_t testnet4_cantor_activation_height = ???;
// constexpr size_t scalenet_cantor_activation_height = ???;
// constexpr size_t chipnet_cantor_activation_height = ???;

} // namespace kth

#endif // KTH_DOMAIN_CONSTANTS_BCH_HPP_
