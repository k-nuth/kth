// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_HD_PRIVATE_KEY_HPP
#define KTH_DOMAIN_WALLET_HD_PRIVATE_KEY_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

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

/// BIP32 extended private key. Valid-by-construction: every reachable
/// instance was produced by a factory that validated its inputs.
///
/// Holds an `hd_public` by composition (not inheritance); the base
/// hd_public carries the derived `point`, `chain_code`, and packed
/// `lineage` (private-side prefix in the high 32 bits, public-side
/// in the low 32 bits — see `to_public()`).
struct KD_API hd_private {
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

    /// Derive a master key from a seed against a caller-supplied
    /// prefix pair (packed as `to_prefixes(private, public)`).
    [[nodiscard]]
    static
    expect<hd_private> from_seed(data_chunk const& seed, uint64_t prefixes);

    /// Wrap an already-decoded 82-byte hd key using the default
    /// mainnet public prefix. Parallel to `parse_from(string_view)`.
    [[nodiscard]]
    static
    expect<hd_private> from_hd_key(hd_key const& private_key);

    /// Wrap an already-decoded 82-byte hd key, reading its private
    /// prefix off the wire and pairing it with a caller-supplied
    /// public prefix. Named distinctly from `from_hd_key_with_prefixes`
    /// so the `uint32_t` / `uint64_t` overloads can't collide via
    /// implicit promotion at the call site.
    [[nodiscard]]
    static
    expect<hd_private> from_hd_key_with_public_prefix(hd_key const& private_key, uint32_t public_prefix);

    /// Wrap an already-decoded 82-byte hd key against a caller-
    /// supplied prefix pair.
    [[nodiscard]]
    static
    expect<hd_private> from_hd_key_with_prefixes(hd_key const& private_key, uint64_t prefixes);

    /// Package a matching `hd_public` / `ec_secret` pair into an
    /// `hd_private`. Caller is responsible for the "matches" invariant
    /// (i.e. `public_.point()` derives from `secret`); no on-curve or
    /// consistency check is performed here. Mirrors
    /// `hd_public::from_verified_components`.
    [[nodiscard]] static constexpr
    hd_private from_verified_parts(hd_public base, ec_secret const& secret) noexcept {
        return hd_private(std::move(base), secret);
    }

    [[nodiscard]]
    friend auto operator<=>(hd_private const&, hd_private const&) = default;

    [[nodiscard]] constexpr
    ec_secret const& secret() const noexcept { return secret_; }

    /// Read-only view of the composed public-side key. Note that its
    /// `lineage().prefixes` still carries the private+public pair
    /// packed together; use `to_public()` to obtain a peer with the
    /// prefix rewritten to the public-only view.
    [[nodiscard]] constexpr
    hd_public const& public_key() const noexcept { return public_; }

    [[nodiscard]] constexpr
    hd_chain_code const& chain_code() const noexcept { return public_.chain_code(); }

    [[nodiscard]] constexpr
    hd_lineage const& lineage() const noexcept { return public_.lineage(); }

    [[nodiscard]] constexpr
    ec_compressed const& point() const noexcept { return public_.point(); }

    /// Base58 encoding used by `fmt::formatter<hd_private>`.
    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]]
    hd_key to_hd_key() const;

    /// Peer `hd_public` — same point / chain / depth / fingerprint,
    /// with the packed lineage prefix collapsed to just the public
    /// half. Infallible.
    [[nodiscard]]
    hd_public to_public() const;

    [[nodiscard]]
    expect<hd_private> derive_private(uint32_t index) const;

    [[nodiscard]]
    expect<hd_public> derive_public(uint32_t index) const;

    /// Overwrite the composed public part and the private `secret_`
    /// with zero for security-sensitive teardown. Wraps
    /// `hd_public::wipe()`.
    void wipe() noexcept;

private:
    static expect<hd_private> from_seed_impl(byte_span seed, uint64_t prefixes);
    static expect<hd_private> from_key_impl(hd_key const& key, uint32_t public_prefix);
    static expect<hd_private> from_key_impl(hd_key const& key, uint64_t prefixes);

    static expect<hd_private> from_verified_secret(ec_secret const& secret, hd_chain_code const& chain_code, hd_lineage const& lineage);

    constexpr
    hd_private(hd_public base, ec_secret const& secret) noexcept
        : public_(std::move(base)), secret_(secret) {}

    hd_public public_;
    ec_secret secret_ {null_hash};
};

} // namespace kth::domain::wallet

#endif
