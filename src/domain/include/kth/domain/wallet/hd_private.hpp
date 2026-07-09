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
 * Derives from `hd_public`. Fallible construction goes through the
 * `parse_from`-family and `from_*` static factories, all returning
 * `expect<hd_private>`. Failure of the private `_impl` factories is
 * signalled internally by a default-constructed invalid instance
 * whose inherited `.valid()` returns false; the default ctor is kept
 * `private` so this sentinel only exists inside the class.
 *
 * Fully dropping the private default ctor is deferred to the
 * `hd_public` refactor PR — it would require zeroing the inherited
 * `hd_public` members directly, which crosses that class's
 * encapsulation boundary.
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

    /// Derive a key from a random seed under a caller-supplied
    /// prefix pair. Fallible on malformed / empty seed input.
    [[nodiscard]]
    static
    expect<hd_private> from_seed(data_chunk const& seed, uint64_t prefixes);

    /// Wrap an already-decoded 82-byte hd key using the default
    /// mainnet public prefix.
    [[nodiscard]]
    static
    expect<hd_private> from_hd_key(hd_key const& key);

    /// Wrap an already-decoded 82-byte hd key with a caller-supplied
    /// public-side prefix. Named distinctly from
    /// `from_hd_key_with_prefixes` so C++ integer promotion at the
    /// call site can never pick the wrong overload (`uint32_t` ↔
    /// `uint64_t` overlap silently otherwise).
    [[nodiscard]]
    static
    expect<hd_private> from_hd_key_with_public_prefix(hd_key const& key, uint32_t public_prefix);

    /// Wrap an already-decoded 82-byte hd key with a caller-supplied
    /// prefix pair.
    [[nodiscard]]
    static
    expect<hd_private> from_hd_key_with_prefixes(hd_key const& key, uint64_t prefixes);

    [[nodiscard]]
    friend auto operator<=>(hd_private const& a, hd_private const& b) = default;

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
    expect<hd_private> derive_private(uint32_t index) const;

    [[nodiscard]]
    expect<hd_public> derive_public(uint32_t index) const;

    /// Reset every field to the default zero state. Used by
    /// wallet_manager to overwrite the intermediate derivation keys
    /// once the leaf public key has been captured.
    void wipe() noexcept;

private:
    // Sentinel default ctor: internal-only. The private factories
    // below return an invalid instance built via this on failure,
    // which the inherited `.valid()` reports as false.
    hd_private() = default;

    static hd_private from_seed_impl(byte_span seed, uint64_t prefixes);
    static hd_private from_hd_key_impl(hd_key const& key, uint32_t public_prefix);
    static hd_private from_hd_key_impl(hd_key const& key, uint64_t prefixes);

    hd_private(ec_secret const& secret, hd_chain_code const& chain_code, hd_lineage const& lineage);

    ec_secret secret_ {null_hash};
};

} // namespace kth::domain::wallet

#endif
