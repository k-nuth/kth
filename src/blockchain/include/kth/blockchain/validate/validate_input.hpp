// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_INPUT_HPP
#define KTH_BLOCKCHAIN_VALIDATE_INPUT_HPP

#include <cstdint>

#include <kth/blockchain/define.hpp>
#include <kth/domain.hpp>

#ifdef WITH_CONSENSUS
#include <kth/consensus.hpp>
#endif

namespace kth::blockchain {

/// This class is static.
struct KB_API validate_input {

#ifdef WITH_CONSENSUS
    static
    uint32_t convert_flags(uint32_t native_forks);

    static
    code convert_result(consensus::verify_result_type result);
#endif

    static
    std::pair<code, size_t> verify_script(domain::chain::transaction const& tx, uint32_t input_index, uint32_t forks);
};

} // namespace kth::blockchain

#endif
