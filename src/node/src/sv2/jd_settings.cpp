// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_settings.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <system_error>

#include <kth/infrastructure/config/authority.hpp>
#include <kth/node/sv2/noise.hpp>

namespace kth::node::sv2 {

namespace {

bool utf8(std::string_view text) {
    size_t position = 0;
    while (position < text.size()) {
        auto const first = static_cast<uint8_t>(text[position++]);
        if (first < 0x80) continue;
        size_t following = 0;
        if (first >= 0xc2 && first <= 0xdf) following = 1;
        else if (first >= 0xe0 && first <= 0xef) following = 2;
        else if (first >= 0xf0 && first <= 0xf4) following = 3;
        else return false;
        if (following > text.size() - position) return false;
        uint32_t point = first & ((1u << (6 - following)) - 1);
        for (size_t i = 0; i < following; ++i) {
            auto const next = static_cast<uint8_t>(text[position++]);
            if ((next & 0xc0) != 0x80) return false;
            point = (point << 6) | (next & 0x3f);
        }
        if ((following == 2 && point < 0x800) || (following == 3 && point < 0x10000)
            || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
    }
    return true;
}

bool dns_host(std::string_view host) {
    if (host.empty() || host.size() > 253) return false;
    size_t start = 0;
    while (start < host.size()) {
        auto const end = host.find('.', start);
        auto const label = host.substr(start,
            end == std::string_view::npos ? host.size() - start : end - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') {
            return false;
        }
        for (auto const c : label) {
            if ( ! ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '-')) return false;
        }
        if (end == std::string_view::npos) return true;
        start = end + 1;
    }
    return false;
}

} // namespace

std::optional<jd_endpoint> parse_jd_endpoint(std::string_view text) {
    if (text.empty() || text.size() > 261) return std::nullopt;
    auto const separator = text.rfind(':');
    if (separator == std::string_view::npos) return std::nullopt;
    auto const host = text.substr(0, separator);
    auto const port_text = text.substr(separator + 1);
    if (host.empty() || port_text.empty() || port_text.size() > 5) return std::nullopt;
    uint16_t port = 0;
    auto const parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() + port_text.size() || port == 0) {
        return std::nullopt;
    }
    if (host.front() == '[') {
        if (host.back() != ']') return std::nullopt;
        std::string normalized(text);
        for (auto& c : normalized) {
            if (c >= 'A' && c <= 'F') c = static_cast<char>(c + ('a' - 'A'));
        }
        if ( ! infrastructure::config::authority::parse_from(normalized)) return std::nullopt;
        return jd_endpoint{std::string(host.substr(1, host.size() - 2)), port};
    }
    if ( ! dns_host(host)) return std::nullopt;
    // A dotted numeric address is not a DNS name. Refuse invalid IPv4 at
    // startup instead of sending it to the resolver as a hostname.
    if (std::all_of(host.begin(), host.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; })
        && ! infrastructure::config::authority::parse_from(text)) return std::nullopt;
    return jd_endpoint{std::string(host), port};
}

bool validate_jd_settings(sv2_settings const& settings, std::string& error) {
    error.clear();
    auto const& jd = settings.jd;
    if ( ! jd.enabled) return true;
    if ( ! settings.enabled || ! settings.mining.enabled) {
        error = "sv2.jd.enabled requires sv2.enabled and sv2.mining.enabled";
        return false;
    }
    if ( ! parse_jd_endpoint(jd.mining_address)) {
        error = "sv2.jd.mining_address must be a host:port or [IPv6]:port with a nonzero port";
        return false;
    }
    if ( ! parse_jd_endpoint(jd.jds_address)) {
        error = "sv2.jd.jds_address must be a host:port or [IPv6]:port with a nonzero port";
        return false;
    }
    if (jd.mining_authority_public_key.size() > 128
        || ! noise_parse_authority_key(jd.mining_authority_public_key)) {
        error = "sv2.jd.mining_authority_public_key must be an SV2 Noise authority public key";
        return false;
    }
    if (jd.jds_authority_public_key.size() > 128
        || ! noise_parse_authority_key(jd.jds_authority_public_key)) {
        error = "sv2.jd.jds_authority_public_key must be an SV2 Noise authority public key";
        return false;
    }
    if (jd.user_identity.empty() || jd.user_identity.size() > 255
        || ! utf8(jd.user_identity)) {
        error = "sv2.jd.user_identity must contain 1 to 255 UTF-8 bytes";
        return false;
    }
    if (jd.mode != "full-template" && jd.mode != "coinbase-only") {
        error = "sv2.jd.mode must be full-template or coinbase-only";
        return false;
    }
    if (settings.handshake_timeout_seconds == 0 || settings.handshake_timeout_seconds > 600
        || jd.response_timeout_seconds == 0 || jd.response_timeout_seconds > 600) {
        error = "Job Declaration handshake and response deadlines must be between 1 and 600 seconds";
        return false;
    }
    return true;
}

} // namespace kth::node::sv2
