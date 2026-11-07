// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_failover.hpp>

#include <algorithm>
#include <utility>

namespace kth::node::sv2 {

jd_health_window::jd_health_window(sv2_settings::jd_settings const& settings)
    : size_(settings.health_window)
    , max_stale_(settings.max_stale_percent)
    , max_reject_(settings.max_reject_percent)
    , max_latency_(settings.max_latency_milliseconds)
{}

std::optional<std::string> jd_health_window::sample(bool stale, bool rejected,
    std::chrono::milliseconds latency) {
    if (latency > max_latency_) return "upstream latency exceeded threshold";
    if (samples_.size() == size_) {
        stale_ -= samples_.front().stale;
        rejected_ -= samples_.front().rejected;
        samples_.pop_front();
    }
    samples_.push_back({stale, rejected});
    stale_ += stale;
    rejected_ += rejected;
    if (samples_.size() < size_) return std::nullopt;
    if (stale_ * 100 > size_t{size_} * max_stale_) return "upstream stale share rate exceeded threshold";
    if (rejected_ * 100 > size_t{size_} * max_reject_) return "upstream rejected share rate exceeded threshold";
    return std::nullopt;
}

char const* jd_health_name(jd_health health) {
    switch (health) {
        case jd_health::available: return "available";
        case jd_health::connecting: return "connecting";
        case jd_health::healthy: return "healthy";
        case jd_health::backoff: return "backoff";
        case jd_health::authority_hold: return "authority-hold";
        case jd_health::disabled: return "disabled-until-restart";
        case jd_health::dishonest: return "dishonest";
    }
    return "unknown";
}

jd_failover::jd_failover(size_t count, sv2_settings::jd_settings const& settings)
    : entries_(count)
    , retry_initial_(settings.retry_initial_seconds)
    , retry_max_(settings.retry_max_seconds)
    , authority_hold_(settings.authority_hold_seconds)
    , recovery_hold_(settings.recovery_hold_seconds)
    , probe_interval_(settings.probe_interval_seconds)
{}

bool jd_failover::eligible(size_t index, time_point now) const {
    auto const& value = entries_[index];
    return value.health != jd_health::disabled && value.retry_at <= now;
}

std::optional<size_t> jd_failover::begin(time_point now) {
    if (active_) return std::nullopt;
    for (size_t index = 0; index < entries_.size(); ++index) {
        if (probe_ == index || ! eligible(index, now)) continue;
        active_ = index;
        entries_[index].health = jd_health::connecting;
        return index;
    }
    return std::nullopt;
}

std::optional<size_t> jd_failover::begin_probe(time_point now) {
    if (probe_ || now < next_probe_) return std::nullopt;
    auto const end = active_.value_or(entries_.size());
    for (size_t index = 0; index < end; ++index) {
        if ( ! eligible(index, now)) continue;
        probe_ = index;
        entries_[index].health = jd_health::connecting;
        next_probe_ = now + probe_interval_;
        return index;
    }
    return std::nullopt;
}

void jd_failover::release_probe() {
    if ( ! probe_) return;
    auto& value = entries_[*probe_];
    value.health = jd_health::available;
    value.healthy_since.reset();
    probe_.reset();
}

void jd_failover::healthy(size_t index, time_point now) {
    auto& value = entries_[index];
    // Repeated readiness notifications keep the start of uninterrupted health.
    if ( ! value.healthy_since) value.healthy_since = now;
    value.health = jd_health::healthy;
}

void jd_failover::fail(size_t index, jd_failure failure, std::string error, time_point now) {
    auto& value = entries_[index];
    // Setup alone is not recovery: a peer that repeatedly opens and drops must
    // keep its growing backoff until it has stayed healthy through the hold.
    if (value.healthy_since && now - *value.healthy_since >= recovery_hold_) {
        value.backoff = std::chrono::seconds::zero();
    }
    value.backoff = value.backoff == std::chrono::seconds::zero()
        ? retry_initial_ : std::min(retry_max_, value.backoff * 2);
    value.healthy_since.reset();
    value.last_error = std::move(error);
    value.retry_at = now + value.backoff;
    switch (failure) {
        case jd_failure::transient:
            value.health = jd_health::backoff;
            break;
        case jd_failure::authority:
            value.health = jd_health::authority_hold;
            value.retry_at = now + std::max(value.backoff, authority_hold_);
            break;
        case jd_failure::incompatible:
            value.health = jd_health::disabled;
            value.retry_at = time_point::max();
            break;
        case jd_failure::dishonest:
            value.health = jd_health::dishonest;
            value.retry_at = now + std::max(value.backoff, authority_hold_);
            break;
    }
    if (active_ == index) active_.reset();
    if (probe_ == index) probe_.reset();
}

std::optional<size_t> jd_failover::switch_at_boundary(time_point now, bool boundary) {
    if ( ! boundary || ! probe_) return std::nullopt;
    auto& candidate = entries_[*probe_];
    if ( ! candidate.healthy_since || now - *candidate.healthy_since < recovery_hold_
        || (active_ && *active_ < *probe_)) return std::nullopt;
    if (active_) {
        auto& previous = entries_[*active_];
        previous.health = jd_health::available;
        previous.healthy_since.reset();
    }
    active_ = probe_;
    probe_.reset();
    candidate.backoff = std::chrono::seconds::zero();
    return active_;
}

std::optional<size_t> jd_failover::active() const {
    return active_;
}

std::optional<size_t> jd_failover::probe() const {
    return probe_;
}

std::vector<jd_failover::entry> const& jd_failover::entries() const {
    return entries_;
}

jd_status jd_failover::snapshot() const {
    return {active_, probe_, entries_};
}

} // namespace kth::node::sv2
