// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_PAYMENT_ADDRESS_HPP
#define KTH_DOMAIN_WALLET_PAYMENT_ADDRESS_HPP

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
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

/// A class for working with non-stealth payment addresses.
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
#endif

    using list = std::vector<payment_address>;
    using ptr = std::shared_ptr<payment_address>;

    /// Constructors.
    payment_address() = default;

    explicit
    payment_address(payment const& decoded);

    explicit
    payment_address(ec_private const& secret);

    explicit
    payment_address(std::string const& address);

    explicit
    payment_address(short_hash const& hash, uint8_t version = mainnet_p2kh);

    explicit
    payment_address(hash_digest const& hash, uint8_t version = mainnet_p2kh);

    explicit
    payment_address(ec_public const& point, uint8_t version = mainnet_p2kh);

    explicit
    payment_address(chain::script const& script, uint8_t version = mainnet_p2sh);

    /// Factories
    static
    payment_address from_pay_public_key_hash_script(chain::script const& script, uint8_t version);

    /// Operators.
    bool operator==(payment_address const& x) const;
    bool operator!=(payment_address const& x) const;
    bool operator<(payment_address const& x) const;

    friend
    std::istream& operator>>(std::istream& in, payment_address& to);

    friend
    std::ostream& operator<<(std::ostream& out, payment_address const& of);

    /// Cast operators.
    operator bool() const;
    // operator short_hash const&() const;

    bool valid() const;

    /// Serializer.
    [[nodiscard]]
    std::string encoded_legacy() const;

#if defined(KTH_CURRENCY_BCH)
    [[nodiscard]]
    std::string encoded_cashaddr(bool token_aware) const;
#endif  //KTH_CURRENCY_BCH

    /// Accessors.
    [[nodiscard]]
    uint8_t version() const;

    [[nodiscard]]
    byte_span hash_span() const;

    [[nodiscard]]
    short_hash hash20() const;

    [[nodiscard]]
    hash_digest const& hash32() const;

    /// Methods.
    [[nodiscard]]
    payment to_payment() const;

    /// Extract a payment address list from an input or output script.
    static
    list extract(chain::script const& script, uint8_t p2kh_version = mainnet_p2kh, uint8_t p2sh_version = mainnet_p2sh);

    static
    list extract_input(chain::script const& script, uint8_t p2kh_version = mainnet_p2kh, uint8_t p2sh_version = mainnet_p2sh);

    static
    list extract_output(chain::script const& script, uint8_t p2kh_version = mainnet_p2kh, uint8_t p2sh_version = mainnet_p2sh);

private:
    /// Validators.
    static
    bool is_address(byte_span decoded);

    /// Factories.
    static
    payment_address from_string(std::string const& address);

#if defined(KTH_CURRENCY_BCH)
    static
    payment_address from_string_cashaddr(std::string const& address);
#endif  //KTH_CURRENCY_BCH

    static
    payment_address from_payment(payment const& decoded);

    static
    payment_address from_private(ec_private const& secret);

    static
    payment_address from_public(ec_public const& point, uint8_t version);

    static
    payment_address from_script(chain::script const& script, uint8_t version);

    bool valid_ = false;
    uint8_t version_ = 0;
    hash_digest hash_data_ = null_hash;
    size_t hash_size_ = 0;
};

/// The pre-encoded structure of a payment address or other similar data.
struct KD_API wrapped_data {
    uint8_t version;
    data_chunk payload;
    uint32_t checksum;
};

} // namespace kth::domain::wallet

// Allow payment_address to be in indexed in std::*map classes.
namespace std {
template <>
struct hash<kth::domain::wallet::payment_address> {
    size_t operator()(kth::domain::wallet::payment_address const& address) const {
        return std::hash<kth::byte_span>()(address.hash_span());
    }
};
} // namespace std

#endif
