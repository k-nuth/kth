// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_EK_PRIVATE_HPP
#define KTH_WALLET_EK_PRIVATE_HPP

#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/encrypted_keys.hpp>

namespace kth::domain::wallet {

/**
 * Encrypted private key (BIP38).
 *
 * Fallible construction goes through `parse_from` which returns
 * `expect<ek_private>`.
 */
struct KD_API ek_private {
    [[nodiscard]]
    static
    expect<ek_private> parse_from(std::string_view encoded);

    explicit
    ek_private(encrypted_private const& value)
        : valid_(true), private_(value) {}

    [[nodiscard]]
    friend bool operator==(ek_private const&, ek_private const&) = default;

    [[nodiscard]]
    friend auto operator<=>(ek_private const& a, ek_private const& b) {
        return a.to_string() <=> b.to_string();
    }

    [[nodiscard]]
    bool valid() const noexcept { return valid_; }

    [[nodiscard]]
    encrypted_private const& value() const noexcept { return private_; }

    [[nodiscard]]
    encrypted_private const& private_key() const noexcept { return private_; }

    /// Base58 encoding used by `fmt::formatter<ek_private>`.
    [[nodiscard]]
    std::string to_string() const;

private:
    bool valid_{false};
    encrypted_private private_{};
};

} // namespace kth::domain::wallet

#endif
