// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_EK_PUBLIC_HPP
#define KTH_WALLET_EK_PUBLIC_HPP

#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/encrypted_keys.hpp>

namespace kth::domain::wallet {

/**
 * Encrypted public key (BIP38).
 *
 * Fallible construction goes through `parse_from` which returns
 * `expect<ek_public>`.
 */
struct KD_API ek_public {
    [[nodiscard]]
    static
    expect<ek_public> parse_from(std::string_view encoded);

    explicit
    ek_public(encrypted_public const& value)
        : valid_(true), public_(value) {}

    [[nodiscard]]
    friend bool operator==(ek_public const&, ek_public const&) = default;

    [[nodiscard]]
    friend auto operator<=>(ek_public const& a, ek_public const& b) {
        return a.to_string() <=> b.to_string();
    }

    [[nodiscard]]
    bool valid() const noexcept { return valid_; }

    [[nodiscard]]
    encrypted_public const& value() const noexcept { return public_; }

    [[nodiscard]]
    encrypted_public const& public_key() const noexcept { return public_; }

    [[nodiscard]]
    std::string to_string() const;

private:
    bool valid_{false};
    encrypted_public public_{};
};

} // namespace kth::domain::wallet

#endif
