// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license. See COPYING or LICENSE.
#ifndef KTH_NETWORK_WS_DISCOVERY_HPP
#define KTH_NETWORK_WS_DISCOVERY_HPP

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <kth/network/define.hpp>

namespace kth::network::ws_discovery {

// Experimental protocol v1. Capability means understand/relay, not listen.
inline constexpr std::string_view capability = "/wsdisc:1/";
inline constexpr std::string_view request_command = "getwsaddr";
inline constexpr std::string_view response_command = "wsaddr";
inline constexpr std::uint8_t ws = 1;
inline constexpr std::uint8_t wss = 2;
inline constexpr std::uint8_t all_transports = ws | wss;
inline constexpr std::size_t maximum_endpoints = 16;
inline constexpr std::size_t maximum_payload = 16384;
inline constexpr std::size_t maximum_store = 256;
inline constexpr std::uint32_t maximum_ttl = 24 * 60 * 60;
inline constexpr std::uint32_t request_interval = 60;
inline constexpr std::uint32_t response_timeout = 30;

enum class failure { malformed, unsupported, nonpublic, expired, too_large };

struct endpoint {
    std::uint8_t transport = wss;
    // Canonical host only: no brackets, port, credentials, query or fragment.
    std::string host;
    std::uint16_t port = 443;
    std::string path = "/";
    std::uint32_t expires = 0;
    friend bool operator==(endpoint const&, endpoint const&) = default;
};

struct request {
    std::uint8_t transports = all_transports;
    std::uint64_t id = 0;
    friend bool operator==(request const&, request const&) = default;
};

struct response {
    std::uint64_t id = 0;
    std::vector<endpoint> endpoints;
    friend bool operator==(response const&, response const&) = default;
};

[[nodiscard]] KN_API bool understands(std::string_view user_agent) noexcept;
// Refuses an overlong result instead of silently truncating the capability.
[[nodiscard]] KN_API std::expected<std::string, failure> advertise(std::string_view user_agent);
[[nodiscard]] KN_API bool negotiated(std::string_view local_agent, std::string_view remote_agent) noexcept;

// Syntax canonicalization and conservative IP/name eligibility. Public DNS
// reachability cannot be established by syntax; it needs an owner's probe.
[[nodiscard]] KN_API std::expected<endpoint, failure> validate(endpoint value, std::uint32_t now, bool allow_lan = false);
[[nodiscard]] KN_API std::expected<endpoint, failure> from_url(std::string_view url, std::uint32_t expires, std::uint32_t now, bool allow_lan = false);
[[nodiscard]] KN_API std::string url(endpoint const& value);

[[nodiscard]] KN_API std::expected<std::vector<std::uint8_t>, failure> encode(request const& value);
[[nodiscard]] KN_API std::expected<request, failure> decode_request(std::span<std::uint8_t const> payload);
[[nodiscard]] KN_API std::expected<std::vector<std::uint8_t>, failure> encode(response const& value, std::uint32_t now, bool allow_lan = false);
[[nodiscard]] KN_API std::expected<response, failure> decode_response(std::span<std::uint8_t const> payload, std::uint32_t now, bool allow_lan = false);

// Owned by ONE peer protocol coroutine. No calls from other tasks/threads.
class KN_API session_policy {
public:
    explicit session_policy(bool mutually_negotiated) : enabled_(mutually_negotiated) {}
    // Incoming request admission (at most one per minute).
    [[nodiscard]] bool accept_request(request const& value, std::uint32_t now);
    // Must succeed BEFORE sending a request; one outstanding request, 30s TTL.
    [[nodiscard]] bool begin_request(request const& value, std::uint32_t now);
    // Matching response consumes the outstanding request exactly once.
    [[nodiscard]] bool accept_response(response const& value, std::uint32_t now);
private:
    bool enabled_;
    std::optional<std::uint32_t> last_incoming_;
    std::optional<std::uint32_t> last_outgoing_;
    std::optional<request> pending_;
    std::uint32_t sent_at_ = 0;
};

enum class verification { candidate, public_address_verified, explicit_lan };

// Owned by one discovery task. Incoming messages are explicit owner events.
// No timer/poll loop: expiry is applied on ingest/select/verify real events.
// Candidates are bounded and NEVER relayed before a successful public-address check.
class KN_API endpoint_store {
public:
    explicit endpoint_store(bool allow_lan = false) : allow_lan_(allow_lan) {}
    // When full, a new record replaces the oldest unverified gossip, or is refused.
    [[nodiscard]] bool ingest(endpoint value, std::uint32_t now);
    // The node's own advertised endpoints (at most maximum_endpoints): never
    // evicted, and admitted into a full store in place of the oldest gossip.
    [[nodiscard]] bool ingest_own(endpoint value, std::uint32_t now);
    // Operator config still requires public reachability verification, unless
    // LAN mode was explicitly selected. Does not extend a gossip record's TTL.
    [[nodiscard]] bool verify(endpoint const& value, verification status, std::uint32_t now);
    // key must already be canonical (from_url, validate or decode_response).
    [[nodiscard]] std::optional<endpoint> lookup(endpoint const& key, std::uint32_t now);
    [[nodiscard]] std::vector<endpoint> select(std::uint8_t transports, std::uint32_t now);
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
private:
    struct entry { endpoint value; verification status = verification::candidate; bool own = false; };
    void expire(std::uint32_t now);
    bool admit(endpoint value, std::uint32_t now, bool own);
    bool allow_lan_;
    std::vector<entry> entries_;
};

} // namespace kth::network::ws_discovery
#endif
