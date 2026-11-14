// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_settings.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <stdexcept>
#include <system_error>
#include <utility>

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

bool validate_upstream(sv2_settings::jd_upstream const& upstream,
    std::string const& prefix, std::string& error) {
    if ( ! parse_jd_endpoint(upstream.mining_address)) {
        error = prefix + "mining_address must be a host:port or [IPv6]:port with a nonzero port";
        return false;
    }
    if ( ! parse_jd_endpoint(upstream.jds_address)) {
        error = prefix + "jds_address must be a host:port or [IPv6]:port with a nonzero port";
        return false;
    }
    if (upstream.mining_authority_public_key.size() > 128
        || ! noise_parse_authority_key(upstream.mining_authority_public_key)) {
        error = prefix + "mining_authority_public_key must be an SV2 Noise authority public key";
        return false;
    }
    if (upstream.jds_authority_public_key.size() > 128
        || ! noise_parse_authority_key(upstream.jds_authority_public_key)) {
        error = prefix + "jds_authority_public_key must be an SV2 Noise authority public key";
        return false;
    }
    if (upstream.user_identity.empty() || upstream.user_identity.size() > 255
        || ! utf8(upstream.user_identity)) {
        error = prefix + "user_identity must contain 1 to 255 UTF-8 bytes";
        return false;
    }
    if (upstream.mode != "full-template" && upstream.mode != "coinbase-only") {
        error = prefix + "mode must be full-template or coinbase-only";
        return false;
    }
    return true;
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

std::optional<sv2_settings::jd_upstream> parse_jd_upstream(std::string_view text) {
    if (text.size() > 1100) return std::nullopt;
    std::array<std::string_view, 7> fields;
    size_t count = 0;
    for (;;) {
        if (count == fields.size()) return std::nullopt;
        auto const end = text.find('|');
        fields[count] = text.substr(0, end);
        if (fields[count].empty()) return std::nullopt;
        ++count;
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    if (count < 6 || (count == 7 && fields[6] != jd_own_upstream)) return std::nullopt;
    return sv2_settings::jd_upstream{std::string(fields[0]), std::string(fields[1]),
        std::string(fields[2]), std::string(fields[3]), std::string(fields[4]), std::string(fields[5]),
        count == 7};
}

bool jd_local_host(std::string_view host) {
    if (host == "::1") return true;
    if (host.size() == 9 && std::equal(host.begin(), host.end(), "localhost",
            [](char a, char b) { return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b; })) {
        return true;
    }
    // parse_jd_endpoint has already refused a dotted number that is not IPv4.
    return host.starts_with("127.") && std::all_of(host.begin(), host.end(),
        [](char c) { return (c >= '0' && c <= '9') || c == '.'; });
}

bool jd_sends_worker_names(sv2_settings::jd_upstream const& upstream) {
    if (upstream.own) return true;
    auto const mining = parse_jd_endpoint(upstream.mining_address);
    return mining && jd_local_host(mining->host);
}

std::vector<sv2_settings::jd_upstream> jd_upstreams(sv2_settings::jd_settings const& settings) {
    if (settings.upstreams.empty()) {
        return {{settings.mining_address, settings.mining_authority_public_key,
            settings.jds_address, settings.jds_authority_public_key, settings.user_identity, settings.mode,
            settings.own_upstream}};
    }
    std::vector<sv2_settings::jd_upstream> result;
    result.reserve(settings.upstreams.size());
    for (auto const& text : settings.upstreams) {
        auto value = parse_jd_upstream(text);
        if ( ! value) throw std::invalid_argument("Invalid sv2.jd.upstream entry");
        result.push_back(std::move(*value));
    }
    return result;
}

bool validate_jd_settings(sv2_settings const& settings, std::string& error) {
    error.clear();
    auto const& jd = settings.jd;
    if ( ! jd.enabled) return true;
    if ( ! settings.enabled || ! settings.mining.enabled) {
        error = "sv2.jd.enabled requires sv2.enabled and sv2.mining.enabled";
        return false;
    }
    if ( ! jd.upstreams.empty()) {
        if ( ! jd.mining_address.empty() || ! jd.mining_authority_public_key.empty()
            || ! jd.jds_address.empty() || ! jd.jds_authority_public_key.empty()
            || ! jd.user_identity.empty() || jd.own_upstream
            || ( ! jd.mode.empty() && jd.mode != sv2_settings::jd_settings{}.mode)) {
            error = "sv2.jd.upstream cannot be combined with legacy single-pool settings";
            return false;
        }
        for (size_t i = 0; i < jd.upstreams.size(); ++i) {
            auto const value = parse_jd_upstream(jd.upstreams[i]);
            if ( ! value) {
                error = "sv2.jd.upstream requires six nonempty fields separated by '|', "
                    "optionally followed by |own";
                return false;
            }
            if ( ! validate_upstream(*value, "sv2.jd.upstream[" + std::to_string(i) + "].", error)) {
                return false;
            }
        }
    } else {
        auto const values = jd_upstreams(jd);
        if ( ! validate_upstream(values.front(), "sv2.jd.", error)) return false;
    }
    if (settings.handshake_timeout_seconds == 0 || settings.handshake_timeout_seconds > 600
        || jd.response_timeout_seconds == 0 || jd.response_timeout_seconds > 600) {
        error = "Job Declaration handshake and response deadlines must be between 1 and 600 seconds";
        return false;
    }
    auto bounded = [&](uint32_t value, uint32_t minimum, uint32_t maximum, char const* name) {
        if (value >= minimum && value <= maximum) return true;
        error = std::string("sv2.jd.") + name + " must be between "
            + std::to_string(minimum) + " and " + std::to_string(maximum);
        return false;
    };
    if ( ! bounded(jd.retry_initial_seconds, 1, 3600, "retry_initial_seconds")
        || ! bounded(jd.retry_max_seconds, 1, 3600, "retry_max_seconds")
        || ! bounded(jd.authority_hold_seconds, 60, 86400, "authority_hold_seconds")
        || ! bounded(jd.recovery_hold_seconds, 1, 3600, "recovery_hold_seconds")
        || ! bounded(jd.probe_interval_seconds, 1, 600, "probe_interval_seconds")
        || ! bounded(jd.max_latency_milliseconds, 1, 600000, "max_latency_milliseconds")
        || ! bounded(jd.max_stale_percent, 1, 100, "max_stale_percent")
        || ! bounded(jd.max_reject_percent, 1, 100, "max_reject_percent")
        || ! bounded(jd.health_window, 1, 1024, "health_window")) return false;
    if (jd.retry_initial_seconds > jd.retry_max_seconds) {
        error = "sv2.jd.retry_initial_seconds must not exceed retry_max_seconds";
        return false;
    }
    if (jd.authority_hold_seconds < jd.retry_max_seconds) {
        error = "sv2.jd.authority_hold_seconds must be at least retry_max_seconds";
        return false;
    }
    return true;
}

} // namespace kth::node::sv2
