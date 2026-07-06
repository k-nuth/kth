// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_HD_PRIVATE_KEY_HPP
#define KTH_DOMAIN_WALLET_HD_PRIVATE_KEY_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/hd_public.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

constexpr
uint64_t to_prefixes(uint32_t private_prefix, uint32_t public_prefix) {
    return uint64_t(private_prefix) << 32 | public_prefix;
}

/**
 * BIP32 extended private key.
 *
 * Derives from `hd_public`. Default-constructible so callers that hold
 * an `hd_private` as a struct member can fill it later via assignment;
 * fallible construction goes through the `parse_from`-family static
 * factories, which return `expect<hd_private>`.
 */
struct KD_API hd_private : hd_public {
    static constexpr uint64_t mainnet = to_prefixes(76066276, hd_public::mainnet);
    static constexpr uint64_t testnet = to_prefixes(70615956, hd_public::testnet);

    static constexpr
    uint32_t to_prefix(uint64_t prefixes) {
        return prefixes >> 32;
    }

    /// Parse a base58-encoded key using the default mainnet prefixes.
    [[nodiscard]]
    static
    expect<hd_private> parse_from(std::string_view encoded);

    /// Parse a base58-encoded key with a caller-supplied public-side
    /// prefix. Named distinctly from `parse_from_with_prefixes` so C++
    /// integer promotion at the call site can never pick the wrong
    /// overload (`uint32_t` ↔ `uint64_t` overlap silently otherwise).
    [[nodiscard]]
    static
    expect<hd_private> parse_from_with_public_prefix(std::string_view encoded, uint32_t public_prefix);

    /// Parse a base58-encoded key with a caller-supplied prefix pair.
    [[nodiscard]]
    static
    expect<hd_private> parse_from_with_prefixes(std::string_view encoded, uint64_t prefixes);

    hd_private() = default;
    hd_private& operator=(hd_private x);

    explicit
    hd_private(data_chunk const& seed, uint64_t prefixes = mainnet);

    explicit
    hd_private(hd_key const& private_key);

    hd_private(hd_key const& private_key, uint64_t prefixes);
    hd_private(hd_key const& private_key, uint32_t prefix);

    [[nodiscard]]
    friend bool operator==(hd_private const&, hd_private const&) = default;

    [[nodiscard]]
    friend auto operator<=>(hd_private const& a, hd_private const& b) {
        return a.to_string() <=> b.to_string();
    }

    // Swap implementation required to properly handle base class.
    friend
    void swap(hd_private& left, hd_private& right);

    [[nodiscard]]
    ec_secret const& secret() const noexcept { return secret_; }

    /// Base58 encoding used by `fmt::formatter<hd_private>`.
    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]]
    hd_key to_hd_key() const;

    [[nodiscard]]
    hd_public to_public() const;

    [[nodiscard]]
    hd_private derive_private(uint32_t index) const;

    [[nodiscard]]
    hd_public derive_public(uint32_t index) const;

private:
    static hd_private from_seed(byte_span seed, uint64_t prefixes);
    static hd_private from_key(hd_key const& decoded);
    static hd_private from_key(hd_key const& key, uint32_t public_prefix);
    static hd_private from_key(hd_key const& key, uint64_t prefixes);

    hd_private(ec_secret const& secret, hd_chain_code const& chain_code, hd_lineage const& lineage);

    ec_secret secret_ {null_hash};
};

} // namespace kth::domain::wallet

#endif
