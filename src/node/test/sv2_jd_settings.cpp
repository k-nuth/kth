// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <kth/node/sv2/jd_settings.hpp>
#include <kth/node/sv2/noise.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

node::sv2_settings configured_jd() {
    node::sv2_settings settings;
    settings.enabled = true;
    settings.mining.enabled = true;
    settings.jd.enabled = true;
    settings.jd.mining_address = "pool.example:3333";
    settings.jd.jds_address = "[::1]:3334";
    hash_digest secret{};
    secret.back() = 1;
    auto const key = noise_keypair::create(secret, {});
    REQUIRE(key);
    settings.jd.mining_authority_public_key = noise_authority_key(key->public_key());
    settings.jd.jds_authority_public_key = settings.jd.mining_authority_public_key;
    settings.jd.user_identity = "account.worker";
    settings.jd.mode = "full-template";
    return settings;
}

std::string upstream_text(node::sv2_settings::jd_settings const& settings) {
    return settings.mining_address + "|" + settings.mining_authority_public_key + "|"
        + settings.jds_address + "|" + settings.jds_authority_public_key + "|"
        + settings.user_identity + "|" + settings.mode;
}

node::sv2_settings configured_list() {
    auto settings = configured_jd();
    auto const text = upstream_text(settings.jd);
    settings.jd = node::sv2_settings::jd_settings{};
    settings.jd.enabled = true;
    settings.jd.upstreams = {text};
    return settings;
}

} // namespace

TEST_CASE("sv2 JD endpoints require bounded hosts and explicit nonzero ports", "[sv2 jd]") {
    for (auto const* text : {"pool.example:3333", "localhost:1", "127.0.0.1:65535",
        "[::1]:3333", "[2001:DB8::2]:4444"}) {
        INFO(text);
        CHECK(parse_jd_endpoint(text));
    }
    auto const ipv6 = parse_jd_endpoint("[2001:DB8::2]:4444");
    REQUIRE(ipv6);
    CHECK(ipv6->host == "2001:DB8::2");
    CHECK(ipv6->port == 4444);
    auto const dns = parse_jd_endpoint("Pool.Example:3333");
    REQUIRE(dns);
    CHECK(dns->host == "Pool.Example");
    CHECK(dns->port == 3333);

    for (auto const* text : {"", "pool.example", ":3333", "pool.example:",
        "pool.example:0", "pool.example:-1", "pool.example:+1", "pool.example:65536",
        "pool.example:123456", "pool.example:3x", "pool.example: 3", "pool.example:3 ",
        " pool.example:3", "pool.example/path:3", "user@pool.example:3", "tcp://pool.example:3",
        "stratum2+tcp://pool.example:3", "pool..example:3", ".pool.example:3", "pool.example.:3",
        "-pool.example:3", "pool-.example:3", "pool_example:3", "256.0.0.1:3", "1.2.3:3",
        "[::1:3", "[]:3", "[::gg]:3", "[127.0.0.1]:3", "::1:3", "[::1]suffix:3"}) {
        INFO(text);
        CHECK_FALSE(parse_jd_endpoint(text));
    }
    CHECK_FALSE(parse_jd_endpoint(std::string(262, 'a')));
    CHECK_FALSE(parse_jd_endpoint(std::string(64, 'a') + ".example:3"));
    auto const longest = std::string(63, 'a') + "." + std::string(63, 'b') + "."
        + std::string(63, 'c') + "." + std::string(61, 'd');
    CHECK(parse_jd_endpoint(longest + ":65535"));
    CHECK_FALSE(parse_jd_endpoint(longest + "e:3"));
    CHECK_FALSE(parse_jd_endpoint(std::string("pool\0name:3", 11)));
}

TEST_CASE("sv2 JD validation keeps dormant settings inert and accepts both explicit modes", "[sv2 jd]") {
    std::string error = "previous error";
    node::sv2_settings disabled;
    disabled.jd.mining_address = "invalid";
    disabled.jd.mode = "invalid";
    disabled.jd.response_timeout_seconds = 0;
    REQUIRE(validate_jd_settings(disabled, error));
    CHECK(error.empty());

    auto settings = configured_jd();
    REQUIRE(validate_jd_settings(settings, error));
    CHECK(error.empty());
    settings.jd.mode = "coinbase-only";
    settings.jd.user_identity = std::string(255, 'x');
    settings.jd.response_timeout_seconds = 600;
    settings.handshake_timeout_seconds = 600;
    CHECK(validate_jd_settings(settings, error));
    settings.jd.user_identity = "\xc2\xa3\xe2\x82\xac\xf0\x90\x80\x80";
    CHECK(validate_jd_settings(settings, error));
}

TEST_CASE("sv2 JD defaults to coinbase-only mode", "[sv2 jd]") {
    // Coinbase-only sends a merkle path instead of the transaction list, so
    // no SV2 sequence or frame limit bounds the template the node mines.
    REQUIRE(node::sv2_settings{}.jd.mode == "coinbase-only");
    auto settings = configured_jd();
    settings.jd.mode = node::sv2_settings{}.jd.mode;
    std::string error;
    CHECK(validate_jd_settings(settings, error));
    CHECK(error.empty());
}

TEST_CASE("sv2 JD validation refuses missing prerequisites and malformed upstream settings", "[sv2 jd]") {
    auto const valid = configured_jd();
    std::string error;
    auto check = [&](node::sv2_settings const& settings, char const* setting) {
        CHECK_FALSE(validate_jd_settings(settings, error));
        CHECK(error.find(setting) != std::string::npos);
    };
    auto settings = valid;
    settings.enabled = false;
    check(settings, "sv2.enabled");
    settings = valid;
    settings.mining.enabled = false;
    check(settings, "sv2.mining.enabled");

    for (auto const* value : {"", "pool.example:0", "pool.example"}) {
        settings = valid;
        settings.jd.mining_address = value;
        check(settings, "sv2.jd.mining_address");
        settings = valid;
        settings.jd.jds_address = value;
        check(settings, "sv2.jd.jds_address");
    }
    for (auto const& value : {std::string{}, std::string("not-an-authority"), std::string(129, 'x')}) {
        settings = valid;
        settings.jd.mining_authority_public_key = value;
        check(settings, "sv2.jd.mining_authority_public_key");
        settings = valid;
        settings.jd.jds_authority_public_key = value;
        check(settings, "sv2.jd.jds_authority_public_key");
    }
    for (auto const& value : {std::string{}, std::string(256, 'x'), std::string(1, '\x80'),
        std::string("\xc0\x80"), std::string("\xc2"), std::string("\xc2x"),
        std::string("\xe0\x80\x80"), std::string("\xf0\x80\x80\x80"),
        std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string(1, '\xf5')}) {
        settings = valid;
        settings.jd.user_identity = value;
        check(settings, "sv2.jd.user_identity");
    }
    for (auto const* value : {"", "full", "Full-template", "coinbase-only "}) {
        settings = valid;
        settings.jd.mode = value;
        check(settings, "sv2.jd.mode");
    }
    for (auto const timeout : {0u, 601u}) {
        settings = valid;
        settings.handshake_timeout_seconds = timeout;
        check(settings, "deadlines");
        settings = valid;
        settings.jd.response_timeout_seconds = timeout;
        check(settings, "deadlines");
    }
}

TEST_CASE("sv2 JD upstream lists preserve priority and legacy single pool settings", "[sv2 jd]") {
    auto const legacy = configured_jd();
    auto const single = jd_upstreams(legacy.jd);
    REQUIRE(single.size() == 1);
    CHECK(single.front().mining_address == legacy.jd.mining_address);
    CHECK(single.front().jds_authority_public_key == legacy.jd.jds_authority_public_key);
    CHECK(single.front().mode == legacy.jd.mode);

    auto settings = configured_list();
    auto lower = legacy;
    lower.jd.mining_address = "backup.example:3335";
    lower.jd.jds_address = "backup-jds.example:3336";
    lower.jd.user_identity = "backup.worker";
    lower.jd.mode = "coinbase-only";
    settings.jd.upstreams.push_back(upstream_text(lower.jd));
    std::string error;
    REQUIRE(validate_jd_settings(settings, error));
    CHECK(error.empty());
    auto const entries = jd_upstreams(settings.jd);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].mining_address == legacy.jd.mining_address);
    CHECK(entries[0].mining_authority_public_key == legacy.jd.mining_authority_public_key);
    CHECK(entries[0].jds_address == legacy.jd.jds_address);
    CHECK(entries[0].jds_authority_public_key == legacy.jd.jds_authority_public_key);
    CHECK(entries[0].user_identity == legacy.jd.user_identity);
    CHECK(entries[0].mode == legacy.jd.mode);
    CHECK(entries[1].mining_address == lower.jd.mining_address);
    CHECK(entries[1].mining_authority_public_key == lower.jd.mining_authority_public_key);
    CHECK(entries[1].jds_address == lower.jd.jds_address);
    CHECK(entries[1].jds_authority_public_key == lower.jd.jds_authority_public_key);
    CHECK(entries[1].user_identity == "backup.worker");
    CHECK(entries[1].mode == "coinbase-only");
}

TEST_CASE("sv2 JD list identities preserve UTF-8 and keep delimiter restrictions local", "[sv2 jd]") {
    auto legacy = configured_jd();
    legacy.jd.user_identity = "account|worker";
    std::string error;
    REQUIRE(validate_jd_settings(legacy, error));
    CHECK(jd_upstreams(legacy.jd).front().user_identity == "account|worker");

    auto settings = configured_list();
    settings.jd.upstreams = {upstream_text(legacy.jd)};
    CHECK_FALSE(validate_jd_settings(settings, error));
    settings.jd.enabled = false;
    CHECK(validate_jd_settings(settings, error));
    CHECK(error.empty());

    legacy.jd.user_identity = "account.\xc2\xa3\xe2\x82\xac\xf0\x90\x80\x80";
    settings.jd.enabled = true;
    settings.jd.upstreams = {upstream_text(legacy.jd)};
    REQUIRE(validate_jd_settings(settings, error));
    CHECK(jd_upstreams(settings.jd).front().user_identity == legacy.jd.user_identity);
}

TEST_CASE("sv2 JD upstream list parsing refuses missing extra empty and oversized fields", "[sv2 jd]") {
    for (auto const& text : {std::string{}, std::string("a|b|c|d|e"),
        std::string("a|b|c|d|e|f|g"), std::string("|b|c|d|e|f"),
        std::string("a||c|d|e|f"), std::string("a|b||d|e|f"),
        std::string("a|b|c||e|f"), std::string("a|b|c|d||f"),
        std::string("a|b|c|d|e|"), std::string(1101, 'x')}) {
        INFO(text);
        CHECK_FALSE(parse_jd_upstream(text));
        auto settings = configured_list();
        settings.jd.upstreams = {text};
        std::string error;
        CHECK_FALSE(validate_jd_settings(settings, error));
        CHECK(error.find("sv2.jd.upstream") != std::string::npos);
        CHECK_THROWS_AS(jd_upstreams(settings.jd), std::invalid_argument);
    }
}

TEST_CASE("sv2 JD list entries keep endpoint key identity and mode validation strict", "[sv2 jd]") {
    auto const valid = configured_jd();
    for (auto const member : {&node::sv2_settings::jd_settings::mining_address,
        &node::sv2_settings::jd_settings::mining_authority_public_key,
        &node::sv2_settings::jd_settings::jds_address,
        &node::sv2_settings::jd_settings::jds_authority_public_key,
        &node::sv2_settings::jd_settings::user_identity,
        &node::sv2_settings::jd_settings::mode}) {
        auto invalid = valid;
        invalid.jd.*member = "invalid";
        if (member == &node::sv2_settings::jd_settings::user_identity) {
            invalid.jd.user_identity = std::string(1, '\x80');
        }
        auto settings = configured_list();
        settings.jd.upstreams.push_back(upstream_text(invalid.jd));
        std::string error;
        CHECK_FALSE(validate_jd_settings(settings, error));
        CHECK(error.find("sv2.jd.upstream[1].") != std::string::npos);

        settings = configured_list();
        settings.jd.*member = valid.jd.*member;
        CHECK_FALSE(validate_jd_settings(settings, error));
        CHECK(error.find("legacy") != std::string::npos);
    }
}

TEST_CASE("sv2 JD failover thresholds are bounded and cross checked", "[sv2 jd]") {
    using jd_settings = node::sv2_settings::jd_settings;
    struct bound {
        uint32_t jd_settings::* member;
        uint32_t minimum;
        uint32_t maximum;
        char const* name;
    };
    for (auto const& limit : std::vector<bound>{
        {&jd_settings::retry_initial_seconds, 1, 3600, "retry_initial_seconds"},
        {&jd_settings::retry_max_seconds, 1, 3600, "retry_max_seconds"},
        {&jd_settings::authority_hold_seconds, 60, 86400, "authority_hold_seconds"},
        {&jd_settings::recovery_hold_seconds, 1, 3600, "recovery_hold_seconds"},
        {&jd_settings::probe_interval_seconds, 1, 600, "probe_interval_seconds"},
        {&jd_settings::max_latency_milliseconds, 1, 600000, "max_latency_milliseconds"},
        {&jd_settings::max_stale_percent, 1, 100, "max_stale_percent"},
        {&jd_settings::max_reject_percent, 1, 100, "max_reject_percent"},
        {&jd_settings::health_window, 1, 1024, "health_window"}}) {
        INFO(limit.name);
        for (auto const value : {limit.minimum - 1, limit.maximum + 1}) {
            auto settings = configured_jd();
            settings.jd.*limit.member = value;
            std::string error;
            CHECK_FALSE(validate_jd_settings(settings, error));
            CHECK(error.find(limit.name) != std::string::npos);
            settings.jd.enabled = false;
            CHECK(validate_jd_settings(settings, error));
        }
        for (auto const value : {limit.minimum, limit.maximum}) {
            auto settings = configured_jd();
            settings.jd.retry_initial_seconds = 1;
            settings.jd.retry_max_seconds = 3600;
            settings.jd.authority_hold_seconds = 86400;
            settings.jd.*limit.member = value;
            if (limit.member == &jd_settings::authority_hold_seconds) settings.jd.retry_max_seconds = 1;
            std::string error;
            CHECK(validate_jd_settings(settings, error));
        }
    }
    auto settings = configured_jd();
    settings.jd.retry_initial_seconds = settings.jd.retry_max_seconds + 1;
    std::string error;
    CHECK_FALSE(validate_jd_settings(settings, error));
    CHECK(error.find("retry_initial_seconds") != std::string::npos);
    settings = configured_jd();
    settings.jd.authority_hold_seconds = 60;
    CHECK_FALSE(validate_jd_settings(settings, error));
    CHECK(error.find("authority_hold_seconds") != std::string::npos);
}

TEST_CASE("sv2 JD upstream lists accept the default single-pool mode and refuse a chosen one", "[sv2 jd]") {
    // sv2.jd.mode defaults to coinbase-only, so leaving it alone is not a
    // single-pool setting. Choosing a mode next to the list still is.
    auto settings = configured_list();
    REQUIRE(settings.jd.mode == "coinbase-only");
    std::string error;
    CHECK(validate_jd_settings(settings, error));
    CHECK(error.empty());
    settings.jd.mode = "full-template";
    CHECK_FALSE(validate_jd_settings(settings, error));
    CHECK(error.find("legacy single-pool settings") != std::string::npos);
}

TEST_CASE("sv2 JD list entries take an optional seventh field marking the operator's own upstream", "[sv2 jd]") {
    auto const six = parse_jd_upstream("a|b|c|d|e|f");
    REQUIRE(six);
    CHECK_FALSE(six->own);
    auto const seven = parse_jd_upstream("a|b|c|d|e|f|own");
    REQUIRE(seven);
    CHECK(seven->own);
    CHECK(seven->mode == "f");
    for (auto const* text : {"a|b|c|d|e|f|", "a|b|c|d|e|f|Own", "a|b|c|d|e|f|own ",
        "a|b|c|d|e|f|yes", "a|b|c|d|e|f|own|own", "a|b|c|d|e|f|own|"}) {
        INFO(text);
        CHECK_FALSE(parse_jd_upstream(text));
    }

    auto settings = configured_list();
    settings.jd.upstreams.push_back(settings.jd.upstreams.front() + "|own");
    std::string error;
    REQUIRE(validate_jd_settings(settings, error));
    auto const entries = jd_upstreams(settings.jd);
    REQUIRE(entries.size() == 2);
    CHECK_FALSE(entries[0].own);
    CHECK(entries[1].own);
    CHECK(entries[1].mode == entries[0].mode);
}

TEST_CASE("sv2 JD own_upstream marks the single-pool upstream and is off by default", "[sv2 jd]") {
    REQUIRE_FALSE(node::sv2_settings{}.jd.own_upstream);
    auto legacy = configured_jd();
    CHECK_FALSE(jd_upstreams(legacy.jd).front().own);
    legacy.jd.own_upstream = true;
    std::string error;
    REQUIRE(validate_jd_settings(legacy, error));
    CHECK(jd_upstreams(legacy.jd).front().own);

    // Like the other single-pool settings, it cannot be combined with a list.
    auto settings = configured_list();
    settings.jd.own_upstream = true;
    CHECK_FALSE(validate_jd_settings(settings, error));
    CHECK(error.find("legacy single-pool settings") != std::string::npos);
}

TEST_CASE("sv2 JD sends worker names only to a marked upstream or one on this machine", "[sv2 jd]") {
    auto upstream = jd_upstreams(configured_jd().jd).front();
    // The configured host string decides. Names are not resolved, and a LAN
    // address is someone else's pool unless it is marked.
    for (auto const* address : {"127.0.0.1:3333", "127.1.2.3:3333", "[::1]:3333",
        "localhost:3333", "LocalHost:3333"}) {
        INFO(address);
        upstream.mining_address = address;
        upstream.own = false;
        CHECK(jd_sends_worker_names(upstream));
    }
    for (auto const* address : {"10.0.0.5:3333", "192.168.1.2:3333", "128.0.0.1:3333",
        "[::2]:3333", "127.example:3333", "localhost.example:3333", "pool.example:3333"}) {
        INFO(address);
        upstream.mining_address = address;
        upstream.own = false;
        CHECK_FALSE(jd_sends_worker_names(upstream));
        upstream.own = true;
        CHECK(jd_sends_worker_names(upstream));
    }
    // Only the Mining address counts: the names travel on that connection.
    upstream.mining_address = "10.0.0.5:3333";
    upstream.jds_address = "127.0.0.1:3334";
    upstream.own = false;
    CHECK_FALSE(jd_sends_worker_names(upstream));
}
