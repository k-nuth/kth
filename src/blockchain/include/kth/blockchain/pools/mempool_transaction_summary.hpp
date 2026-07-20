// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_MEMPOOL_TRANSACTION_SUMMARY_HPP
#define KTH_MEMPOOL_TRANSACTION_SUMMARY_HPP

#include <cstdint>
#include <string>

namespace kth::blockchain {

/// This class is not thread safe.
class mempool_transaction_summary {
public:
    mempool_transaction_summary(std::string const& address, std::string const& hash, std::string const& previous_output_hash,
                                std::string const& previous_output_index, std::string const& satoshis, uint64_t index,
                                uint64_t timestamp);

    /// Associated wallet address
    std::string address() const;

    /// Unique identifier inside the blockchain
    std::string hash() const;

    /// Previous output hash
    std::string previous_output_hash() const;

    /// Previous output index
    std::string previous_output_index() const;

    /// Total value
    std::string satoshis() const;

    /// Transaction index
    uint64_t index() const;

    /// Transaction timestamp
    uint64_t timestamp() const;

private:
    std::string address_;
    std::string hash_;
    std::string previous_output_hash_;
    std::string previous_output_index_;
    std::string satoshis_;
    uint64_t index_;
    uint64_t timestamp_;
};

} // namespace kth::blockchain

#endif //KTH_BLOCKCHAIN_FORK_HPP
