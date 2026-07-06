// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_EK_TOKEN_HPP
#define KTH_WALLET_EK_TOKEN_HPP

#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/encrypted_keys.hpp>

namespace kth::domain::wallet {

/**
 * BIP38 intermediate passphrase token.
 *
 * Default-constructible so the C-API generator can hand out an "empty"
 * handle; string parsing goes through `parse_from` which returns
 * `expect<ek_token>`.
 */
struct KD_API ek_token {
    [[nodiscard]]
    static
    expect<ek_token> parse_from(std::string_view encoded);

    ek_token() = default;

    explicit
    ek_token(encrypted_token const& value)
        : valid_(true), token_(value) {}

    [[nodiscard]]
    friend bool operator==(ek_token const&, ek_token const&) = default;

    [[nodiscard]]
    friend auto operator<=>(ek_token const& a, ek_token const& b) {
        return a.to_string() <=> b.to_string();
    }

    [[nodiscard]]
    bool valid() const noexcept { return valid_; }

    [[nodiscard]]
    encrypted_token const& value() const noexcept { return token_; }

    [[nodiscard]]
    encrypted_token const& token() const noexcept { return token_; }

    [[nodiscard]]
    std::string to_string() const;

private:
    bool valid_{false};
    encrypted_token token_{};
};

} // namespace kth::domain::wallet

#endif
