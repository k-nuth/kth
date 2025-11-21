// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_EK_PUBLIC_HPP
#define KTH_WALLET_EK_PUBLIC_HPP

#include <iostream>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/wallet/encrypted_keys.hpp>

namespace kth::domain::wallet {

/// Use to pass an encrypted public key.
struct KD_API ek_public {
    /// Constructors.
    ek_public();
    ek_public(std::string const& encoded);
    ek_public(encrypted_public const& value);
    ek_public(ek_public const& x) = default;

    ek_public& operator=(ek_public const& x) = default;

    /// Operators.

    bool operator==(ek_public const& x) const;
    bool operator!=(ek_public const& x) const;
    bool operator<(ek_public const& x) const;
    friend std::istream& operator>>(std::istream& in, ek_public& to);
    friend std::ostream& operator<<(std::ostream& out, ek_public const& of);

    /// Cast operators.
    operator bool() const;
    operator encrypted_public const&() const;

    /// Serializer.
    [[nodiscard]]
    std::string encoded() const;

    /// Accessors.
    [[nodiscard]]
    encrypted_public const& public_key() const;

private:
    /// Factories.
    static
    ek_public from_string(std::string const& encoded);

    /// Members.
    /// These should be const, apart from the need to implement assignment.
    bool valid_{false};
    encrypted_public public_;
};

} // namespace kth::domain::wallet

#endif
