// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_FAILOVER_HPP
#define KTH_NODE_SV2_JD_FAILOVER_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include <kth/node/settings.hpp>

namespace kth::node::sv2 {

enum class jd_failure { transient, authority, incompatible, dishonest };
enum class jd_health { available, connecting, healthy, backoff, authority_hold, disabled, dishonest };

[[nodiscard]] char const* jd_health_name(jd_health health);

struct jd_entry_status {
    jd_health health = jd_health::available;
    std::string last_error;
    std::chrono::seconds backoff{0};
    std::chrono::steady_clock::time_point retry_at{};
    std::optional<std::chrono::steady_clock::time_point> healthy_since;
};

struct jd_status {
    std::optional<size_t> active;
    std::optional<size_t> probe;
    std::vector<jd_entry_status> entries;
};

struct jd_health_window {
    // Settings have passed validate_jd_settings.
    explicit jd_health_window(sv2_settings::jd_settings const& settings);

    [[nodiscard]] std::optional<std::string> sample(bool stale, bool rejected,
        std::chrono::milliseconds latency);

private:
    struct outcome {
        bool stale;
        bool rejected;
    };

    std::deque<outcome> samples_;
    size_t stale_ = 0;
    size_t rejected_ = 0;
    uint32_t const size_;
    uint32_t const max_stale_;
    uint32_t const max_reject_;
    std::chrono::milliseconds const max_latency_;
};

// The owner retains a successful probe's sockets throughout the hold. Time is
// supplied by that owner so neither selection nor recovery waits on a timer.
struct jd_failover {
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    using entry = jd_entry_status;

    // Settings have passed validate_jd_settings.
    jd_failover(size_t count, sv2_settings::jd_settings const& settings);

    // Reserve the first eligible entry. An existing active entry is retained.
    [[nodiscard]] std::optional<size_t> begin(time_point now);
    // A single higher-priority candidate may be connected alongside the active
    // entry. The returned reservation lasts until failure or promotion.
    [[nodiscard]] std::optional<size_t> begin_probe(time_point now);
    // A better active entry can make a retained probe unnecessary. Its owner
    // closes and joins those sockets before reserving another probe.
    void release_probe();
    // A followed Reconnect keeps the entry's reservation and backoff. Health
    // starts over on the new connection.
    void restart(size_t index);
    void healthy(size_t index, time_point now);
    void fail(size_t index, jd_failure failure, std::string error, time_point now);
    [[nodiscard]] std::optional<size_t> switch_at_boundary(time_point now, bool boundary);

    [[nodiscard]] std::optional<size_t> active() const;
    [[nodiscard]] std::optional<size_t> probe() const;
    [[nodiscard]] std::vector<entry> const& entries() const;
    [[nodiscard]] jd_status snapshot() const;

private:
    [[nodiscard]] bool eligible(size_t index, time_point now) const;

    std::vector<entry> entries_;
    std::optional<size_t> active_;
    std::optional<size_t> probe_;
    std::chrono::seconds const retry_initial_;
    std::chrono::seconds const retry_max_;
    std::chrono::seconds const authority_hold_;
    std::chrono::seconds const recovery_hold_;
    std::chrono::seconds const probe_interval_;
    time_point next_probe_{};
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_FAILOVER_HPP
