// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/bitcoin_uri.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <kth/domain/deserialization.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
#include <kth/domain/wallet/uri_reader.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_10.hpp>

namespace kth::domain::wallet {

namespace {

constexpr auto bitcoin_scheme = "bitcoin";
constexpr auto parameter_amount = "amount";
constexpr auto parameter_label = "label";
constexpr auto parameter_message = "message";
constexpr auto parameter_r = "r";
constexpr auto parameter_req_ = "req-";
constexpr size_t parameter_req_length = 4;

// RFC 3986 unreserved-in-query-value character check. Matches the
// `is_query` \ {`&`, `=`} class the parser accepts, so escape+decode
// round-trip is closed. Kept local because the shared helper in
// infrastructure/wallet/uri.cpp is `static`.
constexpr bool is_query_value_char(char c) noexcept {
    if (('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || ('0' <= c && c <= '9')) return true;
    switch (c) {
        case '-': case '.': case '_': case '~':                                     // unreserved
        case '!': case '$': case '\'': case '(': case ')': case '*': case '+':
        case ',': case ';':                                                         // sub-delims (minus '&', '=')
        case ':': case '@':                                                         // pchar extras
        case '/': case '?':                                                         // query extras
            return true;
        default:
            return false;
    }
}

std::string percent_encode(std::string_view in) {
    static constexpr std::array<char, 16> hex = {
        '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
    std::string out;
    out.reserve(in.size());
    for (auto const c : in) {
        if (is_query_value_char(c)) {
            out.push_back(c);
        } else {
            auto const byte = static_cast<uint8_t>(c);
            out.push_back('%');
            out.push_back(hex[byte >> 4]);
            out.push_back(hex[byte & 0x0F]);
        }
    }
    return out;
}

/// Internal `uri_reader` implementation. Holds the parser state
/// mutably during URI decoding; `parse_from` extracts the address /
/// query pair from a successful parse to build the immutable
/// `bitcoin_uri`.
struct bitcoin_uri_reader : public uri_reader {
    bool parsed_ok = false;
    bool strict = true;
    std::string address;
    boost::unordered_flat_map<std::string, std::string> query;

    void set_strict(bool s) override {
        strict = s;
    }

    bool set_scheme(std::string const& scheme) override {
        if (scheme != bitcoin_scheme) {
            return false;
        }
        parsed_ok = true;
        return true;
    }

    bool set_authority(std::string const& authority) override {
        // Using "bitcoin://" instead of "bitcoin:" is a common mistake, so we
        // allow the authority in place of the path when not strict.
        return ! strict && set_path(authority);
    }

    bool set_path(std::string const& path) override {
        if ( ! address.empty()) {
            return false;
        }
        // The path is either a payment_address or a stealth_address; we
        // only care that it decodes as one of them.
        if (payment_address::parse_from(path) || stealth_address::parse_from(path)) {
            address = path;
            return true;
        }
        return false;
    }

    bool set_fragment(std::string const& /*fragment*/) override {
        return false;
    }

    bool set_parameter(std::string const& key, std::string const& value) override {
        auto const required = [](std::string const& k) {
            return k.substr(0, parameter_req_length) == parameter_req_;
        };

        if (key == parameter_amount) {
            uint64_t decoded;
            if ( ! decode_base10(decoded, value, btc_decimal_places, strict)) {
                return false;
            }
            // Normalize the encoding so `parameter("amount")` reads
            // back the same string regardless of leading zeros.
            query[parameter_amount] = encode_base10(decoded, btc_decimal_places);
            return true;
        }

        if (key == parameter_label || key == parameter_message || key == parameter_r) {
            query[key] = value;
            return true;
        }

        // Fail on any required parameter that we don't support.
        return ! required(key);
    }
};

}  // namespace

// static
expect<bitcoin_uri> bitcoin_uri::parse_from(std::string_view text, bool strict) {
    auto reader = uri_reader::parse<bitcoin_uri_reader>(std::string{text}, strict);
    if ( ! reader.parsed_ok) {
        return std::unexpected(kth::error::illegal_value);
    }
    return bitcoin_uri(std::move(reader.address), std::move(reader.query));
}

// static
expect<bitcoin_uri> bitcoin_uri::from_parts(parts values) {
    if ( ! values.address.empty()
         && ! payment_address::parse_from(values.address)
         && ! stealth_address::parse_from(values.address)) {
        return std::unexpected(kth::error::illegal_value);
    }

    query_map query;
    if (values.amount) {
        query.emplace(parameter_amount, encode_base10(*values.amount, btc_decimal_places));
    }
    if (values.label) {
        query.emplace(parameter_label, std::move(*values.label));
    }
    if (values.message) {
        query.emplace(parameter_message, std::move(*values.message));
    }
    if (values.r) {
        query.emplace(parameter_r, std::move(*values.r));
    }

    return bitcoin_uri(std::move(values.address), std::move(query));
}

bitcoin_uri::bitcoin_uri(std::string address, query_map query)
    : address_(std::move(address))
    , query_(std::move(query))
{}

// Serializer.
// ----------------------------------------------------------------------------

std::string bitcoin_uri::to_string() const {
    // Emit directly: `bitcoin:<address>?k1=v1&k2=v2`. Keys are
    // materialized in lexicographic order so the output is
    // deterministic despite `query_` being an unordered map.
    std::string out;
    out.reserve(16 + address_.size() + query_.size() * 32);
    out.append(bitcoin_scheme);
    out.push_back(':');
    out.append(address_);

    if (query_.empty()) {
        return out;
    }

    std::vector<query_map::const_iterator> sorted;
    sorted.reserve(query_.size());
    for (auto it = query_.begin(); it != query_.end(); ++it) {
        sorted.push_back(it);
    }
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
        return a->first < b->first;
    });

    char sep = '?';
    for (auto const& it : sorted) {
        out.push_back(sep);
        out.append(percent_encode(it->first));
        out.push_back('=');
        out.append(percent_encode(it->second));
        sep = '&';
    }
    return out;
}

// Property getters.
// ----------------------------------------------------------------------------

uint64_t bitcoin_uri::amount() const {
    uint64_t satoshis;
    decode_base10(satoshis, parameter(parameter_amount), btc_decimal_places);
    return satoshis;
}

std::string bitcoin_uri::label() const {
    return parameter(parameter_label);
}

std::string bitcoin_uri::message() const {
    return parameter(parameter_message);
}

std::string bitcoin_uri::r() const {
    return parameter(parameter_r);
}

expect<payment_address> bitcoin_uri::payment() const {
    return payment_address::parse_from(address_);
}

expect<stealth_address> bitcoin_uri::stealth() const {
    return stealth_address::parse_from(address_);
}

std::string bitcoin_uri::parameter(std::string const& key) const {
    auto const value = query_.find(key);
    return value == query_.end() ? std::string() : value->second;
}

} // namespace kth::domain::wallet
