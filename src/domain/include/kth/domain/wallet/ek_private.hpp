// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_EK_PRIVATE_HPP
#define KTH_WALLET_EK_PRIVATE_HPP

#include <iostream>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/wallet/encrypted_keys.hpp>

namespace kth::domain::wallet {

/// Use to pass an encrypted private key.
struct KD_API ek_private {
    /// Constructors.
    ek_private();
    ek_private(std::string const& encoded);
    ek_private(encrypted_private const& value);
    ek_private(ek_private const& x) = default;

    ek_private& operator=(ek_private const& x) = default;

    /// Operators.
    bool operator==(ek_private const& x) const;
    bool operator!=(ek_private const& x) const;
    bool operator<(ek_private const& x) const;

    friend std::istream& operator>>(std::istream& in, ek_private& to);
    friend std::ostream& operator<<(std::ostream& out, ek_private const& of);

    /// Cast operators.
    operator bool() const;
    operator encrypted_private const&() const;

    /// Serializer.
    [[nodiscard]]
    std::string encoded() const;

    /// Accessors.
    [[nodiscard]]
    encrypted_private const& private_key() const;

private:
    /// Factories.
    static
    ek_private from_string(std::string const& encoded);

    /// Members.
    /// These should be const, apart from the need to implement assignment.
    bool valid_{false};
    encrypted_private private_;
};

} // namespace kth::domain::wallet

#endif
