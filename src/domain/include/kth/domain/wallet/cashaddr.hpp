// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Cashaddr is an address format inspired by bech32.
#ifndef KTH_DOMAIN_WALLET_CASHADDR_HPP_
#define KTH_DOMAIN_WALLET_CASHADDR_HPP_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet::cashaddr {

std::string encode(std::string_view prefix, data_chunk const& payload);
std::pair<std::string, data_chunk> decode(std::string const& str, std::string const& default_prefix);

} // namespace kth::domain::wallet::cashaddr

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
