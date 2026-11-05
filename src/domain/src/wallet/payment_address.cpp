// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/payment_address.hpp>

#include <algorithm>
#include <cstdint>
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

// Validators.
// ----------------------------------------------------------------------------


// Named factories.
// ----------------------------------------------------------------------------

// static
expect<payment_address> payment_address::from_payment(payment const& decoded) noexcept {
    // The size guard is constant-evaluable without invoking the runtime hash.
    static_assert(!is_address(byte_span{}));
    static_assert([] { byte_array<payment_size - 1> bytes{}; return !is_address(bytes); }());
    static_assert([] { byte_array<payment_size + 1> bytes{}; return !is_address(bytes); }());
    if ( ! is_address(decoded)) {
        return kth::fail(kth::error::address_invalid_checksum);
    }
    auto const hash = slice<1, short_hash_size + 1>(decoded);
    return from_shorthash(hash, decoded.front());
}

// static
expect<payment_address> payment_address::from_ec_private(ec_private const& secret) {
    return from_ec_public(secret.to_public(), secret.payment_version());
}

// static
expect<payment_address> payment_address::from_ec_public(ec_public const& point, uint8_t version) {
    return from_shorthash(bitcoin_short_hash(point.to_data()), version);
}

// static
expect<payment_address> payment_address::from_script(chain::script const& script, uint8_t version) {
    return from_shorthash(bitcoin_short_hash(kth::to_data_chunk(script, false)), version);
}

// static
expect<payment_address> payment_address::from_pay_public_key_hash_script(chain::script const& script, uint8_t version) {
    auto const ops = script.operations();
    if ( ! chain::script::is_pay_public_key_hash_pattern(ops)) {
        return kth::fail(kth::error::address_invalid_script);
    }
    short_hash hash;
    std::copy(ops[2].data().begin(), ops[2].data().begin() + short_hash_size, hash.begin());
    return from_shorthash(hash, version);
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
            list addresses;
            auto const pub = ec_public::from_data(ops[1].data());
            if (pub) {
                auto pa = from_ec_public(*pub, p2kh_version);
                if (pa) addresses.push_back(std::move(*pa));
            }
            auto pa = from_shorthash(bitcoin_short_hash(ops.back().data()), p2sh_version);
            if (pa) addresses.push_back(std::move(*pa));
            return addresses;
        }
        case script_pattern::sign_script_hash: {
            auto pa = from_shorthash(bitcoin_short_hash(ops.back().data()), p2sh_version);
            if (!pa) return {};
            return {*pa};
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
            auto pa = from_shorthash(to_array<short_hash_size>(ops[2].data()), p2kh_version);
            if (!pa) return {};
            return {*pa};
        }
        case script_pattern::pay_to_script_hash: {
            auto pa = from_shorthash(to_array<short_hash_size>(ops[1].data()), p2sh_version);
            if (!pa) return {};
            return {*pa};
        }
        case script_pattern::pay_to_script_hash_32: {
            auto pa = from_hash(to_array<hash_size>(ops[1].data()), p2sh_version);
            if (!pa) return {};
            return {*pa};
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
