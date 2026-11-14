// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <string>

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
