// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_OUTPUT_HPP
#define KTH_DOMAIN_CHAIN_OUTPUT_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

#include <kth/domain/concepts.hpp>

namespace kth::domain::chain {

struct KD_API output {
    using list = std::vector<output>;

    /// This is a sentinel used in .value to indicate not found in store.
    /// This is a consensus value used in script::generate_signature_hash.
    /// This is a consensus critical value that must be set on reset.
    static constexpr uint64_t not_found = sighash_null_value;

    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    struct validation_t {
        /// This is a non-consensus sentinel indicating output is unspent.
        static constexpr uint32_t not_spent = max_uint32;

        size_t spender_height = validation_t::not_spent;
    };

    // Constructors.
    //-------------------------------------------------------------------------

    output() = default;
    output(uint64_t value, chain::script const& script, token_data_opt const& token_data);
    output(uint64_t value, chain::script&& script, token_data_opt&& token_data);

    // Operators.
    //-------------------------------------------------------------------------

    // NOTE: `==` is not `= default` because the `validation` member is
    // tracing metadata, not part of the value's identity. `!=` is defaulted
    // (delegates to `!(==)`) so callers that spell out the member call still
    // resolve.
    friend
    bool operator==(output const&, output const&);

    friend
    bool operator!=(output const&, output const&) = default;

    // Serialization.
    //-------------------------------------------------------------------------

    static
    expect<output> from_data(byte_reader& reader, bool wire = true);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, bool wire) const;

    [[nodiscard]]
    size_t serialized_size(bool wire = true) const;

    // Properties (accessors).
    //-------------------------------------------------------------------------

    [[nodiscard]]
    uint64_t value() const;

    void set_value(uint64_t value);

    // [[deprecated]] // unsafe
    chain::script& script();

    [[nodiscard]]
    chain::script const& script() const;

    void set_script(chain::script const& value);
    void set_script(chain::script&& value);

    // [[deprecated]] // unsafe
    token_data_opt& token_data();

    [[nodiscard]]
    token_data_opt const& token_data() const;

    void set_token_data(token_data_opt const& value);
    void set_token_data(token_data_opt&& value);

    /// The first payment address extracted from this output as a
    /// standard script, or `nullopt` if the script has no recognised
    /// pattern. NOTE: not cached — calling this in a hot loop
    /// recomputes each time.
    [[nodiscard]]
    std::optional<wallet::payment_address> address(bool testnet = false) const;

    /// The first payment address extracted, or `nullopt` if the
    /// script has no recognised pattern.
    [[nodiscard]]
    std::optional<wallet::payment_address> address(uint8_t p2kh_version, uint8_t p2sh_version) const;

    /// The payment addresses extracted from this output as a standard script.
    /// NOTE: not cached — caller owns any caching it needs.
    [[nodiscard]]
    wallet::payment_address::list addresses(
        uint8_t p2kh_version = wallet::payment_address::mainnet_p2kh,
        uint8_t p2sh_version = wallet::payment_address::mainnet_p2sh) const;

    // Validation.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    bool is_valid() const;

    [[nodiscard]]
    size_t signature_operations(bool bip141) const;

    [[nodiscard]]
    bool is_dust(uint64_t minimum_output_value) const;

    void reset();

    // THIS IS FOR LIBRARY USE ONLY, DO NOT CREATE A DEPENDENCY ON IT.
    // Holds the spender_height for internal-DB serialization (wire=false).
    // Mutable because callers update this on otherwise-const outputs while
    // walking the chain state.
    mutable validation_t validation{};

private:
    uint64_t value_{not_found};
    chain::script script_;
    token_data_opt token_data_;
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_OUTPUT_HPP
