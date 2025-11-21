// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


//TODO(fernando): use Boost.URL

#ifndef KTH_WALLET_BITCOIN_URI_HPP
#define KTH_WALLET_BITCOIN_URI_HPP

#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
// #include <kth/domain/wallet/uri_reader.hpp>

namespace kth::domain::wallet {

/// A bitcoin URI corresponding to BIP 21 and BIP 72.
/// The object is not constant, setters can change state after construction.
// class KD_API bitcoin_uri : public uri_reader {
struct KD_API bitcoin_uri {
    /// Constructors.
    bitcoin_uri() = default;
    bitcoin_uri(std::string const& uri, bool strict = true);

    bitcoin_uri(bitcoin_uri const& x) = default;
    bitcoin_uri& operator=(bitcoin_uri const& x) = default;

    /// Operators.
    bool operator==(bitcoin_uri const& x) const;
    bool operator!=(bitcoin_uri const& x) const;

    bool operator<(bitcoin_uri const& x) const;

    friend std::istream& operator>>(std::istream& in, bitcoin_uri& to);
    friend std::ostream& operator<<(std::ostream& out, bitcoin_uri const& from);

    /// Test whether the URI has been initialized.
    operator bool() const;

    /// Get the serialized URI representation.
    [[nodiscard]]
    std::string encoded() const;

    /// Property getters.
    [[nodiscard]]
    uint64_t amount() const;

    [[nodiscard]]
    std::string label() const;

    [[nodiscard]]
    std::string message() const;

    [[nodiscard]]
    std::string r() const;

    [[nodiscard]]
    std::string address() const;

    [[nodiscard]]
    payment_address payment() const;

    [[nodiscard]]
    stealth_address stealth() const;

    [[nodiscard]]
    std::string parameter(std::string const& key) const;

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
    /// Private helpers.
    bool set_amount(std::string const& satoshis);

    /// Member state.
    bool strict_{true};
    std::string scheme_;
    std::string address_;
    std::map<std::string, std::string> query_;
};

} // namespace kth::domain::wallet

#endif
