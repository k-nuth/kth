// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_TRANSACTION_BASIS_HPP
#define KTH_DOMAIN_CHAIN_TRANSACTION_BASIS_HPP

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
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/rule_fork.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>
#include <kth/domain/deserialization.hpp>

namespace kth::domain::chain {

namespace detail {

// // Read a length-prefixed collection of inputs or outputs from the source.
// template <class Source, class Put>
// bool read(Source& source, std::vector<Put>& puts, bool wire) {
//     auto result = true;
//     auto const count = source.read_size_little_endian();

//     // Guard against potential for arbitary memory allocation.
//     if (count > static_absolute_max_block_size()) {
//         source.invalidate();
//     } else {
//         puts.resize(count);
//     }

//     auto const deserialize = [&](Put& put) {
//         result = result && put.from_data(source, wire);
// #ifndef NDEBUG
//         put.script().operations();
// #endif
//     };

//     std::for_each(puts.begin(), puts.end(), deserialize);
//     return result;
// }

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


class transaction_basis;

hash_digest hash(transaction_basis const& tx);
hash_digest outputs_hash(transaction_basis const& tx);
hash_digest inpoints_hash(transaction_basis const& tx);
hash_digest sequences_hash(transaction_basis const& tx);
hash_digest utxos_hash(transaction_basis const& tx);

hash_digest to_outputs(transaction_basis const& tx);
hash_digest to_inpoints(transaction_basis const& tx);
hash_digest to_sequences(transaction_basis const& tx);
hash_digest to_utxos(transaction_basis const& tx);

uint64_t total_input_value(transaction_basis const& tx);
uint64_t total_output_value(transaction_basis const& tx);
uint64_t fees(transaction_basis const& tx);
bool is_overspent(transaction_basis const& tx);


struct KD_API transaction_basis {
    using ins = input::list;
    using outs = output::list;
    using list = std::vector<transaction_basis>;
    using hash_ptr = std::shared_ptr<hash_digest>;

    // Constructors.
    //-----------------------------------------------------------------------------

    transaction_basis() = default;
    transaction_basis(uint32_t version, uint32_t locktime, ins const& inputs, outs const& outputs);
    transaction_basis(uint32_t version, uint32_t locktime, ins&& inputs, outs&& outputs);

    // Operators.
    //-----------------------------------------------------------------------------

    bool operator==(transaction_basis const& x) const;
    bool operator!=(transaction_basis const& x) const;

    // Deserialization.
    //-----------------------------------------------------------------------------

    static
    expect<transaction_basis> from_data(byte_reader& reader, bool wire = true);

    [[nodiscard]]
    bool is_valid() const;

    // Serialization.
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data(bool wire = true) const;

    void to_data(data_sink& stream, bool wire = true) const;

    // Witness is not used by outputs, just for template normalization.
    template <typename W>
    void to_data(W& sink, bool wire = true) const {
        if (wire) {
            // Wire (satoshi protocol) serialization.
            sink.write_4_bytes_little_endian(version_);


            detail::write(sink, inputs_, wire);
            detail::write(sink, outputs_, wire);


            sink.write_4_bytes_little_endian(locktime_);
        } else {
            // Database (outputs forward) serialization.
            // Witness data is managed internal to inputs.
            detail::write(sink, outputs_, wire);
            detail::write(sink, inputs_, wire);
            sink.write_variable_little_endian(locktime_);
            sink.write_variable_little_endian(version_);
        }
    }

    // Properties (size, accessors, cache).
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
    code check(uint64_t total_output_value, size_t max_block_size, bool transaction_pool = true, bool retarget = true) const;

    [[nodiscard]]
    size_t min_tx_size(chain_state const& state) const;

    [[nodiscard]]
    code accept(chain_state const& state, bool is_overspent, bool is_duplicated, bool transaction_pool = true) const;

    [[nodiscard]]
    bool is_standard() const;

// protected:
    void reset();

protected:
    [[nodiscard]]
    bool all_inputs_final() const;

private:
    uint32_t version_{0};
    uint32_t locktime_{0};
    input::list inputs_;
    output::list outputs_;
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_TRANSACTION_BASIS_HPP
