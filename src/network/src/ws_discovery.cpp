// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license. See COPYING or LICENSE.
#include <kth/network/ws_discovery.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <utility>

#include <asio/ip/address.hpp>

namespace kth::network::ws_discovery {
namespace {
using bytes = std::vector<std::uint8_t>;

bool supported(std::uint8_t value) {
    return value != 0 && (value & ~all_transports) == 0;
}

bool same_key(endpoint const& a, endpoint const& b) {
    return a.transport == b.transport && a.host == b.host && a.port == b.port && a.path == b.path;
}

bool ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool ascii_digit(char c) { return c >= '0' && c <= '9'; }
bool ascii_hex(char c) { return ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }

bool public_ip(::asio::ip::address const& ip) {
    if (ip.is_v4()) {
        auto a = ip.to_v4().to_bytes();
        // Conservative special-purpose and private IPv4 exclusions.
        return !(a[0] == 0 || a[0] == 10 || a[0] == 127 || a[0] >= 224
            || (a[0] == 100 && a[1] >= 64 && a[1] <= 127)
            || (a[0] == 169 && a[1] == 254)
            || (a[0] == 172 && a[1] >= 16 && a[1] <= 31)
            || (a[0] == 192 && a[1] == 168)
            || (a[0] == 192 && a[1] == 0 && (a[2] == 0 || a[2] == 2))
            || (a[0] == 192 && a[1] == 88 && a[2] == 99)
            || (a[0] == 198 && (a[1] == 18 || a[1] == 19))
            || (a[0] == 198 && a[1] == 51 && a[2] == 100)
            || (a[0] == 203 && a[1] == 0 && a[2] == 113));
    }
    auto a = ip.to_v6().to_bytes();
    // Only ordinary global unicast; exclude transition and documentation nets.
    return (a[0] & 0xe0) == 0x20
        && !(a[0] == 0x20 && a[1] == 0x02)
        && !(a[0] == 0x20 && a[1] == 0x01 && a[2] == 0 && a[3] == 0)
        && !(a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0d && a[3] == 0xb8)
        && !(a[0] == 0x20 && a[1] == 0x01 && a[2] == 0 && (a[3] & 0xf0) == 0x10)
        && !(a[0] == 0x20 && a[1] == 0x01 && a[2] == 0 && (a[3] & 0xf0) == 0x20)
        && !(a[0] == 0x20 && a[1] == 0x01 && a[2] == 0 && a[3] == 0x02)
        && !(a[0] == 0x3f && a[1] == 0xff && (a[2] & 0xf0) == 0);
}

bool valid_hostname(std::string_view host) {
    if (host.size() > 253 || host.empty() || host.back() == '.') return false;
    std::size_t start = 0;
    while (start < host.size()) {
        auto end = host.find('.', start);
        if (end == std::string_view::npos) end = host.size();
        auto const label = host.substr(start, end - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
        if (!std::all_of(label.begin(), label.end(), [](char c) { return ascii_alpha(c) || ascii_digit(c) || c == '-'; })) return false;
        start = end + 1;
    }
    return true;
}

bool public_name(std::string_view host) {
    auto dot = host.rfind('.');
    if (dot == std::string_view::npos || dot + 1 == host.size()) return false;
    auto suffix = host.substr(dot + 1);
    if (!std::any_of(suffix.begin(), suffix.end(), ascii_alpha)) return false;
    static constexpr std::array forbidden = {std::string_view("local"), std::string_view("localhost"),
        std::string_view("internal"), std::string_view("lan"), std::string_view("home"),
        std::string_view("invalid"), std::string_view("test"), std::string_view("example"),
        std::string_view("onion"), std::string_view("i2p")};
    if (std::find(forbidden.begin(), forbidden.end(), suffix) != forbidden.end()) return false;
    return host != "home.arpa" && !host.ends_with(".home.arpa");
}

bool valid_path(std::string_view path) {
    if (path.empty() || path.size() > 256 || path.front() != '/' || path.starts_with("//")) return false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        auto const c = path[i];
        if (!(ascii_alpha(c) || ascii_digit(c) || std::string_view("-._~!$&'()*+,;=:@/%").find(c) != std::string_view::npos)) return false;
        if (c == '%') {
            if (i + 2 >= path.size() || !ascii_hex(path[i + 1]) || !ascii_hex(path[i + 2])) return false;
            // Reject encoded delimiters, NUL and dot to avoid route aliases.
            auto hex = [](char d) { return ascii_digit(d) ? d - '0' : lower(d) - 'a' + 10; };
            auto decoded = hex(path[i + 1]) * 16 + hex(path[i + 2]);
            if (decoded <= 0x20 || decoded >= 0x7f || decoded == '/' || decoded == '\\' || decoded == '.' || decoded == '?' || decoded == '#') return false;
            i += 2;
        }
    }
    std::size_t pos = 1;
    while (pos <= path.size()) {
        auto end = path.find('/', pos);
        if (end == std::string_view::npos) end = path.size();
        auto part = path.substr(pos, end - pos);
        if (part == "." || part == "..") return false;
        pos = end + 1;
    }
    return true;
}

struct reader {
    std::span<std::uint8_t const> data;
    std::size_t position = 0;
    bool ok = true;
    std::uint64_t integer(std::size_t count) {
        if (count > data.size() - position) { ok = false; return 0; }
        std::uint64_t result = 0;
        for (std::size_t i = 0; i < count; ++i) result |= std::uint64_t(data[position++]) << (i * 8);
        return result;
    }
    std::string string(std::size_t count) {
        if (count > data.size() - position) { ok = false; return {}; }
        std::string result(reinterpret_cast<char const*>(data.data() + position), count);
        position += count;
        return result;
    }
    bool finished() const { return ok && position == data.size(); }
};

void integer(bytes& out, std::uint64_t value, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
void string(bytes& out, std::string const& value) { out.insert(out.end(), value.begin(), value.end()); }

bool elapsed(std::optional<std::uint32_t> previous, std::uint32_t now, std::uint32_t interval) {
    return !previous || (now >= *previous && now - *previous >= interval);
}
} // namespace

std::expected<bool, failure> decode_announcement(std::span<std::uint8_t const> payload) {
    if (payload.size() != 1) return std::unexpected(failure::malformed);
    return payload.front() == 1;
}

bool understands(std::string_view user_agent) noexcept {
    return user_agent.size() <= 256 && user_agent.find(capability) != std::string_view::npos;
}

std::expected<std::string, failure> advertise(std::string_view user_agent) {
    if (user_agent.size() > 256) return std::unexpected(failure::too_large);
    if (understands(user_agent)) return std::string(user_agent);
    auto const suffix = user_agent.ends_with('/') ? capability.substr(1) : capability;
    if (user_agent.size() + suffix.size() > 256) return std::unexpected(failure::too_large);
    std::string out(user_agent);
    // Adjacent components share their delimiter; legacy double-slash agents
    // remain recognized by understands().
    out += suffix;
    return out;
}

bool negotiated(std::string_view local_agent, std::string_view remote_agent) noexcept {
    return understands(local_agent) && understands(remote_agent);
}

std::expected<endpoint, failure> validate(endpoint value, std::uint32_t now, bool allow_lan) {
    if (value.transport != ws && value.transport != wss) return std::unexpected(failure::unsupported);
    if (value.expires <= now || value.expires - now > maximum_ttl) return std::unexpected(failure::expired);
    if (value.port == 0 || value.host.empty() || value.host.size() > 253 || !valid_path(value.path)) return std::unexpected(failure::malformed);
    if (value.host.find('%') != std::string::npos) return std::unexpected(failure::malformed);
    std::error_code ec;
    auto ip = ::asio::ip::make_address(value.host, ec);
    if (!ec) {
        if (ip.is_v6() && ip.to_v6().is_v4_mapped()) ip = ::asio::ip::make_address_v4(::asio::ip::v4_mapped_t{}, ip.to_v6());
        if (ip.is_unspecified() || ip.is_multicast()) return std::unexpected(failure::nonpublic);
        if (!allow_lan && !public_ip(ip)) return std::unexpected(failure::nonpublic);
        value.host = ip.to_string();
    } else {
        std::transform(value.host.begin(), value.host.end(), value.host.begin(), lower);
        if (!valid_hostname(value.host)) return std::unexpected(failure::malformed);
        if (!allow_lan && !public_name(value.host)) return std::unexpected(failure::nonpublic);
    }
    for (std::size_t i = 0; i < value.path.size(); ++i) {
        if (value.path[i] == '%') {
            for (auto j : {i + 1, i + 2}) if (value.path[j] >= 'a' && value.path[j] <= 'f') value.path[j] -= 'a' - 'A';
            i += 2;
        }
    }
    return value;
}

std::expected<endpoint, failure> from_url(std::string_view input, std::uint32_t expires, std::uint32_t now, bool allow_lan) {
    endpoint result;
    result.expires = expires;
    std::size_t start;
    if (input.starts_with("wss://")) { result.transport = wss; result.port = 443; start = 6; }
    else if (input.starts_with("ws://")) { result.transport = ws; result.port = 80; start = 5; }
    else return std::unexpected(failure::unsupported);
    auto slash = input.find('/', start);
    auto authority = input.substr(start, slash == std::string_view::npos ? input.size() - start : slash - start);
    result.path = slash == std::string_view::npos ? "/" : std::string(input.substr(slash));
    std::string_view port;
    if (authority.starts_with('[')) {
        auto close = authority.find(']');
        if (close == std::string_view::npos) return std::unexpected(failure::malformed);
        result.host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return std::unexpected(failure::malformed);
            port = authority.substr(close + 2);
            if (port.empty()) return std::unexpected(failure::malformed);
        }
        if (result.host.find(':') == std::string::npos) return std::unexpected(failure::malformed);
    } else {
        auto colon = authority.find(':');
        result.host = authority.substr(0, colon);
        if (colon != std::string_view::npos) {
            port = authority.substr(colon + 1);
            if (port.empty()) return std::unexpected(failure::malformed);
        }
    }
    if (!port.empty()) {
        unsigned number = 0;
        auto const converted = std::from_chars(port.data(), port.data() + port.size(), number);
        if (converted.ec != std::errc{} || converted.ptr != port.data() + port.size() || number == 0 || number > 65535) return std::unexpected(failure::malformed);
        result.port = static_cast<std::uint16_t>(number);
    }
    return validate(std::move(result), now, allow_lan);
}

std::string url(endpoint const& value) {
    auto host = value.host.find(':') == std::string::npos ? value.host : '[' + value.host + ']';
    return std::string(value.transport == wss ? "wss://" : "ws://") + host + ':' + std::to_string(value.port) + value.path;
}

std::expected<bytes, failure> encode(request const& value) {
    if (!supported(value.transports) || value.id == 0) return std::unexpected(failure::malformed);
    bytes out{1, value.transports};
    integer(out, value.id, 8);
    return out;
}

std::expected<request, failure> decode_request(std::span<std::uint8_t const> payload) {
    if (payload.size() != 10 || payload[0] != 1) return std::unexpected(failure::malformed);
    reader in{payload};
    (void)in.integer(1);
    request value;
    value.transports = static_cast<std::uint8_t>(in.integer(1));
    value.id = in.integer(8);
    if (!in.finished() || !supported(value.transports) || value.id == 0) return std::unexpected(failure::malformed);
    return value;
}

std::expected<bytes, failure> encode(response const& value, std::uint32_t now, bool allow_lan) {
    if (value.id == 0 || value.endpoints.size() > maximum_endpoints) return std::unexpected(failure::too_large);
    bytes out{1};
    integer(out, value.id, 8);
    integer(out, value.endpoints.size(), 1);
    std::vector<endpoint> seen;
    for (auto const& item : value.endpoints) {
        auto checked = validate(item, now, allow_lan);
        if (!checked) return std::unexpected(checked.error());
        if (std::any_of(seen.begin(), seen.end(), [&](auto const& old) { return same_key(old, *checked); })) return std::unexpected(failure::malformed);
        seen.push_back(*checked);
        out.push_back(checked->transport);
        integer(out, checked->expires, 4);
        // All integers are LE, including port, deliberately distinct from addr.
        integer(out, checked->port, 2);
        integer(out, checked->host.size(), 1);
        string(out, checked->host);
        integer(out, checked->path.size(), 2);
        string(out, checked->path);
    }
    if (out.size() > maximum_payload) return std::unexpected(failure::too_large);
    return out;
}

std::expected<response, failure> decode_response(std::span<std::uint8_t const> payload, std::uint32_t now, bool allow_lan) {
    if (payload.size() > maximum_payload || payload.size() < 10) return std::unexpected(failure::too_large);
    reader in{payload};
    if (in.integer(1) != 1) return std::unexpected(failure::unsupported);
    response value;
    value.id = in.integer(8);
    auto count = in.integer(1);
    if (value.id == 0 || count > maximum_endpoints) return std::unexpected(failure::malformed);
    for (std::uint64_t i = 0; i < count; ++i) {
        endpoint item;
        item.transport = static_cast<std::uint8_t>(in.integer(1));
        item.expires = static_cast<std::uint32_t>(in.integer(4));
        item.port = static_cast<std::uint16_t>(in.integer(2));
        auto host_size = in.integer(1);
        item.host = in.string(host_size);
        auto path_size = in.integer(2);
        if (path_size > 256) return std::unexpected(failure::too_large);
        item.path = in.string(path_size);
        if (!in.ok) return std::unexpected(failure::malformed);
        // Leases are the sender's clock: one that has run out is dropped, and
        // one longer than ours (a sender's clock ahead) is cut to ours. Neither
        // makes the response invalid.
        // Everything but the lease is checked first: a record that ran out is
        // still a malformed one if it is malformed.
        bool const ran_out = item.expires <= now;
        item.expires = ran_out ? now + 1
            : std::uint32_t(std::min<std::uint64_t>(item.expires, std::uint64_t(now) + maximum_ttl));
        auto checked = validate(std::move(item), now, allow_lan);
        if (!checked) return std::unexpected(checked.error());
        if (ran_out) continue;
        if (std::any_of(value.endpoints.begin(), value.endpoints.end(), [&](auto const& old) { return same_key(old, *checked); })) return std::unexpected(failure::malformed);
        value.endpoints.push_back(std::move(*checked));
    }
    if (!in.finished()) return std::unexpected(failure::malformed);
    return value;
}

bool session_policy::accept_request(request const& value, std::uint32_t now) {
    if (!enabled_ || !supported(value.transports) || value.id == 0 || !elapsed(last_incoming_, now, request_interval)) return false;
    last_incoming_ = now;
    return true;
}

bool session_policy::begin_request(request const& value, std::uint32_t now) {
    if (!enabled_ || !supported(value.transports) || value.id == 0 || !elapsed(last_outgoing_, now, request_interval)) return false;
    if (pending_ && (now < sent_at_ || now - sent_at_ < response_timeout)) return false;
    pending_ = value;
    sent_at_ = now;
    last_outgoing_ = now;
    return true;
}

bool session_policy::accept_response(response const& value, std::uint32_t now) {
    if (!enabled_ || !pending_ || now < sent_at_ || now - sent_at_ >= response_timeout || value.id != pending_->id || value.endpoints.size() > maximum_endpoints) return false;
    if (std::any_of(value.endpoints.begin(), value.endpoints.end(), [&](auto const& item) { return (item.transport != ws && item.transport != wss) || !(item.transport & pending_->transports); })) return false;
    pending_.reset();
    return true;
}

void endpoint_store::expire(std::uint32_t now) {
    std::erase_if(entries_, [now](auto const& item) { return item.value.expires <= now; });
}

bool endpoint_store::ingest(endpoint value, std::uint32_t now) {
    return admit(std::move(value), now, false);
}

bool endpoint_store::ingest_own(endpoint value, std::uint32_t now) {
    return admit(std::move(value), now, true);
}

bool endpoint_store::admit(endpoint value, std::uint32_t now, bool own) {
    expire(now);
    auto checked = validate(std::move(value), now, allow_lan_);
    if (!checked) return false;
    auto found = std::find_if(entries_.begin(), entries_.end(), [&](auto const& old) { return same_key(old.value, *checked); });
    if (found != entries_.end()) {
        // Gossip cannot extend an already admitted record's verification lease.
        // Expiry and a later re-ingest require a fresh public-address check.
        found->own = found->own || own;
        return true;
    }
    if (entries_.size() >= maximum_store) {
        // Full: the oldest unverified gossip makes room; for the node's own
        // endpoints, failing that, the oldest gossip. Its own are never evicted.
        auto evicted = std::find_if(entries_.begin(), entries_.end(), [](auto const& item) {
            return !item.own && item.status == verification::candidate;
        });
        if (evicted == entries_.end() && own) {
            evicted = std::find_if(entries_.begin(), entries_.end(), [](auto const& item) { return !item.own; });
        }
        if (evicted == entries_.end()) return false;
        entries_.erase(evicted);
    }
    std::error_code ec;
    auto const ip = ::asio::ip::make_address(checked->host, ec);
    auto const status = !ec && public_ip(ip) ? verification::public_address_verified : verification::candidate;
    entries_.push_back({std::move(*checked), status, own});
    return true;
}

bool endpoint_store::verify(endpoint const& value, verification status, std::uint32_t now) {
    expire(now);
    if (status == verification::candidate || (status == verification::explicit_lan && !allow_lan_)) return false;
    auto checked = validate(value, now, status == verification::explicit_lan && allow_lan_);
    if (!checked) return false;
    auto found = std::find_if(entries_.begin(), entries_.end(), [&](auto const& old) { return same_key(old.value, *checked); });
    if (found == entries_.end() || found->value.expires != checked->expires) return false;
    found->status = status;
    return true;
}

std::optional<endpoint> endpoint_store::lookup(endpoint const& key, std::uint32_t now) {
    expire(now);
    auto found = std::find_if(entries_.begin(), entries_.end(), [&](auto const& item) { return same_key(item.value, key); });
    if (found == entries_.end()) return std::nullopt;
    return found->value;
}

std::vector<endpoint> endpoint_store::select(std::uint8_t transports, std::uint32_t now) {
    expire(now);
    std::vector<endpoint> result;
    if (!supported(transports)) return result;
    for (auto const& item : entries_) {
        if (item.status == verification::candidate || !(item.value.transport & transports)) continue;
        result.push_back(item.value);
        if (result.size() == maximum_endpoints) break;
    }
    // Move selected entries behind the rest for bounded round-robin responses.
    if (!result.empty()) {
        std::stable_partition(entries_.begin(), entries_.end(), [&](auto const& item) {
            return std::none_of(result.begin(), result.end(), [&](auto const& chosen) { return same_key(item.value, chosen); });
        });
    }
    return result;
}

} // namespace kth::network::ws_discovery
