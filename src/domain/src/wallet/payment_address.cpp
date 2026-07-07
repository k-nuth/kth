// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/payment_address.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <kth/domain/wallet/ec_private.hpp>
#include <kth/domain/wallet/ec_public.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>

#if defined(KTH_CURRENCY_BCH)
#include <kth/domain/wallet/cashaddr.hpp>
#endif  //KTH_CURRENCY_BCH

using namespace kth::infrastructure::machine;
using namespace kth::domain::machine;

#if defined(KTH_CURRENCY_BCH)
using namespace kth::domain::wallet;
#endif  //KTH_CURRENCY_BCH

namespace kth::domain::wallet {

payment_address::payment_address(short_hash const& short_hash, uint8_t version)
    : version_(version)
    , hash_size_(short_hash.size())
{
    std::copy_n(short_hash.begin(), short_hash.size(), hash_data_.begin());
}

payment_address::payment_address(hash_digest const& hash, uint8_t version)
    : version_(version)
    , hash_data_(hash)
    , hash_size_(hash.size())
{}

// Validators.
// ----------------------------------------------------------------------------

bool payment_address::is_address(byte_span decoded) {
    return (decoded.size() == payment_size) && verify_checksum(decoded);
}

// CashAddr helpers (BCH-only).
// ----------------------------------------------------------------------------

//TODO(fernando): move BCH cashaddr to another place
#if defined(KTH_CURRENCY_BCH)

template <unsigned int frombits, unsigned int tobits, bool pad, typename O, typename I>
bool convert_bits(O& out, I it, I end) {
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

enum cash_addr_type : uint8_t {
    PUBKEY_TYPE = 0,
    SCRIPT_TYPE = 1,
    TOKEN_PUBKEY_TYPE = 2, // Token-Aware P2PKH
    TOKEN_SCRIPT_TYPE = 3, // Token-Aware P2SH
};

std::optional<config::network> payment_address::detect_cashaddr_network(std::string const& address) {
    auto const colon = address.find(':');
    if (colon == std::string::npos) return std::nullopt;
    auto prefix = address.substr(0, colon);
    // CashAddr spec allows all-uppercase addresses (e.g. BITCOINCASH:QP...).
    // Normalize to lowercase before comparing against the known prefixes.
    std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (prefix == cashaddr_prefix_mainnet) return config::network::mainnet;
    if (prefix == cashaddr_prefix_testnet) return config::network::testnet;
    if (prefix == cashaddr_prefix_regtest) return config::network::regtest;
    return std::nullopt;
}

expect<payment_address> payment_address::from_string_cashaddr(std::string const& address, config::network net) {
    auto const expected_prefix = cashaddr_prefix_for(net);
    auto const [prefix, payload] = cashaddr::decode(address, std::string(expected_prefix));

    if (prefix != expected_prefix) {
        return std::unexpected(kth::error::illegal_value);
    }

    if (payload.empty()) {
        return std::unexpected(kth::error::illegal_value);
    }

    // Check that the padding is zero.
    size_t extrabits = payload.size() * 5 % 8;
    if (extrabits >= 5) {
        return std::unexpected(kth::error::illegal_value);
    }

    uint8_t last = payload.back();
    uint8_t mask = (1U << extrabits) - 1;
    if ((last & mask) != 0) {
        return std::unexpected(kth::error::illegal_value);
    }

    data_chunk data;
    data.reserve(payload.size() * 5 / 8);
    convert_bits<5, 8, false>(data, std::begin(payload), std::end(payload));

    // Decode type and size from the version.
    uint8_t version = data[0];
    if ((version & 0x80) != 0) {
        // First bit is reserved.
        return std::unexpected(kth::error::illegal_value);
    }

    auto type = cash_addr_type((version >> 3U) & 0x1f);
    uint32_t hash_size = 20 + 4 * (version & 0x03);
    if ((version & 0x04) != 0) {
        hash_size *= 2;
    }

    // Check that we decoded the exact number of bytes we expected.
    if (data.size() != hash_size + 1) {
        return std::unexpected(kth::error::illegal_value);
    }

    if (data.size() == short_hash_size + 1) {
        short_hash hash;
        std::copy(std::begin(data) + 1, std::end(data), std::begin(hash));

        if (prefix == cashaddr_prefix_mainnet) {
            return payment_address{hash, type == PUBKEY_TYPE ? mainnet_p2kh : mainnet_p2sh};
        }
        return payment_address{hash, type == PUBKEY_TYPE ? testnet_p2kh : testnet_p2sh};
    }

    // 32-byte hash variant (BCH 2025 Leibniz `pay_script_hash_32`).
    hash_digest hash;
    std::copy(std::begin(data) + 1, std::end(data), std::begin(hash));

    if (prefix == cashaddr_prefix_mainnet) {
        return payment_address{hash, type == PUBKEY_TYPE ? mainnet_p2kh : mainnet_p2sh};
    }
    return payment_address{hash, type == PUBKEY_TYPE ? testnet_p2kh : testnet_p2sh};
}

#endif  //KTH_CURRENCY_BCH

// Named factories.
// ----------------------------------------------------------------------------

// static
expect<payment_address> payment_address::parse_from(std::string_view address) {
    auto const decoded = decode_base58<payment_size>(address);
    if (decoded && is_address(*decoded)) {
        return from_payment(*decoded);
    }
#if defined(KTH_CURRENCY_BCH)
    std::string const s{address};
    auto const net = detect_cashaddr_network(s);
    if (net) {
        return from_string_cashaddr(s, *net);
    }
#endif
    return std::unexpected(kth::error::illegal_value);
}

// static
expect<payment_address> payment_address::parse_from(std::string_view address, config::network net) {
    auto const decoded = decode_base58<payment_size>(address);
    if (decoded && is_address(*decoded)) {
        return from_payment(*decoded);
    }
#if defined(KTH_CURRENCY_BCH)
    return from_string_cashaddr(std::string{address}, net);
#else
    (void)net;
    return std::unexpected(kth::error::illegal_value);
#endif
}

// static
expect<payment_address> payment_address::from_payment(payment const& decoded) {
    if ( ! is_address(decoded)) {
        return std::unexpected(kth::error::illegal_value);
    }
    auto const hash = slice<1, short_hash_size + 1>(decoded);
    return payment_address{hash, decoded.front()};
}

// static
expect<payment_address> payment_address::from_ec_private(ec_private const& secret) {
    return from_ec_public(secret.to_public(), secret.payment_version());
}

// static
expect<payment_address> payment_address::from_ec_public(ec_public const& point, uint8_t version) {
    return payment_address{bitcoin_short_hash(point.to_data()), version};
}

// static
payment_address payment_address::from_script(chain::script const& script, uint8_t version) {
    return payment_address{bitcoin_short_hash(kth::to_data_chunk(script, false)), version};
}

// static
expect<payment_address> payment_address::from_pay_public_key_hash_script(chain::script const& script, uint8_t version) {
    auto const ops = script.operations();
    if ( ! chain::script::is_pay_public_key_hash_pattern(ops)) {
        return std::unexpected(kth::error::illegal_value);
    }
    short_hash hash;
    std::copy(ops[2].data().begin(), ops[2].data().begin() + short_hash_size, hash.begin());
    return payment_address{hash, version};
}

// Serializer.
// ----------------------------------------------------------------------------

std::string payment_address::encoded_legacy() const {
    // Legacy base58 wraps a 20-byte hash; the format has no
    // representation for the 32-byte hashes that ship under BCH's
    // `pay_script_hash_32` pattern (2025 Leibniz). Calling this on
    // such an address would silently truncate to the first 20
    // bytes — surface that as an empty string sentinel so callers
    // can detect "no legacy form available" instead of acting on
    // wrong-but-plausible output.
    //
    // Default-constructed / invalid addresses (`hash_size_ == 0`)
    // fall through deliberately: the resulting "1111…oLvT2"
    // base58-of-zeros sentinel has been the documented "this is
    // an uninitialised address" marker for years.
    if (hash_size_ > short_hash_size) {
        return {};
    }
    return encode_base58(wrap(version_, hash20()));
}

std::string payment_address::to_string() const {
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

namespace {

// Convert the data part to a 5 bit representation.
template <typename T>
data_chunk pack_addr_data_(T const& id, uint8_t type) {
    uint8_t version_byte(type << 3U);
    size_t size = id.size();
    uint8_t encoded_size = 0;

    switch (size * 8) {
        case 160: encoded_size = 0; break;
        case 192: encoded_size = 1; break;
        case 224: encoded_size = 2; break;
        case 256: encoded_size = 3; break;
        case 320: encoded_size = 4; break;
        case 384: encoded_size = 5; break;
        case 448: encoded_size = 6; break;
        case 512: encoded_size = 7; break;
        default:
            throw std::runtime_error("Error packing cashaddr: invalid address length");
    }

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

std::string encode_cashaddr_(payment_address const& addr, bool token_aware) {
    auto const p2kh_type = token_aware ? TOKEN_PUBKEY_TYPE : PUBKEY_TYPE;
    auto const p2sh_type = token_aware ? TOKEN_SCRIPT_TYPE : SCRIPT_TYPE;

    if (addr.version() == payment_address::mainnet_p2kh || addr.version() == payment_address::mainnet_p2sh) {
        auto const type = addr.version() == payment_address::mainnet_p2kh ? p2kh_type : p2sh_type;
        return cashaddr::encode(payment_address::cashaddr_prefix_mainnet,
                                pack_addr_data_(addr.hash_span(), type));
    }
    if (addr.version() == payment_address::testnet_p2kh || addr.version() == payment_address::testnet_p2sh) {
        auto const type = addr.version() == payment_address::testnet_p2kh ? p2kh_type : p2sh_type;
        return cashaddr::encode(payment_address::cashaddr_prefix_testnet,
                                pack_addr_data_(addr.hash_span(), type));
    }
    return "";
}

} // anonymous namespace

std::string payment_address::encoded_cashaddr(bool token_aware) const {
    return encode_cashaddr_(*this, token_aware);
}

std::string payment_address::encoded_token() const {
    return encode_cashaddr_(*this, true);
}

#endif  //KTH_CURRENCY_BCH

// Accessors.
// ----------------------------------------------------------------------------

kth::byte_span payment_address::hash_span() const {
    return {hash_data_.begin(), hash_size_};
}

short_hash payment_address::hash20() const {
    // `pay_script_hash_32` (BCH 2025 Leibniz) addresses store a
    // 32-byte hash that doesn't fit in a `short_hash`. Returning
    // the first 20 bytes would be silent truncation. Surface that
    // as the zero sentinel so callers can detect "no 20-byte hash
    // available".
    if (hash_size_ > short_hash_size) {
        return null_short_hash;
    }
    short_hash hash;
    std::copy_n(hash_data_.begin(), hash.size(), hash.begin());
    return hash;
}

hash_digest const& payment_address::hash32() const {
    return hash_data_;
}

payment payment_address::to_payment() const {
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

// Static extraction.
// ----------------------------------------------------------------------------

// Context free input extraction is provably ambiguous (see extract_input).
payment_address::list payment_address::extract(chain::script const& script, uint8_t p2kh_version, uint8_t p2sh_version) {
    auto const input = extract_input(script, p2kh_version, p2sh_version);
    return input.empty() ? extract_output(script, p2kh_version, p2sh_version) : input;
}

// Context free input extraction is provably ambiguous. See inline comments.
payment_address::list payment_address::extract_input(chain::script const& script, uint8_t p2kh_version, uint8_t p2sh_version) {
    auto const pattern = script.input_pattern();
    auto const ops = script.operations();

    switch (pattern) {
        // Given lack of context (prevout) sign_public_key_hash is always
        // ambiguous with sign_script_hash, so return both potentially-
        // correct addresses. A server can differentiate by extracting
        // from the previous output.
        case script_pattern::sign_public_key_hash: {
            auto const pub = ec_public::from_data(ops[1].data());
            if ( ! pub) {
                return {
                    payment_address{bitcoin_short_hash(ops.back().data()), p2sh_version}
                };
            }
            auto pa = from_ec_public(*pub, p2kh_version);
            if ( ! pa) {
                return {
                    payment_address{bitcoin_short_hash(ops.back().data()), p2sh_version}
                };
            }
            return {
                *pa,
                payment_address{bitcoin_short_hash(ops.back().data()), p2sh_version}
            };
        }
        case script_pattern::sign_script_hash: {
            return {
                payment_address{bitcoin_short_hash(ops.back().data()), p2sh_version}
            };
        }

        // There is no address in sign_public_key script (signature only)
        // and the public key cannot be extracted from the signature.
        case script_pattern::sign_public_key:
        // There are no addresses in sign_multisig script, signatures only.
        case script_pattern::sign_multisig:
        case script_pattern::non_standard:
        default:
            return {};
    }
}

// A server should use this against the prevout instead of using extract_input.
payment_address::list payment_address::extract_output(chain::script const& script, uint8_t p2kh_version, uint8_t p2sh_version) {
    auto const pattern = script.output_pattern();
    auto const ops = script.operations();

    switch (pattern) {
        case script_pattern::pay_to_public_key_hash: {
            return {
                payment_address{to_array<short_hash_size>(ops[2].data()), p2kh_version}
            };
        }
        case script_pattern::pay_to_script_hash: {
            return {
                payment_address{to_array<short_hash_size>(ops[1].data()), p2sh_version}
            };
        }
        case script_pattern::pay_to_script_hash_32: {
            return {
                payment_address{to_array<hash_size>(ops[1].data()), p2sh_version}
            };
        }
        case script_pattern::pay_to_public_key: {
            auto const pub = ec_public::from_data(ops[0].data());
            if ( ! pub) {
                return {};
            }
            // pay_to_public_key is not p2kh but we conflate for tracking.
            auto pa = from_ec_public(*pub, p2kh_version);
            if ( ! pa) {
                return {};
            }
            return { *pa };
        }

        // Bare multisig, null data and pay-to-script (BCH 2026-May leibniz)
        // do not associate a payment address. BCHN's `TX_SCRIPT` branch in
        // `ExtractDestination` returns false for the same reason: a raw
        // scriptPubKey has no hash / key to derive an address from.
        case script_pattern::pay_to_multisig:
        case script_pattern::null_data:
        case script_pattern::pay_to_script:
        case script_pattern::non_standard:
        default:
            return {};
    }
}

} // namespace kth::domain::wallet
