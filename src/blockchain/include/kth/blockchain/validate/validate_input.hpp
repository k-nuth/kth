// Copyright (c) 2016-present Knuth Project developers.
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
    uint32_t convert_flags(domain::script_flags_t native_forks);

    static
    code convert_result(consensus::verify_result_type result);
#endif

    static
    std::pair<code, size_t> verify_script(domain::chain::transaction const& tx, uint32_t input_index, domain::script_flags_t flags);

    // Verify every input of `tx`, building the signature-checker context (the
    // serialized transaction and the spent-output coins) ONCE and reusing it
    // across inputs — that context depends only on the transaction, not on the
    // input, so the per-input verify_script rebuilds it redundantly (O(inputs^2)).
    // Returns the first input error (or success) and the transaction's total
    // SigChecks. Every input's prevout cache must be populated by the caller.
    static
    std::pair<code, size_t> verify_transaction(domain::chain::transaction const& tx, domain::script_flags_t flags);
};

} // namespace kth::blockchain

#endif
