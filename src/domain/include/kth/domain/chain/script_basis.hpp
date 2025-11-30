// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_SCRIPT_BASIS_HPP
#define KTH_DOMAIN_CHAIN_SCRIPT_BASIS_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <type_traits>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>

#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/rule_fork.hpp>
#include <kth/domain/wallet/ec_public.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/machine/script_pattern.hpp>
#include <kth/infrastructure/machine/script_version.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::chain {

class transaction;
struct KD_API script_basis {
    using operation = machine::operation;
    using rule_fork = machine::rule_fork;
    using script_pattern = infrastructure::machine::script_pattern;
#if ! defined(KTH_CURRENCY_BCH)
    using script_version = infrastructure::machine::script_version;
#endif // ! KTH_CURRENCY_BCH

    // Constructors.
    //-------------------------------------------------------------------------

    script_basis() = default;
    script_basis(data_chunk const& encoded, bool prefix);
    script_basis(data_chunk&& encoded, bool prefix);

    // Operators.
    //-------------------------------------------------------------------------

    bool operator==(script_basis const& x) const;
    bool operator!=(script_basis const& x) const;

    // Deserialization.
    //-------------------------------------------------------------------------

    static
    expect<script_basis> from_data(byte_reader& reader, bool prefix);

    static
    expect<script_basis> from_data_with_size(byte_reader& reader, size_t size);

    /// Deserialization invalidates the iterator.
    void from_operations(operation::list const& ops);
    bool from_string(std::string const& mnemonic);

    /// A script object is valid if the byte count matches the prefix.
    [[nodiscard]]
    bool is_valid() const;

    // Serialization.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data(bool prefix) const;
    void to_data(data_sink& stream, bool prefix) const;

    template <typename W>
    void to_data(W& sink, bool prefix) const {
        // TODO(legacy): optimize by always storing the prefixed serialization.
        if (prefix) {
            sink.write_variable_little_endian(serialized_size(false));
        }

        sink.write_bytes(bytes_);
    }

    [[nodiscard]]
    std::string to_string(uint32_t active_forks) const;


    // Properties (size, accessors, cache).
    //-------------------------------------------------------------------------

    [[nodiscard]]
    size_t serialized_size(bool prefix) const;

    [[nodiscard]]
    data_chunk const& bytes() const;
    // operation::list const& operations() const;

    // Utilities (static).
    //-------------------------------------------------------------------------

    /// Determine if the fork is enabled in the active forks set.
    static
    bool is_enabled(uint32_t active_forks, rule_fork fork) {
        return (fork & active_forks) != 0;
    }

    /// Consensus patterns.
    static
    bool is_push_only(operation::list const& ops);

    static
    bool is_relaxed_push(operation::list const& ops);

    static
    bool is_coinbase_pattern(operation::list const& ops, size_t height);

    /// Common output patterns (psh and pwsh are also consensus).
    static
    bool is_null_data_pattern(operation::list const& ops);

    static
    bool is_pay_multisig_pattern(operation::list const& ops);

    static
    bool is_pay_public_key_pattern(operation::list const& ops);

    static
    bool is_pay_public_key_hash_pattern(operation::list const& ops);

    static
    bool is_pay_script_hash_pattern(operation::list const& ops);

    static
    bool is_pay_script_hash_32_pattern(operation::list const& ops);

    /// Common input patterns (skh is also consensus).
    static
    bool is_sign_multisig_pattern(operation::list const& ops);

    static
    bool is_sign_public_key_pattern(operation::list const& ops);

    static
    bool is_sign_public_key_hash_pattern(operation::list const& ops);

    static
    bool is_sign_script_hash_pattern(operation::list const& ops);

    /// Stack factories.
    static
    operation::list to_null_data_pattern(data_slice data);

    static
    operation::list to_pay_public_key_pattern(data_slice point);

    static
    operation::list to_pay_public_key_hash_pattern(short_hash const& hash);

    static
    operation::list to_pay_public_key_hash_pattern_unlocking(endorsement const& end, wallet::ec_public const& public_key);

    static
    operation::list to_pay_public_key_hash_pattern_unlocking_placeholder(size_t endorsement_size, size_t pubkey_size);

    static
    operation::list to_pay_script_hash_pattern(short_hash const& hash);

    static
    operation::list to_pay_script_hash_32_pattern(hash_digest const& hash);

    static
    operation::list to_pay_multisig_pattern(uint8_t signatures, point_list const& points);

    static
    operation::list to_pay_multisig_pattern(uint8_t signatures, data_stack const& points);

    // Utilities (non-static).
    //-------------------------------------------------------------------------

    /// Common pattern detection.
    [[nodiscard]]
    script_pattern pattern() const;

    [[nodiscard]]
    script_pattern output_pattern() const;

    [[nodiscard]]
    script_pattern input_pattern() const;

    /// Consensus computations.
    [[nodiscard]]
    size_t sigops(bool accurate) const;

    [[nodiscard]]
    bool is_unspendable() const;

    void reset();

// protected:

    //TODO: implement is_pay_to_public_key_hash


    [[nodiscard]]
    bool is_pay_to_script_hash(uint32_t forks) const;

    [[nodiscard]]
    bool is_pay_to_script_hash_32(uint32_t forks) const;

// private:
    static
    size_t serialized_size(operation::list const& ops);
protected:
    static
    data_chunk operations_to_data(operation::list const& ops);

    // static
    // hash_digest generate_unversioned_signature_hash(transaction const& tx, uint32_t input_index, script_basis const& script_code, uint8_t sighash_type);

    static
    std::pair<hash_digest, size_t> generate_version_0_signature_hash(
        transaction const& tx,
        uint32_t input_index,
        script_basis const& script_code,
        uint64_t value,
        uint8_t sighash_type,
        uint32_t active_forks
    );

    data_chunk bytes_;
    bool valid_{false};
};

machine::operation::list operations(script_basis const& script);
machine::operation first_operation(script_basis const& script);

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_SCRIPT_BASIS_HPP
