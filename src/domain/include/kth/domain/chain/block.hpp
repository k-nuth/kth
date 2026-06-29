// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_BLOCK_HPP
#define KTH_DOMAIN_CHAIN_BLOCK_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <vector>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/transaction.hpp>


#include <kth/domain/concepts.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>

namespace kth::domain::chain {

using indexes = std::vector<size_t>;

struct KD_API block {
public:
    using list = std::vector<block>;
    using indexes = std::vector<size_t>;

    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    struct validation_t {
        uint64_t originator = 0;
        code error = error::not_found;
        chain_state::ptr state = nullptr;

        // Similate organization and instead just validate the block.
        bool simulate = false;

        asio::time_point start_deserialize;
        asio::time_point end_deserialize;
        asio::time_point start_check;
        asio::time_point start_populate;
        asio::time_point start_accept;
        asio::time_point start_connect;
        asio::time_point start_notify;
        asio::time_point start_pop;
        asio::time_point start_push;
        asio::time_point end_push;
        float cache_efficiency;
    };

    // Constructors.
    //-------------------------------------------------------------------------

    block() = default;
    block(chain::header const& header, transaction::list&& transactions);
    block(chain::header const& header, transaction::list const& transactions);

    // Operators.
    //-------------------------------------------------------------------------

    bool operator==(block const& x) const;
    bool operator!=(block const& x) const;

    // Deserialization.
    //-------------------------------------------------------------------------

    static
    expect<block> from_data(byte_reader& reader);

    [[nodiscard]]
    bool is_valid() const;

    // Serialization.
    //-------------------------------------------------------------------------

    data_chunk to_data() const;
    void to_data(data_sink& stream) const;

    template <typename W>
    void to_data(W& sink) const {
        header_.to_data(sink, true);
        sink.write_size_little_endian(transactions_.size());
        auto const to = [&sink](transaction const& tx) {
            tx.to_data(sink, true);
        };
        std::for_each(transactions_.begin(), transactions_.end(), to);
    }

    [[nodiscard]]
    hash_list to_hashes() const;

    // Properties (size, accessors).
    //-------------------------------------------------------------------------

    size_t serialized_size() const;

    chain::header& header();

    [[nodiscard]]
    chain::header const& header() const;

    void set_header(chain::header const& value);

    transaction::list& transactions();

    [[nodiscard]]
    transaction::list const& transactions() const;

    void set_transactions(transaction::list const& value);
    void set_transactions(transaction::list&& value);

    [[nodiscard]]
    hash_digest hash() const;

    // Utilities.
    //-------------------------------------------------------------------------

    static
    block genesis_mainnet();

    static
    block genesis_testnet();

    static
    block genesis_regtest();

#if defined(KTH_CURRENCY_BCH)
    static
    block genesis_testnet4();

    static
    block genesis_scalenet();

    static
    block genesis_chipnet();
#endif

    static
    size_t locator_size(size_t top);

    static
    indexes locator_heights(size_t top);

    // Validation.
    //-------------------------------------------------------------------------

    static
    uint64_t subsidy(size_t height, bool retarget);

    static
    uint256_t proof(uint32_t bits);

    [[nodiscard]]
    uint64_t fees() const;

    [[nodiscard]]
    uint64_t claim() const;

    [[nodiscard]]
    uint64_t reward(size_t height) const;

    [[nodiscard]]
    uint256_t proof() const;

    [[nodiscard]]
    hash_digest generate_merkle_root() const;

    [[nodiscard]]
    size_t signature_operations() const;

    [[nodiscard]]
    size_t signature_operations(bool bip16, bool bip141) const;

    [[nodiscard]]
    size_t total_inputs(bool with_coinbase) const;

    [[nodiscard]]
    bool is_extra_coinbases() const;

    [[nodiscard]]
    bool is_final(size_t height, uint32_t block_time) const;

    [[nodiscard]]
    bool is_distinct_transaction_set() const;

    [[nodiscard]]
    bool is_valid_coinbase_claim(size_t height) const;

    [[nodiscard]]
    bool is_valid_coinbase_script(size_t height) const;

    [[nodiscard]]
    bool is_forward_reference() const;

    [[nodiscard]]
    bool is_canonical_ordered() const;

    [[nodiscard]]
    bool is_internal_double_spend() const;

    [[nodiscard]]
    bool is_valid_merkle_root() const;

    code check() const;

    /// Check block body only (skip header validation for headers-first sync).
    /// Use this when headers have already been validated during header sync.
    code check_body() const;

    [[nodiscard]]
    code check_transactions() const;

    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    mutable validation_t validation{};

// protected:
    void reset();

    [[nodiscard]]
    size_t non_coinbase_input_count() const;

private:
    chain::header header_;
    transaction::list transactions_;
};

// Non-member functions.
//-------------------------------------------------------------------------

size_t locator_size(size_t top);

indexes locator_heights(size_t top);

size_t total_inputs(block const& blk, bool with_coinbase);

size_t serialized_size(block const& blk);

} // namespace kth::domain::chain

#endif
