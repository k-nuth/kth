// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Cashaddr is an address format inspired by bech32.
#ifndef KTH_DOMAIN_WALLET_CASHADDR_HPP_
#define KTH_DOMAIN_WALLET_CASHADDR_HPP_

#include <cstdint>
#include <kth/infrastructure/expect.hpp>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet::cashaddr {

/// Decode and validate the text format and checksum, without parsing an address.
/// @param str CashAddr text, with an optional `prefix:`. All-lowercase or
/// all-uppercase input is accepted; mixed case is rejected.
/// @param default_prefix Used unchanged only when str has no explicit prefix.
/// An explicit prefix wins: this is a fallback, not a network restriction.
/// @return On success, {prefix, payload}: prefix is a lowercase string when
/// supplied explicitly (without `:`); payload contains 5-bit values in [0, 31],
/// with the eight checksum symbols removed. It is NOT the raw address hash:
/// address version/type/size and hash data remain packed into these 5-bit groups.
/// Does not validate the address type, hash size, padding, or requested network;
/// use payment_address::parse_from for address-level validation.
/// @return On failure, an address_invalid_character, address_invalid_prefix,
/// address_mixed_case, or address_invalid_checksum error from Knuth's category.
/// Successful results can be constant-evaluated. Errors use std::error_code,
/// which is not constant-evaluable in C++23.
/// Allocates returned strings/vectors; allocation failures may throw.
[[nodiscard]] constexpr expect<std::pair<std::string, data_chunk>>
decode_checked(std::string_view str, std::string_view default_prefix);

/// Encode 5-bit payload symbols as `prefix:data+checksum`.
/// The checksum covers both prefix and payload and is computed here.
/// This does not pack a raw hash or validate an address's semantic fields.
/// @pre prefix is a nonempty lowercase ASCII-letter prefix.
/// @pre Every payload element is in [0, 31]; payload excludes checksum symbols.
/// @return The full CashAddr text, including prefix, separator and checksum.
/// Allocates the result and temporary buffers; allocation failures may throw.
[[nodiscard]] constexpr std::string encode(std::string_view prefix, data_chunk const& payload);

/// Compatibility decoder with the same input and successful result as
/// decode_checked: {prefix without `:`, 5-bit payload without checksum}.
/// Returns {empty string, empty vector} for any format/checksum error and
/// discards its cause. A successful decode can have an empty payload; an empty
/// payload alone is not a failure indicator (nor is it a valid payment address).
/// Only decode_checked distinguishes success from failure without ambiguity.
/// Prefer decode_checked when the caller needs the reason for a failure.
/// Both valid and invalid input can be evaluated at compile time.
/// Allocates returned strings/vectors; allocation failures may throw.
[[nodiscard]] constexpr std::pair<std::string, data_chunk>
decode(std::string_view str, std::string_view default_prefix);

} // namespace kth::domain::wallet::cashaddr

#include <kth/domain/impl/wallet/cashaddr.ipp>

#endif /* KTH_DOMAIN_WALLET_CASHADDR_HPP_ */


// Unit Tests ----------------------------------------------------
#ifdef DOCTEST_LIBRARY_INCLUDED
using namespace kth::domain::wallet;
using namespace std;

static
std::pair<std::string, std::vector<uint8_t>> cashdddr_decode(std::string const& str) {
    return cashaddr::decode(str, "");
}

bool case_insensitive_equal(std::string const& a, std::string const& b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t i = 0; i < a.size(); ++i) {
        char c1 = a[i];
        if (c1 >= 'A' && c1 <= 'Z') {
            c1 -= ('A' - 'a');
        }
        char c2 = b[i];
        if (c2 >= 'A' && c2 <= 'Z') {
            c2 -= ('A' - 'a');
        }
        if (c1 != c2) {
            return false;
        }
    }

    return true;
}

TEST_CASE("[cashaddr_testvectors_valid] cashaddr_testvectors_valid") {
    static std::string const CASES[] = {
        "prefix:x64nx6hz",
        "PREFIX:X64NX6HZ",
        "p:gpf8m4h7",
        "bitcoincash:qpzry9x8gf2tvdw0s3jn54khce6mua7lcw20ayyn",
        "bchtest:testnetaddress4d6njnut",
        "bchreg:555555555555555555555555555555555555555555555udxmlmrz",
    };

    for (std::string const& str : CASES) {
        auto ret = cashdddr_decode(str);
        CHECK( ! ret.first.empty());
        std::string recode = cashaddr::encode(ret.first, ret.second);
        CHECK( ! recode.empty());
        CHECK(case_insensitive_equal(str, recode));
    }
}

TEST_CASE("[cashaddr_testvectors_invalid] cashaddr_testvectors_invalid") {
    static std::string const CASES[] = {
        "prefix:x32nx6hz",
        "prEfix:x64nx6hz",
        "prefix:x64nx6Hz",
        "pref1x:6m8cxv73",
        "prefix:",
        ":u9wsx07j",
        "bchreg:555555555555555555x55555555555555555555555555udxmlmrz",
        "bchreg:555555555555555555555555555555551555555555555udxmlmrz",
        "pre:fix:x32nx6hz",
        "prefixx64nx6hz",
    };

    for (std::string const& str : CASES) {
        auto ret = cashdddr_decode(str);
        CHECK(ret.first.empty());
    }
}

TEST_CASE("[cashaddr_rawencode] cashaddr_rawencode") {
    using raw = std::pair<std::string, std::vector<uint8_t>>;

    raw toEncode;
    toEncode.first = "helloworld";
    toEncode.second = {0x1f, 0x0d};

    std::string encoded = cashaddr::encode(toEncode.first, toEncode.second);
    raw decoded = cashdddr_decode(encoded);

    CHECK(toEncode.first == decoded.first);

    CHECK(std::equal(begin(toEncode.second), end(toEncode.second),
                     begin(decoded.second), end(decoded.second)));

}

TEST_CASE("[cashaddr_testvectors_noprefix] cashaddr_testvectors_noprefix") {
    static const std::pair<std::string, std::string> CASES[] = {
        {"bitcoincash", "qpzry9x8gf2tvdw0s3jn54khce6mua7lcw20ayyn"},
        {"prefix", "x64nx6hz"},
        {"PREFIX", "X64NX6HZ"},
        {"p", "gpf8m4h7"},
        {"bitcoincash", "qpzry9x8gf2tvdw0s3jn54khce6mua7lcw20ayyn"},
        {"bchtest", "testnetaddress4d6njnut"},
        {"bchreg", "555555555555555555555555555555555555555555555udxmlmrz"},
    };

    for (std::pair<std::string, std::string> const& c : CASES) {
        std::string prefix = c.first;
        std::string payload = c.second;
        std::string addr = prefix + ":" + payload;
        auto ret = cashaddr::decode(payload, prefix);

        CHECK(case_insensitive_equal(ret.first, prefix));
        std::string recode = cashaddr::encode(ret.first, ret.second);
        CHECK( ! recode.empty());
        CHECK(case_insensitive_equal(addr, recode));
    }
}
#endif /*DOCTEST_LIBRARY_INCLUDED*/
// Unit Tests ----------------------------------------------------
