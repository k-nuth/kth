// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_BITCOIN_URI_HPP
#define KTH_WALLET_BITCOIN_URI_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>

namespace kth::domain::wallet {

/// BIP21 / BIP72 bitcoin URI (`bitcoin:address?params`).
///
/// Immutable value type. Every reachable instance came from
/// `parse_from(...)` or `from_parts(...)`, both returning `expect<>`;
/// there's no default ctor and no post-construction mutation.
///
/// To build a URI programmatically, populate a `bitcoin_uri::parts`
/// and hand it to `from_parts`:
///
///     auto uri = bitcoin_uri::from_parts({
///         .address = payment.to_string(),
///         .amount  = 10'000'000,
///         .label   = "coffee",
///     });
struct KD_API bitcoin_uri {
    /// Caller-populated parts used by `from_parts`. Field-order-only
    /// aggregate init; all fields are optional except the address
    /// (an empty address string produces a `bitcoin:` URI with no
    /// path, which is a legal BIP21 form for "params only" URIs).
    struct parts {
        std::string address;
        std::optional<uint64_t> amount;
        std::optional<std::string> label;
        std::optional<std::string> message;
        std::optional<std::string> r;
    };

    /// Parse a URI string. Returns `error::illegal_value` on malformed
    /// input.
    [[nodiscard]] static
    expect<bitcoin_uri> parse_from(std::string_view uri, bool strict);

    /// Build a URI value from caller-populated parts. Fails when the
    /// address is non-empty but decodes neither as a `payment_address`
    /// nor a `stealth_address`.
    [[nodiscard]] static
    expect<bitcoin_uri> from_parts(parts values);

    // Equality only — no meaningful total order over URIs. `!=` is
    // auto-synthesized by C++20 from `==`.
    [[nodiscard]]
    friend bool operator==(bitcoin_uri const&, bitcoin_uri const&) = default;

    /// Serialized URI. Used by `fmt::formatter<bitcoin_uri>`.
    [[nodiscard]]
    std::string to_string() const;

    /// Property getters.
    [[nodiscard]] uint64_t                amount() const;
    [[nodiscard]] std::string             label() const;
    [[nodiscard]] std::string             message() const;
    [[nodiscard]] std::string             r() const;
    [[nodiscard]] std::string const&      address() const noexcept { return address_; }
    [[nodiscard]] expect<payment_address> payment() const;
    [[nodiscard]] expect<stealth_address> stealth() const;
    [[nodiscard]] std::string             parameter(std::string const& key) const;

private:
    using query_map = boost::unordered_flat_map<std::string, std::string>;

    bitcoin_uri(std::string address, query_map query);

    std::string const address_;
    query_map const query_;
};

} // namespace kth::domain::wallet

#endif
