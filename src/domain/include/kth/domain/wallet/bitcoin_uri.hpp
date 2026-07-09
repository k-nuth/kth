// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_BITCOIN_URI_HPP
#define KTH_WALLET_BITCOIN_URI_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>

namespace kth::domain::wallet {

/**
 * BIP21 / BIP72 bitcoin URI (`bitcoin:address?params`).
 *
 * Builder-style API: default-construct then populate via setters, or
 * consume a serialized URI via `parse_from` which returns
 * `expect<bitcoin_uri>`. Use `valid()` to test whether any field has
 * been populated.
 */
struct KD_API bitcoin_uri {
    /// Parse a URI string. Returns `error::illegal_value` on malformed
    /// input.
    [[nodiscard]] static
    expect<bitcoin_uri> parse_from(std::string_view uri, bool strict = true);

    /// Explicit so the C-API generator binds it as `construct_default`.
    /// The builder-style setters expect this empty starting point.
    bitcoin_uri() = default;

    [[nodiscard]]
    friend bool operator==(bitcoin_uri const&, bitcoin_uri const&) = default;

    /// Provide the full comparison suite (`<`, `<=`, `>`, `>=`) via the
    /// spaceship operator so callers don't have to remember which single
    /// ordering operator we defined.
    [[nodiscard]]
    friend auto operator<=>(bitcoin_uri const& a, bitcoin_uri const& b) {
        return a.to_string() <=> b.to_string();
    }

    /// True when at least one field has been populated.
    [[nodiscard]]
    bool valid() const;

    /// Serialized URI. Used by `fmt::formatter<bitcoin_uri>`.
    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]]
    std::string encoded() const { return to_string(); }

    /// Property getters.
    [[nodiscard]] uint64_t        amount() const;
    [[nodiscard]] std::string     label() const;
    [[nodiscard]] std::string     message() const;
    [[nodiscard]] std::string     r() const;
    [[nodiscard]] std::string     address() const;
    [[nodiscard]] expect<payment_address> payment() const;
    [[nodiscard]] expect<stealth_address> stealth() const;
    [[nodiscard]] std::string     parameter(std::string const& key) const;

    /// Property setters.
    void set_amount(uint64_t satoshis);
    void set_label(std::string const& label);
    void set_message(std::string const& message);
    void set_r(std::string const& r);
    bool set_address(std::string const& address);
    void set_address(payment_address const& payment);
    void set_address(stealth_address const& stealth);

    /// uri_reader implementation.
    void set_strict(bool strict);
    bool set_scheme(std::string const& scheme);
    bool set_authority(std::string const& authority);
    bool set_path(std::string const& path);
    bool set_fragment(std::string const& fragment);
    bool set_parameter(std::string const& key, std::string const& value);

private:
    bool set_amount(std::string const& satoshis);

    bool strict_{true};
    std::string scheme_;
    std::string address_;
    std::map<std::string, std::string> query_;
};

} // namespace kth::domain::wallet

#endif
