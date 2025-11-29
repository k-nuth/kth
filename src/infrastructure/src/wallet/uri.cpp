// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//TODO(fernando): use Boost.URL

#include <kth/infrastructure/wallet/uri.hpp>

#include <iomanip>
#include <sstream>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace {

constexpr uint8_t decode_hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return uint8_t(c - '0');
    if (c >= 'A' && c <= 'F') return uint8_t(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return uint8_t(c - 'a' + 10);
    return 0;
}

} // anonymous namespace

namespace kth::infrastructure::wallet {

// These character classification functions correspond to RFC 3986.
// They avoid C standard library character classification functions,
// since those give different answers based on the current locale.
static
bool is_alpha(char c) {
    return
        ('A' <= c && c <= 'Z') ||
        ('a' <= c && c <= 'z');
}

static
bool is_scheme(char c) {
    return
        is_alpha(c) || ('0' <= c && c <= '9') ||
        '+' == c || '-' == c || '.' == c;
}

static
bool is_path_char(char c) {
    return
        is_alpha(c) || ('0' <= c && c <= '9') ||
        '-' == c || '.' == c || '_' == c || '~' == c || // unreserved
        '!' == c || '$' == c || '&' == c || '\'' == c ||
        '(' == c || ')' == c || '*' == c || '+' == c ||
        ',' == c || ';' == c || '=' == c || // sub-delims
        ':' == c || '@' == c;
}

static
bool is_path(char c) {
    return is_path_char(c) || '/' == c;
}

static
bool is_query(char c) {
    return is_path_char(c) || '/' == c || '?' == c;
}

static
bool is_query_char(char c) {
    return is_query(c) && '&' != c && '=' != c;
}

// Verifies that all RFC 3986 escape sequences in a string are valid, and that
// all characters belong to the given class.
static
bool validate(std::string const& in, bool (*is_valid)(char const)) {
    auto i = in.begin();
    while (in.end() != i) {
        if ('%' == *i) {
            if ( ! (2 < in.end() - i && is_base16(i[1]) && is_base16(i[2]))) {
                return false;
            }
            i += 3;
        } else {
            if ( ! is_valid(*i)) {
                return false;
            }
            i += 1;
        }
    }

    return true;
}

// Decodes all RFC 3986 escape sequences in a string.
static
std::string unescape(std::string const& in) {
    // Do the conversion:
    std::string out;
    out.reserve(in.size());

    auto i = in.begin();
    while (in.end() != i) {
        if ('%' == *i && 2 < in.end() - i && is_base16(i[1]) && is_base16(i[2])) {
            out.push_back(uint8_t((decode_hex_digit(i[1]) << 4) | decode_hex_digit(i[2])));
            i += 3;
        } else {
            out.push_back(*i);
            i += 1;
        }
    }

    return out;
}

// URI encodes a string (i.e. percent encoding).
// is_valid a function returning true for acceptable characters.
static
std::string escape(std::string const& in, bool (*is_valid)(char)) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << std::setfill('0');
    for (auto const c: in) {
        if (is_valid(c)) {
            stream << c;
        } else {
            stream << '%' << std::setw(2) << +c;
        }
    }

    return stream.str();
}

bool uri::decode(std::string const& encoded, bool strict) {
    auto i = encoded.begin();

    // Store the scheme:
    auto start = i;
    while (encoded.end() != i && ':' != *i) {
        ++i;
    }

    scheme_ = std::string(start, i);
    if (scheme_.empty() || !is_alpha(scheme_[0])) {
        return false;
    }

    if ( ! std::all_of(scheme_.begin(), scheme_.end(), is_scheme)) {
        return false;
    }

    // Consume ':':
    if (encoded.end() == i) {
        return false;
    }

    ++i;

    // Consume "//":
    authority_.clear();
    has_authority_ = false;
    if (1 < encoded.end() - i && '/' == i[0] && '/' == i[1]) {
        has_authority_ = true;
        i += 2;

        // Store authority part:
        start = i;
        while (encoded.end() != i && '#' != *i && '?' != *i && '/' != *i) {
            ++i;
        }

        authority_ = std::string(start, i);
        if (strict && !validate(authority_, is_path_char)) {
            return false;
        }
    }

    // Store the path part:
    start = i;
    while (encoded.end() != i && '#' != *i && '?' != *i) {
        ++i;
    }

    path_ = std::string(start, i);
    if (strict && !validate(path_, is_path)) {
        return false;
    }

    // Consume '?':
    has_query_ = false;
    if (encoded.end() != i && '#' != *i) {
        has_query_ = true;
        ++i;
    }

    // Store the query part:
    start = i;
    while (encoded.end() != i && '#' != *i) {
        ++i;
    }

    query_ = std::string(start, i);
    if (strict && !validate(query_, is_query)) {
        return false;
    }

    // Consume '#':
    has_fragment_ = false;
    if (encoded.end() != i) {
        has_fragment_ = true;
        ++i;
    }

    // Store the fragment part:
    fragment_ = std::string(i, encoded.end());
    return !strict || validate(fragment_, is_query);
}

std::string uri::encoded() const {
    std::ostringstream out;
    out << scheme_ << ':';
    if (has_authority_) {
        out << "//" << authority_;
    }

    out << path_;
    if (has_query_) {
        out << '?' << query_;
    }

    if (has_fragment_) {
        out << '#' << fragment_;
    }

    return out.str();
}

// Scheme accessors:

std::string uri::scheme() const {
    auto out = scheme_;
    for (auto& c: out) {
        if ('A' <= c && c <= 'Z') {
            c = c - 'A' + 'a';
        }
    }

    return out;
}

void uri::set_scheme(std::string const& scheme) {
    scheme_ = scheme;
}

// Authority accessors:

std::string uri::authority() const {
    return unescape(authority_);
}

bool uri::has_authority() const {
    return has_authority_;
}

void uri::set_authority(std::string const& authority) {
    has_authority_ = true;
    authority_ = escape(authority, is_path_char);
}

void uri::remove_authority() {
    has_authority_ = false;
}

// Path accessors:

std::string uri::path() const {
    return unescape(path_);
}

void uri::set_path(std::string const& path) {
    path_ = escape(path, is_path);
}

// Query accessors:

std::string uri::query() const {
    return unescape(query_);
}

bool uri::has_query() const {
    return has_query_;
}

void uri::set_query(std::string const& query) {
    has_query_ = true;
    query_ = escape(query, is_query);
}

void uri::remove_query() {
    has_query_ = false;
}

// Fragment accessors:

std::string uri::fragment() const {
    return unescape(fragment_);
}

bool uri::has_fragment() const {
    return has_fragment_;
}

void uri::set_fragment(std::string const& fragment) {
    has_fragment_ = true;
    fragment_ = escape(fragment, is_query);
}

void uri::remove_fragment() {
    has_fragment_ = false;
}

// Query interpretation:

uri::query_map uri::decode_query() const {
    query_map out;
    auto i = query_.begin();
    while (query_.end() != i) {
        // Read the key:
        auto begin = i;
        while (query_.end() != i && '&' != *i && '=' != *i) {
            ++i;
        }

        auto key = unescape(std::string(begin, i));

        // Consume '=':
        if (query_.end() != i && '&' != *i) {
            ++i;
        }

        // Read the value:
        begin = i;
        while (query_.end() != i && '&' != *i) {
            ++i;
        }

        out[key] = unescape(std::string(begin, i));

        // Consume '&':
        if (query_.end() != i) {
            ++i;
        }
    }

    return out;
}

void uri::encode_query(const query_map& map) {
    auto first = true;
    std::ostringstream query;
    for (auto const& term : map) {
        if ( ! first) {
            query << '&';
        }

        first = false;
        query << escape(term.first, is_query_char);
        if ( ! term.second.empty()) {
            query << '=' << escape(term.second, is_query_char);
        }
    }

    has_query_ = !map.empty();
    query_ = query.str();
}

} // namespace kth::infrastructure::wallet
