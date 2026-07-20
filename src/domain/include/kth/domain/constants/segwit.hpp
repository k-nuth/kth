// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONSTANTS_SEGWIT_HPP_
#define KTH_DOMAIN_CONSTANTS_SEGWIT_HPP_

#include <cstddef>
#include <cstdint>

#include <kth/domain/config/network.hpp>

namespace kth {

constexpr uint8_t witness_marker = 0x00;
constexpr uint8_t witness_flag = 0x01;
constexpr uint32_t witness_head = 0xaa21a9ed;
constexpr size_t min_witness_program = 2;
constexpr size_t max_witness_program = 40;

constexpr size_t fast_sigops_factor = 4;
constexpr size_t max_fast_sigops = fast_sigops_factor * get_max_block_sigops(domain::config::network::mainnet);
constexpr size_t light_weight_factor = 4;
constexpr size_t max_block_weight = light_weight_factor * get_max_block_size(domain::config::network::mainnet);
constexpr size_t base_size_contribution = 3;
constexpr size_t total_size_contribution = 1;

} // namespace kth

#endif // KTH_DOMAIN_CONSTANTS_SEGWIT_HPP_
