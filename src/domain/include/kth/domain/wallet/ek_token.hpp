// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_EK_TOKEN_HPP
#define KTH_WALLET_EK_TOKEN_HPP

#include <iostream>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/wallet/encrypted_keys.hpp>

namespace kth::domain::wallet {

/**
 * Serialization helper to convert between base58 string and bip38 token.
 */
struct KD_API ek_token {
    /// Constructors.
    ek_token();
    ek_token(std::string const& encoded);
    ek_token(encrypted_token const& value);
    ek_token(ek_token const& x) = default;


    ek_token& operator=(ek_token const& x) = default;

    /// Operators.
    bool operator==(ek_token const& x) const;
    bool operator!=(ek_token const& x) const;
    bool operator<(ek_token const& x) const;

    friend std::istream& operator>>(std::istream& in, ek_token& to);
    friend std::ostream& operator<<(std::ostream& out, ek_token const& of);

    /// Cast operators.
    operator bool() const;
    operator encrypted_token const&() const;

    /// Serializer.
    [[nodiscard]]
    std::string encoded() const;

    /// Accessors.
    [[nodiscard]]
    encrypted_token const& token() const;

private:
    /// Factories.
    static
    ek_token from_string(std::string const& encoded);

    /// Members.
    /// These should be const, apart from the need to implement assignment.
    bool valid_{false};
    encrypted_token token_;
};

} // namespace kth::domain::wallet

#endif
