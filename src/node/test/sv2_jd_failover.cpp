// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <cstddef>
#include <string>

#include <kth/node/settings.hpp>
#include <kth/node/sv2/jd_failover.hpp>

using namespace kth::node;
using namespace kth::node::sv2;
using namespace std::chrono_literals;

namespace {

sv2_settings::jd_settings limits() {
    sv2_settings::jd_settings settings;
    settings.retry_initial_seconds = 2;
    settings.retry_max_seconds = 8;
    settings.authority_hold_seconds = 60;
    settings.recovery_hold_seconds = 10;
    settings.probe_interval_seconds = 3;
    settings.max_latency_milliseconds = 100;
    settings.max_stale_percent = 25;
    settings.max_reject_percent = 25;
    settings.health_window = 4;
    return settings;
}

jd_failover::time_point const start{100s};

} // namespace

TEST_CASE("sv2 jd failover starts in priority order and keeps solo when every entry is held", "[sv2 jd]") {
    jd_failover pools(3, limits());
    REQUIRE(pools.begin(start) == 0);
    REQUIRE(pools.active() == 0);
    REQUIRE_FALSE(pools.begin(start));
    pools.healthy(0, start);
    REQUIRE(pools.entries()[0].health == jd_health::healthy);
    pools.fail(0, jd_failure::transient, "connect failed", start);
    REQUIRE(pools.begin(start) == 1);
    pools.fail(1, jd_failure::transient, "token timeout", start);
    REQUIRE(pools.begin(start) == 2);
    pools.fail(2, jd_failure::transient, "custom job refused", start);
    REQUIRE_FALSE(pools.begin(start));
    REQUIRE_FALSE(pools.active());
    auto const snapshot = pools.snapshot();
    REQUIRE_FALSE(snapshot.active);
    REQUIRE_FALSE(snapshot.probe);
    REQUIRE(snapshot.entries.size() == 3);
    REQUIRE(snapshot.entries[0].last_error == "connect failed");
    REQUIRE(snapshot.entries[1].last_error == "token timeout");
    REQUIRE(snapshot.entries[2].last_error == "custom job refused");
    REQUIRE(pools.begin(start + 2s) == 0);

    jd_failover empty(0, limits());
    REQUIRE_FALSE(empty.begin(start));
    REQUIRE_FALSE(empty.begin_probe(start));
    REQUIRE_FALSE(empty.switch_at_boundary(start, true));
    empty.release_probe();
    REQUIRE_FALSE(empty.probe());
}

TEST_CASE("sv2 jd failover doubles backoff to its cap and setup alone does not reset it", "[sv2 jd]") {
    jd_failover pools(1, limits());
    auto now = start;
    for (auto const delay : {2s, 4s, 8s, 8s}) {
        REQUIRE(pools.begin(now) == 0);
        pools.healthy(0, now);
        pools.fail(0, jd_failure::transient, "dropped after setup", now + 1s);
        auto const& entry = pools.entries()[0];
        REQUIRE(entry.backoff == delay);
        REQUIRE(entry.retry_at == now + 1s + delay);
        REQUIRE_FALSE(pools.begin(entry.retry_at - 1ms));
        now = entry.retry_at;
    }
    REQUIRE(pools.begin(now) == 0);
    pools.healthy(0, now);
    pools.fail(0, jd_failure::transient, "dropped after stable recovery", now + 10s);
    REQUIRE(pools.entries()[0].backoff == 2s);
}

TEST_CASE("sv2 jd failover flags authority and dishonest peers for a long hold and disables incompatible peers", "[sv2 jd]") {
    for (auto const failure : {jd_failure::authority, jd_failure::dishonest}) {
        CAPTURE(static_cast<int>(failure));
        jd_failover pools(2, limits());
        REQUIRE(pools.begin(start) == 0);
        pools.fail(0, failure, "flagged peer", start);
        REQUIRE(pools.entries()[0].health == (failure == jd_failure::authority
            ? jd_health::authority_hold : jd_health::dishonest));
        REQUIRE(pools.entries()[0].retry_at == start + 60s);
        REQUIRE(pools.begin(start) == 1);
        REQUIRE_FALSE(pools.begin_probe(start + 59s));
        REQUIRE(pools.begin_probe(start + 60s) == 0);
    }
    jd_failover pools(2, limits());
    REQUIRE(pools.begin(start) == 0);
    pools.fail(0, jd_failure::incompatible, "required setup feature missing", start);
    REQUIRE(pools.entries()[0].health == jd_health::disabled);
    REQUIRE(pools.entries()[0].retry_at == jd_failover::time_point::max());
    REQUIRE(pools.begin(start) == 1);
    REQUIRE_FALSE(pools.begin_probe(start + 24h));
    pools.fail(1, jd_failure::incompatible, "setup refused", start);
    REQUIRE_FALSE(pools.begin(start + 24h));
    jd_failover restarted(2, limits());
    REQUIRE(restarted.begin(start + 24h) == 0);
}

TEST_CASE("sv2 jd failover reserves one probe and waits for the hold and a job boundary", "[sv2 jd]") {
    jd_failover pools(3, limits());
    REQUIRE(pools.begin(start) == 0);
    pools.fail(0, jd_failure::transient, "drop", start);
    REQUIRE(pools.begin(start) == 1);
    pools.fail(1, jd_failure::transient, "drop", start);
    REQUIRE(pools.begin(start) == 2);
    pools.healthy(2, start);
    REQUIRE_FALSE(pools.begin_probe(start + 1s));
    REQUIRE(pools.begin_probe(start + 2s) == 0);
    REQUIRE(pools.probe() == 0);
    REQUIRE_FALSE(pools.begin_probe(start + 100s));
    REQUIRE_FALSE(pools.switch_at_boundary(start + 100s, true));
    pools.healthy(0, start + 3s);
    pools.healthy(0, start + 4s);
    REQUIRE(pools.entries()[0].healthy_since == start + 3s);
    REQUIRE_FALSE(pools.switch_at_boundary(start + 12999ms, true));
    REQUIRE_FALSE(pools.switch_at_boundary(start + 13s, false));
    REQUIRE(pools.active() == 2);
    REQUIRE(pools.switch_at_boundary(start + 13s, true) == 0);
    REQUIRE(pools.active() == 0);
    REQUIRE_FALSE(pools.probe());
    REQUIRE(pools.entries()[0].backoff == 0s);
    REQUIRE(pools.entries()[2].health == jd_health::available);
    REQUIRE_FALSE(pools.entries()[2].healthy_since);
    REQUIRE_FALSE(pools.begin_probe(start + 100s));
}

TEST_CASE("sv2 jd probe failures preserve the active entry and respect probe pacing", "[sv2 jd]") {
    jd_failover pools(3, limits());
    REQUIRE(pools.begin(start) == 0);
    pools.fail(0, jd_failure::transient, "drop", start);
    REQUIRE(pools.begin(start) == 1);
    pools.fail(1, jd_failure::transient, "drop", start);
    REQUIRE(pools.begin(start) == 2);
    REQUIRE(pools.begin_probe(start + 2s) == 0);
    pools.fail(0, jd_failure::transient, "probe failed", start + 2s);
    REQUIRE(pools.active() == 2);
    REQUIRE_FALSE(pools.probe());
    REQUIRE_FALSE(pools.begin_probe(start + 4s));
    REQUIRE(pools.begin_probe(start + 5s) == 1);
    pools.healthy(1, start + 5s);
    pools.fail(2, jd_failure::transient, "active failed", start + 5s);
    REQUIRE_FALSE(pools.active());
    REQUIRE(pools.probe() == 1);
    REQUIRE_FALSE(pools.begin(start + 5s));
    REQUIRE_FALSE(pools.switch_at_boundary(start + 14s, true));
    REQUIRE(pools.switch_at_boundary(start + 15s, true) == 1);
}

TEST_CASE("sv2 jd recovery never promotes a probe below a newly recovered higher priority active entry", "[sv2 jd]") {
    jd_failover pools(3, limits());
    REQUIRE(pools.begin(start) == 0);
    pools.fail(0, jd_failure::authority, "authority", start);
    REQUIRE(pools.begin(start) == 1);
    pools.fail(1, jd_failure::transient, "drop", start);
    REQUIRE(pools.begin(start) == 2);
    REQUIRE(pools.begin_probe(start + 2s) == 1);
    pools.healthy(1, start + 2s);
    pools.fail(2, jd_failure::transient, "drop", start + 60s);
    REQUIRE(pools.begin(start + 60s) == 0);
    REQUIRE_FALSE(pools.switch_at_boundary(start + 60s, true));
    REQUIRE(pools.active() == 0);
    pools.release_probe();
    REQUIRE_FALSE(pools.probe());
    REQUIRE(pools.entries()[1].health == jd_health::available);
    REQUIRE_FALSE(pools.entries()[1].healthy_since);
    REQUIRE(pools.entries()[1].last_error == "drop");
    REQUIRE_FALSE(pools.begin_probe(start + 60s));
}

TEST_CASE("sv2 jd interrupted probe health restarts the recovery hold and retains its backoff", "[sv2 jd]") {
    jd_failover pools(3, limits());
    REQUIRE(pools.begin(start) == 0);
    pools.fail(0, jd_failure::transient, "drop", start);
    REQUIRE(pools.begin(start) == 1);
    pools.fail(1, jd_failure::transient, "drop", start);
    REQUIRE(pools.begin(start) == 2);
    pools.healthy(2, start);
    REQUIRE(pools.begin_probe(start + 2s) == 0);
    pools.healthy(0, start + 2s);
    pools.fail(0, jd_failure::transient, "probe dropped during hold", start + 11s);
    REQUIRE_FALSE(pools.entries()[0].healthy_since);
    REQUIRE(pools.entries()[0].backoff == 4s);
    REQUIRE(pools.entries()[0].retry_at == start + 15s);
    REQUIRE(pools.entries()[1].backoff == 2s);
    REQUIRE(pools.active() == 2);
    REQUIRE(pools.begin_probe(start + 11s) == 1);
    pools.release_probe();
    REQUIRE_FALSE(pools.begin_probe(start + 13s));
    REQUIRE(pools.begin_probe(start + 15s) == 0);
    pools.healthy(0, start + 15s);
    REQUIRE_FALSE(pools.switch_at_boundary(start + 24s, true));
    REQUIRE_FALSE(pools.switch_at_boundary(start + 25s, false));
    REQUIRE(pools.switch_at_boundary(start + 25s, true) == 0);
    REQUIRE(pools.entries()[0].backoff == 0s);
}

TEST_CASE("sv2 jd recovering from solo preserves the hold and job boundary", "[sv2 jd]") {
    jd_failover pools(2, limits());
    REQUIRE(pools.begin(start) == 0);
    pools.fail(0, jd_failure::authority, "authority mismatch", start);
    REQUIRE(pools.begin(start) == 1);
    pools.fail(1, jd_failure::transient, "drop", start);
    REQUIRE_FALSE(pools.active());
    REQUIRE_FALSE(pools.begin_probe(start + 1s));
    REQUIRE(pools.begin_probe(start + 2s) == 1);
    REQUIRE_FALSE(pools.begin(start + 2s));
    pools.healthy(1, start + 2s);
    REQUIRE_FALSE(pools.switch_at_boundary(start + 11999ms, true));
    REQUIRE_FALSE(pools.active());
    REQUIRE_FALSE(pools.switch_at_boundary(start + 12s, false));
    REQUIRE(pools.switch_at_boundary(start + 12s, true) == 1);
    REQUIRE(pools.active() == 1);
    REQUIRE_FALSE(pools.probe());
    REQUIRE(pools.entries()[0].health == jd_health::authority_hold);
}

TEST_CASE("sv2 jd health thresholds use a bounded full share window and keep exact limits", "[sv2 jd]") {
    jd_health_window window(limits());
    REQUIRE_FALSE(window.sample(true, false, 100ms));
    REQUIRE_FALSE(window.sample(false, true, 100ms));
    REQUIRE_FALSE(window.sample(false, false, 100ms));
    REQUIRE_FALSE(window.sample(false, false, 100ms));
    REQUIRE_FALSE(window.sample(false, false, 100ms));
    REQUIRE_FALSE(window.sample(false, false, 100ms));
    REQUIRE_FALSE(window.sample(false, true, 100ms));
    auto const rejected = window.sample(false, true, 100ms);
    REQUIRE(rejected);
    REQUIRE(*rejected == "upstream rejected share rate exceeded threshold");

    jd_health_window stale(limits());
    REQUIRE_FALSE(stale.sample(true, false, 0ms));
    REQUIRE_FALSE(stale.sample(true, false, 0ms));
    REQUIRE_FALSE(stale.sample(false, false, 0ms));
    auto const reason = stale.sample(false, false, 0ms);
    REQUIRE(reason);
    REQUIRE(*reason == "upstream stale share rate exceeded threshold");

    jd_health_window latency(limits());
    auto const slow = latency.sample(false, false, 101ms);
    REQUIRE(slow);
    REQUIRE(*slow == "upstream latency exceeded threshold");
    REQUIRE_FALSE(latency.sample(false, false, 100ms));
}

TEST_CASE("sv2 jd status names identify every health state", "[sv2 jd]") {
    REQUIRE(std::string(jd_health_name(jd_health::available)) == "available");
    REQUIRE(std::string(jd_health_name(jd_health::connecting)) == "connecting");
    REQUIRE(std::string(jd_health_name(jd_health::healthy)) == "healthy");
    REQUIRE(std::string(jd_health_name(jd_health::backoff)) == "backoff");
    REQUIRE(std::string(jd_health_name(jd_health::authority_hold)) == "authority-hold");
    REQUIRE(std::string(jd_health_name(jd_health::disabled)) == "disabled-until-restart");
    REQUIRE(std::string(jd_health_name(jd_health::dishonest)) == "dishonest");
    REQUIRE(std::string(jd_health_name(static_cast<jd_health>(-1))) == "unknown");
}
