// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_PAYMENT_ADDRESS_HPP
#define KTH_DOMAIN_WALLET_PAYMENT_ADDRESS_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/ec_private.hpp>
#include <kth/domain/wallet/ec_public.hpp>
#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

static constexpr
size_t payment_size = 1U + short_hash_size + checksum_size;  // 1 + 20 + sizeof(uint32_t) = 1 + 20 + 4 = 25

using payment = byte_array<payment_size>;

/**
 * Non-stealth payment address (P2PKH, P2SH, or 32-byte hash variants).
 *
 * Fallible construction goes through the `parse_from` / `from_*`
 * named factories, each returning `expect<payment_address>`. Callers
 * that need to hold a payment_address as a struct member before its
 * value is known should wrap it in `std::optional` — the type has no
 * default-constructed invalid state.
 */
struct KD_API payment_address {

#if defined(KTH_CURRENCY_LTC)
    static constexpr uint8_t mainnet_p2kh = 0x30;
#else
    static constexpr uint8_t mainnet_p2kh = 0x00;
#endif

    static constexpr uint8_t mainnet_p2sh = 0x05;
    static constexpr uint8_t testnet_p2kh = 0x6f;
    static constexpr uint8_t testnet_p2sh = 0xc4;

#if defined(KTH_CURRENCY_BCH)
    static constexpr std::string_view cashaddr_prefix_mainnet = "bitcoincash";
    static constexpr std::string_view cashaddr_prefix_testnet = "bchtest";
    static constexpr std::string_view cashaddr_prefix_regtest = "bchreg";

    static constexpr
    std::string_view cashaddr_prefix_for(config::network net) noexcept {
        switch (net) {
            case config::network::mainnet:  return cashaddr_prefix_mainnet;
            case config::network::regtest:  return cashaddr_prefix_regtest;
            default:                        return cashaddr_prefix_testnet;
        }
    }
#endif

    using list = std::vector<payment_address>;
    using ptr = std::shared_ptr<payment_address>;

    /// Parse a base58 legacy address (or cashaddr under BCH). Returns
    /// `error::illegal_value` on malformed input.
    [[nodiscard]]
    static
    expect<payment_address> parse_from(std::string_view address);

    /// Same, restricted to a specific network's cashaddr prefix (BCH).
    [[nodiscard]]
    static
    expect<payment_address> parse_from(std::string_view address, config::network net);

    /// Wrap a 25-byte wire payment (`<version><20-byte hash><checksum>`).
    /// Returns `error::illegal_value` if the checksum fails.
    [[nodiscard]]
    static
    expect<payment_address> from_payment(payment const& decoded);

    /// Derive from an EC private key.
    [[nodiscard]]
    static
    expect<payment_address> from_ec_private(ec_private const& secret);

    /// Wrap an EC public key with a version prefix.
    [[nodiscard]]
    static
    expect<payment_address> from_ec_public(ec_public const& point, uint8_t version);

    /// P2SH from any script (hash of the serialized script). Infallible.
    [[nodiscard]]
    static
    payment_address from_script(chain::script const& script, uint8_t version);

    /// Extract the pushed short_hash from a P2PKH-pattern script.
    /// Returns `error::illegal_value` if the script isn't P2PKH.
    [[nodiscard]]
    static
    expect<payment_address> from_pay_public_key_hash_script(chain::script const& script, uint8_t version);

    /// Wrap a 20-byte hash160 payload directly. Infallible.
    payment_address(short_hash const& short_hash, uint8_t version);

    /// Wrap a 32-byte hash payload directly. Infallible.
    payment_address(hash_digest const& hash, uint8_t version);

    [[nodiscard]]
    friend bool operator==(payment_address const&, payment_address const&) = default;

    /// Canonical order matches the canonical `to_string()`: cashaddr
    /// (token-unaware) under BCH, legacy base58 otherwise.
    [[nodiscard]]
    friend auto operator<=>(payment_address const& a, payment_address const& b) {
        return a.to_string() <=> b.to_string();
    }

    [[nodiscard]]
    bool valid() const noexcept { return valid_; }

    /// Canonical string form. Used by `fmt::formatter<payment_address>`.
    /// Cashaddr (token-unaware) under BCH; legacy base58 otherwise.
    [[nodiscard]]
    std::string to_string() const;

    /// Legacy base58 encoding (`<version><20-byte hash><checksum>`).
    /// Only valid for 20-byte addresses (P2KH, P2SH). Returns an
    /// empty string when the address carries a 32-byte hash
    /// (`pay_script_hash_32`, BCH 2025 Leibniz) — that hash size
    /// has no legacy representation; use `encoded_cashaddr()` /
    /// `encoded_token()` instead.
    [[nodiscard]]
    std::string encoded_legacy() const;

#if defined(KTH_CURRENCY_BCH)
    [[nodiscard]]
    std::string encoded_cashaddr(bool token_aware) const;

    [[nodiscard]]
    std::string encoded_token() const;
#endif  //KTH_CURRENCY_BCH

    [[nodiscard]]
    uint8_t version() const noexcept { return version_; }

    [[nodiscard]]
    byte_span hash_span() const;

    /// 20-byte hash accessor. Only valid for 20-byte addresses
    /// (P2KH, P2SH). Returns `null_short_hash` when the address
    /// carries a 32-byte hash — use `hash32()` for 32-byte payloads
    /// or `hash_span()` for the size-agnostic path.
    [[nodiscard]]
    short_hash hash20() const;

    [[nodiscard]]
    hash_digest const& hash32() const;

    /// 25-byte `<version><20-byte hash><checksum>` payment layout.
    /// Only valid for 20-byte addresses. Returns a zero-initialised
    /// `payment` when the address carries a 32-byte hash — the
    /// `payment` byte_array has no representation for that size.
    [[nodiscard]]
    payment to_payment() const;

    /// Extract a payment address list from an input or output script.
    [[nodiscard]]
    static
    list extract(chain::script const& script, uint8_t p2kh_version, uint8_t p2sh_version);

    [[nodiscard]]
    static
    list extract_input(chain::script const& script, uint8_t p2kh_version, uint8_t p2sh_version);

    [[nodiscard]]
    static
    list extract_output(chain::script const& script, uint8_t p2kh_version, uint8_t p2sh_version);

private:
    static
    bool is_address(byte_span decoded);

#if defined(KTH_CURRENCY_BCH)
    static
    expect<payment_address> from_string_cashaddr(std::string const& address, config::network net);

    static
    std::optional<config::network> detect_cashaddr_network(std::string const& address);
#endif  //KTH_CURRENCY_BCH

    bool valid_{false};
    uint8_t version_{0};
    hash_digest hash_data_{null_hash};
    size_t hash_size_{0};
};

/// The pre-encoded structure of a payment address or other similar data.
struct KD_API wrapped_data {
    uint8_t version;
    data_chunk payload;
    uint32_t checksum;
};

} // namespace kth::domain::wallet

// Allow payment_address to be indexed in std::*map classes.
namespace std {
template <>
struct hash<kth::domain::wallet::payment_address> {
    size_t operator()(kth::domain::wallet::payment_address const& address) const {
        return std::hash<kth::byte_span>()(address.hash_span());
    }
};
} // namespace std

#endif
