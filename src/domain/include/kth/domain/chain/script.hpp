// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_SCRIPT_HPP
#define KTH_DOMAIN_CHAIN_SCRIPT_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <type_traits>

#include <nonstd/expected.hpp>

#include <kth/domain/chain/script_basis.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>

#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/rule_fork.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/machine/script_pattern.hpp>
#include <kth/infrastructure/machine/script_version.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>
namespace kth::domain::chain {

class transaction;
class witness;

enum class endorsement_type {
    ecdsa,
    schnorr
};

struct KD_API script : script_basis {
public:
    using operation = machine::operation;
    using rule_fork = machine::rule_fork;
    using script_pattern = infrastructure::machine::script_pattern;
#if ! defined(KTH_CURRENCY_BCH)
    using script_version = infrastructure::machine::script_version;
#endif // ! KTH_CURRENCY_BCH

    // Constructors.
    //-------------------------------------------------------------------------

    script() = default;

    explicit
    script(operation::list const& ops);

    explicit
    script(operation::list&& ops);

    explicit
    script(data_chunk const& encoded, bool prefix);

    script(data_chunk&& encoded, bool prefix);

    explicit
    script(script_basis const& x);

    explicit
    script(script_basis&& x) noexcept;

    // Special member functions.
    //-------------------------------------------------------------------------

    script(script const& x);
    script(script&& x) noexcept;
    script& operator=(script const& x);
    script& operator=(script&& x) noexcept;

    // Deserialization.
    //-------------------------------------------------------------------------

    static
    expect<script> from_data(byte_reader& reader, bool wire);

    static
    expect<script> from_data_with_size(byte_reader& reader, size_t size);

    /// Deserialization invalidates the iterator.
    void from_operations(operation::list&& ops);
    void from_operations(operation::list const& ops);
    bool from_string(std::string const& mnemonic);

    /// Script operations is valid if all push ops have the predicated size.
    bool is_valid_operations() const;

    // Serialization.
    //-------------------------------------------------------------------------

    std::string to_string(uint32_t active_forks) const;

    // Iteration.
    //-------------------------------------------------------------------------

    void clear();
    bool empty() const;
    size_t size() const;
    operation const& front() const;
    operation const& back() const;
    operation::iterator begin() const;
    operation::iterator end() const;
    operation const& operator[](size_t index) const;

    // Properties (size, accessors, cache).
    //-------------------------------------------------------------------------

    // size_t serialized_size(bool prefix) const;
    using script_basis::serialized_size;
    operation::list const& operations() const;
    operation first_operation() const;

    // Signing.
    //-------------------------------------------------------------------------

    static
    std::pair<hash_digest, size_t> generate_signature_hash(transaction const& tx,
                                        uint32_t input_index,
                                        script const& script_code,
                                        uint8_t sighash_type,
                                        uint32_t active_forks,
#if ! defined(KTH_CURRENCY_BCH)
                                        script_version version = script_version::unversioned,
#endif // ! KTH_CURRENCY_BCH
                                        uint64_t value = max_uint64);

    static
    std::pair<bool, size_t> check_signature(ec_signature const& signature,
                            uint8_t sighash_type,
                            data_chunk const& public_key,
                            script const& script_code,
                            transaction const& tx,
                            uint32_t input_index,
                            uint32_t active_forks,
#if ! defined(KTH_CURRENCY_BCH)
                            script_version version = script_version::unversioned,
#endif // ! KTH_CURRENCY_BCH
                            uint64_t value = max_uint64);

    // static
    // bool create_endorsement(endorsement& out, ec_secret const& secret, script const& prevout_script, transaction const& tx, uint32_t input_index, uint8_t sighash_type, script_version version = script_version::unversioned, uint64_t value = max_uint64);

    static
    nonstd::expected<endorsement, std::error_code> create_endorsement(
        ec_secret const& secret,
        script const& prevout_script,
        transaction const& tx,
        uint32_t input_index,
        uint8_t sighash_type,
        uint32_t active_forks,
#if ! defined(KTH_CURRENCY_BCH)
        script_version version = script_version::unversioned,
#endif // ! KTH_CURRENCY_BCH
        uint64_t value = max_uint64,
        endorsement_type type = endorsement_type::ecdsa);


    // Utilities (static).
    //-------------------------------------------------------------------------

    /// Transaction helpers.
    // static
    // hash_digest to_outputs(transaction const& tx);

    // static
    // hash_digest to_inpoints(transaction const& tx);

    // static
    // hash_digest to_sequences(transaction const& tx);

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

    static
    bool is_pay_witness_script_hash_pattern(operation::list const& ops);

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
    operation::list to_pay_public_key_hash_pattern_unlocking(endorsement const& end, wallet::ec_public const& pubkey);

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
    data_chunk witness_program() const;

#if ! defined(KTH_CURRENCY_BCH)
    script_version version() const;
#endif // ! KTH_CURRENCY_BCH

    script_pattern pattern() const;
    script_pattern output_pattern() const;
    script_pattern input_pattern() const;

    /// Consensus computations.
    size_t sigops(bool accurate) const;
    bool is_unspendable() const;

    void reset();

    bool is_pay_to_script_hash(uint32_t forks) const;

    bool is_pay_to_script_hash_32(uint32_t forks) const;

    // Validation.
    //-----------------------------------------------------------------------------

    //TODO: move to script_basis (?)
    static
    code verify(transaction const& tx, uint32_t input_index, uint32_t forks, script const& input_script, script const& prevout_script, uint64_t /*value*/);

    static
    code verify(transaction const& tx, uint32_t input, uint32_t forks);

private:
    static
    size_t serialized_size(operation::list const& ops);

    static
    data_chunk operations_to_data(operation::list const& ops);

#if ! defined(KTH_CURRENCY_BCH)
    static
    std::pair<hash_digest, size_t> generate_unversioned_signature_hash(transaction const& tx, uint32_t input_index, script const& script_code, uint8_t sighash_type);
#endif // ! KTH_CURRENCY_BCH

    // These are protected by mutex.
    mutable bool cached_{false};
    mutable operation::list operations_;

#if ! defined(__EMSCRIPTEN__)
    mutable upgrade_mutex mutex_;
#else
    mutable shared_mutex mutex_;
#endif
};

} // namespace kth::domain::chain

#endif
