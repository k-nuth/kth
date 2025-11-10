// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_TRANSACTION_HPP
#define KTH_DOMAIN_CHAIN_TRANSACTION_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/point.hpp>
#include <kth/domain/chain/transaction_basis.hpp>
#include <kth/domain/chain/utxo.hpp>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/rule_fork.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

#include <expected>

#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/chain/coin_selection.hpp>

namespace kth::domain::chain {

using template_result = std::tuple<transaction, std::vector<uint32_t>, std::vector<wallet::payment_address>, std::vector<uint64_t>>;

struct KD_API transaction : transaction_basis {
public:
    using ins = input::list;
    using outs = output::list;
    using list = std::vector<transaction>;
    using hash_ptr = std::shared_ptr<hash_digest>;

    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    struct validation {
        uint64_t originator = 0;
        code error = error::not_found;
        chain_state::ptr state = nullptr;

        // The transaction is an unspent duplicate.
        bool duplicate = false;

        // The unconfirmed tx exists in the store.
        bool pooled = false;

        // The unconfirmed tx is validated at the block's current fork state.
        bool current = false;

        // Similate organization and instead just validate the transaction.
        bool simulate = false;

        // The transaction was validated before its insertion in the mempool.
        bool validated = false;
    };

    // Constructors.
    //-----------------------------------------------------------------------------

    transaction();
    transaction(uint32_t version, uint32_t locktime, ins const& inputs, outs const& outputs);
    transaction(uint32_t version, uint32_t locktime, ins&& inputs, outs&& outputs);
    transaction(transaction const& x, hash_digest const& hash);
    transaction(transaction&& x, hash_digest const& hash);

    explicit
    transaction(transaction_basis const& x);
    explicit
    transaction(transaction_basis&& x) noexcept;


    // Special member functions.
    //-----------------------------------------------------------------------------


    //Note(kth): cannot be defaulted because of the mutex data member.
    transaction(transaction const& x);
    transaction(transaction&& x) noexcept;
    transaction& operator=(transaction const& x);
    transaction& operator=(transaction&& x) noexcept;

    // Deserialization.
    //-----------------------------------------------------------------------------

    static
    expect<transaction> from_data(byte_reader& reader, bool wire);

    // Serialization.
    //-----------------------------------------------------------------------------

    data_chunk to_data(bool wire = true) const;

    void to_data(data_sink& stream, bool wire = true) const;

    template <typename W>
        requires ( ! std::is_same_v<W, bool>)
    void to_data(W& sink, bool wire = true) const {
        transaction_basis::to_data(sink, wire);
    }

    // Properties (size, accessors, cache).
    //-----------------------------------------------------------------------------

    size_t serialized_size(bool wire = true) const;

    void set_version(uint32_t value);
    void set_locktime(uint32_t value);
    void set_inputs(const ins& value);
    void set_inputs(ins&& value);
    void set_outputs(const outs& value);
    void set_outputs(outs&& value);

    hash_digest outputs_hash() const;
    hash_digest inpoints_hash() const;
    hash_digest sequences_hash() const;
    hash_digest utxos_hash() const;

    hash_digest hash() const;

    // Utilities.
    //-------------------------------------------------------------------------

    void recompute_hash();

    // Validation.
    //-----------------------------------------------------------------------------

    using transaction_basis::signature_operations;

    uint64_t fees() const;
    // point::list previous_outputs() const;
    point::list missing_previous_outputs() const;
    // hash_list missing_previous_transactions() const;
    uint64_t total_input_value() const;
    uint64_t total_output_value() const;
    size_t signature_operations() const;

    bool is_overspent() const;

    using transaction_basis::accept;

    code check(size_t max_block_size, bool transaction_pool, bool retarget = true) const;
    code accept(bool transaction_pool = true) const;
    code accept(chain_state const& state, bool transaction_pool = true) const;
    code connect() const;
    code connect(chain_state const& state) const;
    code connect_input(chain_state const& state, size_t input_index) const;


    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    mutable validation validation{};

    bool is_standard() const;

    static
    std::expected<template_result, std::error_code> create_template(
        std::vector<utxo> available_utxos,
        uint64_t amount_to_send_hint,
        wallet::payment_address const& destination_address,
        std::vector<wallet::payment_address> const& change_addresses,
        coin_selection_algorithm selection_algo
    );

    static
    std::expected<template_result, std::error_code> create_template(
        std::vector<utxo> available_utxos,
        uint64_t amount_to_send_hint,
        wallet::payment_address const& destination_address,
        std::vector<wallet::payment_address> const& change_addresses,
        std::vector<double> const& change_ratios,
        coin_selection_algorithm selection_algo
    );

// protected:
    void reset();

protected:
    void invalidate_cache() const;
    bool all_inputs_final() const;

private:

    // TODO(kth): (refactor to transaction_result)
    // this 3 variables should be stored in transaction_unconfired database when the store
    // function is called. This values will be in the transaction_result object before
    // creating the transaction object

    // These share a mutex as they are not expected to contend.
    mutable hash_ptr hash_;
    mutable hash_ptr outputs_hash_;
    mutable hash_ptr inpoints_hash_;
    mutable hash_ptr sequences_hash_;
    mutable hash_ptr utxos_hash_;

#if ! defined(__EMSCRIPTEN__)
    mutable upgrade_mutex hash_mutex_;
#else
    mutable shared_mutex hash_mutex_;
#endif

    // These share a mutex as they are not expected to contend.
    mutable std::optional<uint64_t> total_input_value_;
    mutable std::optional<uint64_t> total_output_value_;
    mutable std::optional<bool> segregated_;

#if ! defined(__EMSCRIPTEN__)
    mutable upgrade_mutex mutex_;
#else
    mutable shared_mutex mutex_;
#endif
};


code verify(transaction const& tx, uint32_t input_index, uint32_t forks, script const& input_script, script const& prevout_script, uint64_t /*value*/);
code verify(transaction const& tx, uint32_t input, uint32_t forks);

} // namespace kth::domain::chain

#endif
