// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_BLOCK_HPP
#define KTH_DOMAIN_CHAIN_BLOCK_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <kth/domain/chain/block_basis.hpp>
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
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>

namespace kth::domain::chain {

struct KD_API block : block_basis {
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

    explicit
    block(block_basis const& basis)
        : block_basis(basis)
    {}

    explicit
    block(block_basis&& basis)
        : block_basis(std::move(basis))
    {}

    //Note(kth): cannot be defaulted because of the mutex data member.
    block(block const& x);
    block(block&& x) noexcept;
    /// This class is move assignable and copy assignable.
    block& operator=(block const& x);
    block& operator=(block&& x) noexcept;

    // Deserialization.
    //-------------------------------------------------------------------------
    static
    expect<block> from_data(byte_reader& reader, bool wire = true);

    // Serialization.
    //-------------------------------------------------------------------------

    using block_basis::to_data;
    data_chunk to_data() const;

    // Properties (size, accessors, cache).
    //-------------------------------------------------------------------------

    size_t serialized_size() const;

    // void set_header(chain::header const& value);
    void set_transactions(transaction::list const& value);
    void set_transactions(transaction::list&& value);

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

    size_t signature_operations() const;

    using block_basis::signature_operations;

    size_t total_inputs(bool with_coinbase = true) const;

    code check() const;
    code accept(bool transactions = true) const;
    code accept(chain_state const& state, bool transactions = true) const;
    code connect() const;

    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    mutable validation_t validation{};

private:
    mutable std::optional<size_t> total_inputs_;
    mutable std::optional<size_t> base_size_;   // total size

#if ! defined(__EMSCRIPTEN__)
    mutable upgrade_mutex mutex_;
#else
    mutable shared_mutex mutex_;
#endif
};

} // namespace kth::domain::chain

#endif
