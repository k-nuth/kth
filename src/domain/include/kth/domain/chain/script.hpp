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
#include <tuple>
#include <type_traits>
#include <vector>

#include <expected>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>

#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/domain/machine/script_pattern.hpp>
#include <kth/domain/wallet/ec_public.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/machine/script_version.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>

#include <kth/domain/concepts.hpp>

namespace kth::domain::chain {

class transaction;
class witness;

enum class endorsement_type {
    ecdsa,
    schnorr
};

struct KD_API script {
    using operation = machine::operation;
    using script_flags = machine::script_flags;
    using script_pattern = domain::machine::script_pattern;
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

    // Operators.
    //-------------------------------------------------------------------------

    friend
    constexpr bool operator==(script const&, script const&) = default;

    // Deserialization.
    //-------------------------------------------------------------------------

    static
    expect<script> from_data(byte_reader& reader, bool prefix);

    static
    expect<script> from_data_with_size(byte_reader& reader, size_t size);

    void from_operations(operation::list const& ops);
    void from_operations(operation::list&& ops);
    bool from_string(std::string const& mnemonic);

    /// A script object is valid if the byte count matches the prefix.
    [[nodiscard]]
    bool is_valid() const;

    /// Script operations is valid if all push ops have the predicated size.
    [[nodiscard]]
    bool is_valid_operations() const;

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
    std::string to_string(script_flags_t active_flags) const;

    // Properties (size, accessors).
    //-------------------------------------------------------------------------
    //
    // NOTE: operations are NOT cached. operations() returns a freshly-parsed
    // list on every call. Callers in hot paths must hoist the call outside the
    // loop. Caching, if needed, is the caller's responsibility.

    [[nodiscard]]
    size_t serialized_size(bool prefix) const;

    [[nodiscard]]
    data_chunk const& bytes() const;

    [[nodiscard]]
    operation::list operations() const;

    [[nodiscard]]
    operation first_operation() const;

    // Signing.
    //-------------------------------------------------------------------------

    static
    std::pair<hash_digest, size_t> generate_signature_hash(transaction const& tx,
                                        uint32_t input_index,
                                        script const& script_code,
                                        uint8_t sighash_type,
                                        script_flags_t active_flags,
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
                            script_flags_t active_flags,
#if ! defined(KTH_CURRENCY_BCH)
                            script_version version = script_version::unversioned,
#endif // ! KTH_CURRENCY_BCH
                            uint64_t value = max_uint64);

    static
    std::expected<endorsement, std::error_code> create_endorsement(
        ec_secret const& secret,
        script const& prevout_script,
        transaction const& tx,
        uint32_t input_index,
        uint8_t sighash_type,
        script_flags_t active_flags,
#if ! defined(KTH_CURRENCY_BCH)
        script_version version = script_version::unversioned,
#endif // ! KTH_CURRENCY_BCH
        uint64_t value = max_uint64,
        endorsement_type type = endorsement_type::ecdsa);

    // Utilities (static).
    //-------------------------------------------------------------------------

    /// Determine if any of the given flag bits are enabled in the active flags set.
    static constexpr
    bool is_enabled(script_flags_t active_flags, script_flags_t fork) {
        return (fork & active_flags) != 0;
    }

    /// Determine if multiple flags are enabled. Returns a tuple of bools
    /// usable with structured bindings:
    ///   auto const [a, b, c] = are_enabled(flags, fork_a, fork_b, fork_c);
    template <typename... Forks>
    static constexpr
    auto are_enabled(script_flags_t active_flags, Forks... flags) {
        return std::tuple{is_enabled(active_flags, flags)...};
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

#if defined(KTH_SEGWIT_ENABLED)
    static
    bool is_pay_witness_script_hash_pattern(operation::list const& ops);
#endif // KTH_SEGWIT_ENABLED

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
    operation::list to_null_data_pattern(byte_span data);

    static
    operation::list to_pay_public_key_pattern(byte_span point);

    static
    operation::list to_pay_public_key_hash_pattern(short_hash const& hash);

    static
    operation::list to_pay_public_key_hash_pattern_unlocking(endorsement const& end, wallet::ec_public const& pubkey);

    static
    operation::list to_pay_public_key_hash_pattern_unlocking_placeholder(size_t endorsement_size, size_t pubkey_size);

    static
    operation::list to_pay_script_hash_pattern_unlocking_placeholder(size_t script_size, bool multisig);

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
#if defined(KTH_SEGWIT_ENABLED)
    [[nodiscard]]
    data_chunk witness_program() const;
#endif // KTH_SEGWIT_ENABLED

#if ! defined(KTH_CURRENCY_BCH)
    [[nodiscard]]
    script_version version() const;
#endif // ! KTH_CURRENCY_BCH

    [[nodiscard]]
    script_pattern pattern() const;

    [[nodiscard]]
    script_pattern output_pattern() const;

    /// Flag-aware output-pattern classifier. Identical to `output_pattern()`
    /// except when `flags & bch_p2s` is active: any scriptPubKey that
    /// doesn't match one of the known templates and whose serialised size
    /// fits within `max_p2s_script_size` returns `pay_to_script` instead of
    /// `non_standard`. Mirrors BCHN's `Solver(scriptPubKey, ..., flags)`.
    [[nodiscard]]
    script_pattern output_pattern(script_flags_t flags) const;

    [[nodiscard]]
    script_pattern input_pattern() const;

    /// Consensus computations.
    [[nodiscard]]
    size_t sigops(bool accurate) const;

    [[nodiscard]]
    bool is_unspendable() const;

    void reset();

    [[nodiscard]]
    bool is_pay_to_script_hash(script_flags_t flags) const;

    [[nodiscard]]
    bool is_pay_to_script_hash_32(script_flags_t flags) const;

    // Validation.
    //-------------------------------------------------------------------------

    static
    code verify(transaction const& tx, uint32_t input_index, script_flags_t flags, script const& input_script, script const& prevout_script, uint64_t /*value*/);

    static
    code verify(transaction const& tx, uint32_t input, script_flags_t flags);

private:
    static
    size_t serialized_size(operation::list const& ops);

    static
    data_chunk operations_to_data(operation::list const& ops);

    static
    std::pair<hash_digest, size_t> generate_unversioned_signature_hash(transaction const& tx, uint32_t input_index, script const& script_code, uint8_t sighash_type);

    static
    std::pair<hash_digest, size_t> generate_version_0_signature_hash(
        transaction const& tx,
        uint32_t input_index,
        script const& script_code,
        uint64_t value,
        uint8_t sighash_type,
        script_flags_t active_flags);

    data_chunk bytes_;
    bool valid_{false};
};

machine::operation::list operations(script const& s);
machine::operation first_operation(script const& s);

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_SCRIPT_HPP
