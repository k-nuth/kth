// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_TRANSACTION_HPP
#define KTH_DOMAIN_CHAIN_TRANSACTION_HPP

#include <algorithm>
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
#include <kth/domain/chain/utxo.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/script_flags.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>

#include <kth/domain/concepts.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/payment_address.hpp>

namespace kth::domain::chain {

namespace detail {

// Write a length-prefixed collection of inputs or outputs to the sink.
template <class Sink, class Put>
void write(Sink& sink, std::vector<Put> const& puts, bool wire) {
    sink.write_variable_little_endian(puts.size());

    auto const serialize = [&](const Put& put) {
        put.to_data(sink, wire);
    };

    std::for_each(puts.begin(), puts.end(), serialize);
}

} // namespace detail

class transaction;

hash_digest hash(transaction const& tx);
hash_digest outputs_hash(transaction const& tx);
hash_digest inpoints_hash(transaction const& tx);
hash_digest sequences_hash(transaction const& tx);
hash_digest utxos_hash(transaction const& tx);

hash_digest to_outputs(transaction const& tx);
hash_digest to_inpoints(transaction const& tx);
hash_digest to_sequences(transaction const& tx);
hash_digest to_utxos(transaction const& tx);

uint64_t total_input_value(transaction const& tx);
uint64_t total_output_value(transaction const& tx);
uint64_t fees(transaction const& tx);
bool is_overspent(transaction const& tx);

struct KD_API transaction {
    using ins = input::list;
    using outs = output::list;
    using list = std::vector<transaction>;

    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    struct validation_t {
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

    transaction() = default;
    transaction(uint32_t version, uint32_t locktime, ins const& inputs, outs const& outputs);
    transaction(uint32_t version, uint32_t locktime, ins&& inputs, outs&& outputs);

    // Operators.
    //-----------------------------------------------------------------------------

    // NOTE: `==` is not `= default` because the `validation` member is
    // tracing metadata, not part of the value's identity. `!=` is defaulted
    // (delegates to `!(==)`) so callers that spell out the member call still
    // resolve.
    bool operator==(transaction const& x) const;
    bool operator!=(transaction const& x) const = default;

    // Deserialization.
    //-----------------------------------------------------------------------------

    static
    expect<transaction> from_data(byte_reader& reader, bool wire = true);

    [[nodiscard]]
    bool is_valid() const;

    // Serialization.
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data(bool wire = true) const;

    void to_data(data_sink& stream, bool wire = true) const;

    template <typename W>
        requires ( ! std::is_same_v<W, bool>)
    void to_data(W& sink, bool wire = true) const {
        if (wire) {
            sink.write_4_bytes_little_endian(version_);
            detail::write(sink, inputs_, wire);
            detail::write(sink, outputs_, wire);
            sink.write_4_bytes_little_endian(locktime_);
        } else {
            // Database (outputs forward) serialization.
            detail::write(sink, outputs_, wire);
            detail::write(sink, inputs_, wire);
            sink.write_variable_little_endian(locktime_);
            sink.write_variable_little_endian(version_);
        }
    }

    // Properties (size, accessors).
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    size_t serialized_size(bool wire = true) const;

    [[nodiscard]]
    uint32_t version() const;
    void set_version(uint32_t value);

    [[nodiscard]]
    uint32_t locktime() const;
    void set_locktime(uint32_t value);

    // [[deprecated]] // unsafe
    ins& inputs();

    [[nodiscard]]
    ins const& inputs() const;

    void set_inputs(ins const& value);
    void set_inputs(ins&& value);

    // [[deprecated]] // unsafe
    outs& outputs();

    [[nodiscard]]
    outs const& outputs() const;

    void set_outputs(outs const& value);
    void set_outputs(outs&& value);

    // Hashes — recomputed on every call (no cache).
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    hash_digest hash() const;

    [[nodiscard]]
    hash_digest outputs_hash() const;

    [[nodiscard]]
    hash_digest inpoints_hash() const;

    [[nodiscard]]
    hash_digest sequences_hash() const;

    [[nodiscard]]
    hash_digest utxos_hash() const;

    // Validation.
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    uint64_t fees() const;

    [[nodiscard]]
    point::list previous_outputs() const;

    [[nodiscard]]
    point::list missing_previous_outputs() const;

    [[nodiscard]]
    hash_list missing_previous_transactions() const;

    [[nodiscard]]
    uint64_t total_input_value() const;

    [[nodiscard]]
    uint64_t total_output_value() const;

    [[nodiscard]]
    size_t signature_operations() const;

    [[nodiscard]]
    size_t signature_operations(bool bip16, bool bip141) const;

    [[nodiscard]]
    bool is_coinbase() const;

    [[nodiscard]]
    bool is_null_non_coinbase() const;

    [[nodiscard]]
    bool is_oversized_coinbase() const;

    [[nodiscard]]
    bool is_mature(size_t height) const;

    [[nodiscard]]
    bool is_internal_double_spend() const;

    [[nodiscard]]
    bool is_double_spend(bool include_unconfirmed) const;

    [[nodiscard]]
    bool is_dusty(uint64_t minimum_output_value) const;

    [[nodiscard]]
    bool is_missing_previous_outputs() const;

    [[nodiscard]]
    bool is_final(size_t block_height, uint32_t block_time) const;

    [[nodiscard]]
    bool is_locked(size_t block_height, uint32_t median_time_past) const;

    [[nodiscard]]
    bool is_locktime_conflict() const;

    [[nodiscard]]
    bool is_overspent() const;

    /// Structural validation — no context needed, no prevout cache needed.
    /// Can be called immediately after deserialization for fast rejection.
    [[nodiscard]]
    code check(size_t max_block_size, bool transaction_pool, bool retarget = true) const;

    [[nodiscard]]
    size_t min_tx_size(script_flags_t flags) const;

    /// Contextual validation — requires flags, height, etc. Prevout cache must be populated.
    /// Call after check() passes and prevout cache is filled.
    [[nodiscard]]
    code accept(
        script_flags_t flags,
        size_t height,
        uint32_t median_time_past,
        size_t max_sigops,
        bool is_under_checkpoint,
        bool transaction_pool
    ) const;

    [[nodiscard]]
    code connect() const;

    [[nodiscard]]
    code connect(chain_state const& state) const;

    [[nodiscard]]
    code connect_input(chain_state const& state, size_t input_index) const;

    [[nodiscard]]
    bool is_standard() const;

    /// Flag-aware counterpart of `is_standard()`. Outputs are classified
    /// through `script::output_pattern(flags)` (so `bch_p2s` can promote a
    /// sub-201-byte non-matching scriptPubKey to `pay_to_script`, BCH
    /// 2026-May leibniz); inputs keep the classical `input_pattern()`
    /// classification — the P2S catch-all is intentionally asymmetric and
    /// applies to locking bytecode only, matching BCHN's `Solver()` being
    /// passed flags only on the scriptPubKey side.
    [[nodiscard]]
    bool is_standard(script_flags_t flags) const;

    void reset();

    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    mutable validation_t validation{};

private:
    [[nodiscard]]
    bool all_inputs_final() const;

#if defined(KTH_CURRENCY_BCH)
    [[nodiscard]]
    code validate_tokens(script_flags_t flags) const;
#endif

    uint32_t version_{0};
    uint32_t locktime_{0};
    input::list inputs_;
    output::list outputs_;
};

code verify(transaction const& tx, uint32_t input_index, script_flags_t flags, script const& input_script, script const& prevout_script, uint64_t /*value*/);
code verify(transaction const& tx, uint32_t input, script_flags_t flags);

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_TRANSACTION_HPP
