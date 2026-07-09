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

/// BIP32 extended public key. Valid-by-construction: every reachable
/// instance was produced by a factory that validated its inputs, so
/// accessors and serializers are always meaningful.
struct KD_API hd_public {
    friend class hd_private;

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

    /// Wrap an already-decoded 82-byte hd key, reading its version
    /// prefix straight off the wire.
    [[nodiscard]]
    static
    expect<hd_public> from_hd_key(hd_key const& key);

    /// Wrap an already-decoded 82-byte hd key against a caller-
    /// supplied version prefix. Mismatched prefix returns an error.
    [[nodiscard]]
    static
    expect<hd_public> from_hd_key_with_prefix(hd_key const& key, uint32_t prefix);

    [[nodiscard]]
    friend auto operator<=>(hd_public const& a, hd_public const& b) = default;

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
    expect<hd_public> derive_public(uint32_t index) const;

    /// Overwrite every field with zero for security-sensitive
    /// teardown (before scope exit). After `wipe()` the accessors
    /// return well-formed but semantically meaningless data; further
    /// use of the object is a caller error.
    void wipe() noexcept;

protected:
    hd_public(ec_compressed const& point, hd_chain_code const& chain_code, hd_lineage const& lineage);

    static
    expect<hd_public> from_secret(ec_secret const& secret, hd_chain_code const& chain_code, hd_lineage const& lineage);

    uint32_t fingerprint() const;

    hd_chain_code chain_;
    hd_lineage lineage_;
    ec_compressed point_;

private:
    static
    expect<hd_public> from_key_impl(hd_key const& key);

    static
    expect<hd_public> from_key_impl(hd_key const& key, uint32_t prefix);
};

} // namespace kth::domain::wallet

#endif
