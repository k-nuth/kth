// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef KTH_DOMAIN_IMPL_WALLET_PAYMENT_ADDRESS_IPP
#define KTH_DOMAIN_IMPL_WALLET_PAYMENT_ADDRESS_IPP
#include <kth/infrastructure/formats/base_58.hpp>
#if defined(KTH_CURRENCY_BCH)
#include <kth/domain/wallet/cashaddr.hpp>
#endif
namespace kth::domain::wallet {

constexpr bool payment_address::is_address(byte_span decoded) noexcept {
    return (decoded.size() == payment_size) && verify_checksum(decoded);
}
// CashAddr helpers (BCH-only).
// ----------------------------------------------------------------------------

#if defined(KTH_CURRENCY_BCH)

namespace payment_address_detail {

[[nodiscard]] constexpr size_t find_cashaddr_separator(std::string_view address) noexcept {
    if consteval {
        for (size_t i = 0; i < address.size(); ++i) if (address[i] == ':') return i;
        return std::string_view::npos;
    } else {
        return address.find(':');
    }
}

template <unsigned int frombits, unsigned int tobits, bool pad, typename O, typename I>
constexpr bool convert_bits(O& out, I it, I end) {
    size_t acc = 0;
    size_t bits = 0;
    constexpr size_t maxv = (1U << tobits) - 1;
    constexpr size_t max_acc = (1U << (frombits + tobits - 1)) - 1;
    while (it != end) {
        acc = ((acc << frombits) | *it) & max_acc;
        bits += frombits;
        while (bits >= tobits) {
            bits -= tobits;
            out.push_back((acc >> bits) & maxv);
        }
        ++it;
    }

    // We have remaining bits to encode but do not pad.
    if ( ! pad && bits) {
        return false;
    }

    // We have remaining bits to encode so we do pad.
    if (pad && bits) {
        out.push_back((acc << (tobits - bits)) & maxv);
    }

    return true;
}

enum class cashaddr_destination_type : uint8_t {
    pubkey_hash = 0,
    script_hash = 1,
    token_pubkey_hash = 2, // Token-Aware P2PKH
    token_script_hash = 3, // Token-Aware P2SH
};

} // namespace payment_address_detail

constexpr std::optional<config::network> payment_address::detect_cashaddr_network(std::string_view address) noexcept {
    auto const colon = payment_address_detail::find_cashaddr_separator(address);
    if (colon == std::string_view::npos) return std::nullopt;
    auto const prefix = address.substr(0, colon);
    auto const matches = [prefix](std::string_view expected) {
        if (prefix.size() != expected.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) {
            auto const c = prefix[i];
            auto const lower = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
            if (lower != expected[i]) return false;
        }
        return true;
    };
    if (matches(cashaddr_prefix_mainnet)) return config::network::mainnet;
    if (matches(cashaddr_prefix_testnet)) return config::network::testnet;
    if (matches(cashaddr_prefix_regtest)) return config::network::regtest;
    return std::nullopt;
}

constexpr std::expected<payment_address, kth::error::error_code_t> payment_address::parse_cashaddr(std::string_view address, config::network net) {
    auto const expected_prefix = cashaddr_prefix_for(net);
    auto const decoded = cashaddr::detail::decode_format(address, expected_prefix);
    if (!decoded) return std::unexpected(decoded.error());
    auto const& [prefix, payload] = *decoded;

    if (prefix != expected_prefix) {
        return std::unexpected(kth::error::address_network_mismatch);
    }

    if (payload.empty()) {
        return std::unexpected(kth::error::address_invalid_hash_length);
    }

    // Check that the padding is zero.
    size_t extrabits = payload.size() * 5 % 8;
    if (extrabits >= 5) {
        return std::unexpected(kth::error::address_invalid_padding);
    }

    uint8_t last = payload.back();
    uint8_t mask = (1U << extrabits) - 1;
    if ((last & mask) != 0) {
        return std::unexpected(kth::error::address_invalid_padding);
    }

    using namespace payment_address_detail;
    data_chunk data;
    data.reserve(payload.size() * 5 / 8);
    convert_bits<5, 8, false>(data, std::begin(payload), std::end(payload));

    // Decode type and size from the version.
    uint8_t version = data[0];
    if ((version & 0x80) != 0) {
        // First bit is reserved.
        return std::unexpected(kth::error::address_reserved_version);
    }

    // A token-aware address carries the same P2PKH or P2SH payload as its
    // plain counterpart; the type only signals that the receiver accepts
    // CashTokens. Any other type is not an address kind this decodes.
    bool is_script = false;
    switch (static_cast<cashaddr_destination_type>((version >> 3U) & 0x1f)) {
        case cashaddr_destination_type::pubkey_hash:
        case cashaddr_destination_type::token_pubkey_hash:
            break;
        case cashaddr_destination_type::script_hash:
        case cashaddr_destination_type::token_script_hash:
            is_script = true;
            break;
        default:
            return std::unexpected(kth::error::address_unsupported_type);
    }

    uint32_t hash_size = 20 + 4 * (version & 0x03);
    if ((version & 0x04) != 0) {
        hash_size *= 2;
    }

    // Check that we decoded the exact number of bytes we expected.
    if (data.size() != hash_size + 1) {
        return std::unexpected(kth::error::address_invalid_hash_length);
    }

    // P2PKH requires a 20-byte hash. P2SH also supports 32-byte hashes;
    // reject every other size before copying into the fixed-size storage.
    if (hash_size != short_hash_size &&
        (!is_script || hash_size != sizeof(hash_digest))) {
        return std::unexpected(kth::error::address_unsupported_hash_size);
    }

    if (data.size() == short_hash_size + 1) {
        short_hash hash;
        std::copy(std::begin(data) + 1, std::end(data), std::begin(hash));

        if (prefix == cashaddr_prefix_mainnet) {
            return payment_address{hash, is_script ? mainnet_p2sh : mainnet_p2kh};
        }
        return payment_address{hash, is_script ? testnet_p2sh : testnet_p2kh};
    }

    // 32-byte hash variant (BCH 2025 Leibniz `pay_script_hash_32`).
    hash_digest hash;
    std::copy(std::begin(data) + 1, std::end(data), std::begin(hash));

    if (prefix == cashaddr_prefix_mainnet) {
        return payment_address{hash, is_script ? mainnet_p2sh : mainnet_p2kh};
    }
    return payment_address{hash, is_script ? testnet_p2sh : testnet_p2kh};
}

#endif  //KTH_CURRENCY_BCH

// static
constexpr expect<payment_address> payment_address::parse_from(std::string_view address) {
    if (address.empty()) return kth::fail(kth::error::address_empty_input);
    auto const decoded = decode_base58<payment_size>(address);
    if (decoded) {
        return from_payment(*decoded);
    }
#if defined(KTH_CURRENCY_BCH)
    auto const net = detect_cashaddr_network(address);
    if (net) {
        auto parsed = parse_cashaddr(address, *net);
        if (!parsed) return kth::fail(parsed.error());
        return std::move(*parsed);
    }
#endif
    if (address.find(':') != std::string_view::npos) {
        return kth::fail(kth::error::address_invalid_prefix);
    }
    return kth::fail(decoded.error() == base58_errc::invalid_character
        ? kth::error::address_invalid_character : kth::error::address_invalid_base58_length);
}

// static
constexpr expect<payment_address> payment_address::parse_from(std::string_view address, config::network net) {
    if (address.empty()) return kth::fail(kth::error::address_empty_input);
    auto const decoded = decode_base58<payment_size>(address);
    if (decoded) {
        return from_payment(*decoded);
    }
#if defined(KTH_CURRENCY_BCH)
    auto parsed = parse_cashaddr(address, net);
    if (!parsed) return kth::fail(parsed.error());
    return std::move(*parsed);
#else
    (void)net;
    if (address.find(':') != std::string_view::npos) {
        return kth::fail(kth::error::address_invalid_prefix);
    }
    return kth::fail(decoded.error() == base58_errc::invalid_character
        ? kth::error::address_invalid_character : kth::error::address_invalid_base58_length);
#endif
}

// Checked component factories are the only entry to the private constructors.
constexpr expect<payment_address> payment_address::from_shorthash(short_hash const& hash, uint8_t version) noexcept {
    if (!is_supported_version(version)) return kth::fail(kth::error::address_unsupported_type);
    return payment_address{hash, version};
}

constexpr expect<payment_address> payment_address::from_hash(hash_digest const& hash, uint8_t version) noexcept {
    if (!is_supported_version(version)) return kth::fail(kth::error::address_unsupported_type);
    if (version != mainnet_p2sh && version != testnet_p2sh)
        return kth::fail(kth::error::address_unsupported_hash_size);
    return payment_address{hash, version};
}

// Serializer.
// ----------------------------------------------------------------------------

constexpr std::string payment_address::encoded_legacy() const {
    // Legacy base58 wraps a 20-byte hash; the format has no
    // representation for the 32-byte hashes that ship under BCH's
    // `pay_script_hash_32` pattern (2025 Leibniz). Calling this on
    // such an address would silently truncate to the first 20
    // bytes — surface that as an empty string sentinel so callers
    // can detect "no legacy form available" instead of acting on
    // wrong-but-plausible output.
    if (hash_size_ > short_hash_size) {
        return {};
    }
    return encode_base58(wrap(version_, hash20()));
}

constexpr std::string payment_address::to_string() const {
#if defined(KTH_CURRENCY_BCH)
    // Under BCH, CashAddr is the canonical wire form. token-unaware
    // matches the modern default; callers wanting the token-aware
    // variant call `encoded_token()` (or `encoded_cashaddr(true)`)
    // explicitly.
    return encoded_cashaddr(false);
#else
    return encoded_legacy();
#endif
}

#if defined(KTH_CURRENCY_BCH)

namespace payment_address_detail {

// Build the CashAddr data payload (without prefix or checksum): prepend the
// version byte, then convert that byte and the hash to padded 5-bit values.
//precondition: type <= cashaddr_destination_type::token_script_hash
//precondition: id.size() == short_hash_size || id.size() == hash_size
//precondition: id.size() == short_hash_size || type == cashaddr_destination_type::script_hash || type == cashaddr_destination_type::token_script_hash
// The caller owns a validated payment_address, so only HASH160 and P2SH32
// reach this helper. Size codes 0 and 3 represent 160 and 256 bits.
constexpr data_chunk pack_addr_data_(byte_span id, cashaddr_destination_type type) {
    uint8_t version_byte(static_cast<uint8_t>(type) << 3U);
    size_t const size = id.size();
    uint8_t const encoded_size = size == short_hash_size ? 0 : 3;

    version_byte |= encoded_size;
    data_chunk data = {version_byte};
    data.insert(data.end(), std::begin(id), std::end(id));

    data_chunk converted;
    // Reserve the number of bytes required for a 5-bit packed version of a
    // hash, with version byte.  Add half a byte(4) so integer math provides
    // the next multiple-of-5 that would fit all the data.
    converted.reserve(((size + 1) * 8 + 4) / 5);
    convert_bits<8, 5, true>(converted, std::begin(data), std::end(data));

    return converted;
}

constexpr std::string encode_cashaddr_(payment_address const& addr, std::string_view prefix, bool token_aware) {
    bool const is_script = addr.version() == payment_address::mainnet_p2sh ||
                           addr.version() == payment_address::testnet_p2sh;
    auto const type = is_script
        ? (token_aware ? cashaddr_destination_type::token_script_hash : cashaddr_destination_type::script_hash)
        : (token_aware ? cashaddr_destination_type::token_pubkey_hash : cashaddr_destination_type::pubkey_hash);
    auto const payload = pack_addr_data_(addr.hash_span(), type);
    return cashaddr::encode(prefix, payload);
}

} // namespace payment_address_detail

constexpr std::string payment_address::encoded_cashaddr(bool token_aware) const {
    bool const mainnet = version_ == mainnet_p2kh || version_ == mainnet_p2sh;
    return encoded_cashaddr(mainnet ? config::network::mainnet : config::network::testnet, token_aware);
}

constexpr std::string payment_address::encoded_cashaddr(config::network net, bool token_aware) const {
    return payment_address_detail::encode_cashaddr_(*this, cashaddr_prefix_for(net), token_aware);
}

constexpr std::string payment_address::encoded_token() const {
    return encoded_cashaddr(true);
}

constexpr std::string payment_address::encoded_token(config::network net) const {
    return encoded_cashaddr(net, true);
}


template <fixed_string Text, bool TokenAware>
consteval payment_address payment_address::from_literal() noexcept {
    constexpr std::string_view text{Text.content, Text.size()};
    constexpr auto net = detect_cashaddr_network(text);
    static_assert(net.has_value(), "address literal requires bitcoincash:, bchtest:, or bchreg: prefix");
    if constexpr (net.has_value()) {
        constexpr auto parsed = parse_cashaddr(text, *net);
        static_assert(parsed.has_value(), "invalid CashAddr address literal");
        if constexpr (parsed.has_value()) {
            constexpr auto separator = payment_address_detail::find_cashaddr_separator(text);
            // Full address validation above guarantees a nonempty valid payload.
            // The first 5-bit symbol is the address type (0..3).
            constexpr auto type = cashaddr::detail::CHARSET_REV[uint8_t(Text.content[separator + 1])];
            static_assert(bool(type & 0x02) == TokenAware,
                "address literal suffix does not match its normal/token-aware type");
            return *parsed;
        }
    }
}

template <fixed_string Text>
[[nodiscard]] consteval payment_address operator""_cashaddr() noexcept {
    return payment_address::from_literal<Text, false>();
}

template <fixed_string Text>
[[nodiscard]] consteval payment_address operator""_tokenaddr() noexcept {
    return payment_address::from_literal<Text, true>();
}

#endif  //KTH_CURRENCY_BCH

// Accessors.
// ----------------------------------------------------------------------------

constexpr payment payment_address::to_payment() const {
    // `payment` is the fixed 25-byte (`version` + 20-byte hash +
    // 4-byte checksum) layout. There's no representation for a
    // 32-byte hash — surface as the zero sentinel so callers can
    // detect "no `payment` representation" and route through
    // CashAddr instead.
    if (hash_size_ > short_hash_size) {
        return payment{};
    }
    return wrap(version_, hash20());
}


} // namespace kth::domain::wallet
#endif
