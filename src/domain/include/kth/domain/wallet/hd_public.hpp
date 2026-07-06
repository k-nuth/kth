// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_HD_PUBLIC_KEY_HPP
#define KTH_DOMAIN_WALLET_HD_PUBLIC_KEY_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/hd_lineage.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

/// A constant used in key derivation.
static constexpr uint32_t hd_first_hardened_key = 1 << 31;

/// An hd key chain code.
static constexpr size_t hd_chain_code_size = 32;
using hd_chain_code = byte_array<hd_chain_code_size>;

/// A decoded hd public or private key.
static constexpr size_t hd_key_size = 82;
using hd_key = byte_array<hd_key_size>;

class hd_private;

/**
 * BIP32 extended public key.
 *
 * Default-constructible (invalid state) so callers that hold an
 * `hd_public` as a struct member can fill it later via assignment.
 * Fallible construction goes through `parse_from` /
 * `parse_from_with_prefix` which return `expect<hd_public>`.
 */
struct KD_API hd_public {
    static constexpr uint32_t mainnet = 76067358;
    static constexpr uint32_t testnet = 70617039;

    static constexpr
    uint32_t to_prefix(uint64_t prefixes) {
        return prefixes & 0x00000000FFFFFFFF;
    }

    [[nodiscard]]
    static
    expect<hd_public> parse_from(std::string_view encoded);

    [[nodiscard]]
    static
    expect<hd_public> parse_from_with_prefix(std::string_view encoded, uint32_t prefix);

    hd_public();

    explicit
    hd_public(hd_key const& public_key);

    hd_public(hd_key const& public_key, uint32_t prefix);

    [[nodiscard]]
    friend bool operator==(hd_public const&, hd_public const&) = default;

    [[nodiscard]]
    friend auto operator<=>(hd_public const& a, hd_public const& b) {
        return a.to_string() <=> b.to_string();
    }

    [[nodiscard]]
    bool valid() const noexcept { return valid_; }

    /// Base58 encoding used by `fmt::formatter<hd_public>`.
    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]]
    hd_chain_code const& chain_code() const noexcept { return chain_; }

    [[nodiscard]]
    hd_lineage const& lineage() const noexcept { return lineage_; }

    [[nodiscard]]
    ec_compressed const& point() const noexcept { return point_; }

    [[nodiscard]]
    hd_key to_hd_key() const;

    [[nodiscard]]
    hd_public derive_public(uint32_t index) const;

protected:
    static
    hd_public from_secret(ec_secret const& secret, hd_chain_code const& chain_code, hd_lineage const& lineage);

    uint32_t fingerprint() const;

    // These should be const, apart from the need to implement assignment.
    bool valid_{false};
    hd_chain_code chain_;
    hd_lineage lineage_;
    ec_compressed point_;

private:
    static
    hd_public from_key(hd_key const& key);

    static
    hd_public from_key(hd_key const& key, uint32_t prefix);

    hd_public(ec_compressed const& point, hd_chain_code const& chain_code, hd_lineage const& lineage);
};

} // namespace kth::domain::wallet

#endif
