// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/peer_database.hpp>

#include <algorithm>
#include <fstream>
#include <random>
#include <system_error>

#include <fmt/format.h>
#include <simdjson.h>
#include <spdlog/spdlog.h>

#include <kth/domain/message/version.hpp>

namespace kth::network {

namespace {

using enum domain::message::version::service;

// BCHN-compatible service flag checks
// See: bchn/src/protocol.h MayHaveUsefulAddressDB and HasAllDesirableServiceFlags

/// Checks if a peer may have a useful address database (is a full or limited node)
/// BCHN: return (services & NODE_NETWORK) || (services & NODE_NETWORK_LIMITED);
bool may_have_useful_address_db(uint64_t services) {
    return (services & node_network) != 0
        || (services & node_network_limited) != 0;
}

/// Gets the desirable service flags based on current state
/// For simplicity, we always require NODE_NETWORK (full node)
/// BCHN has more complex logic depending on IBD state
uint64_t get_desirable_service_flags(uint64_t /*services*/) {
    return node_network;
}

/// Checks if a peer has all desirable service flags
/// BCHN: return !(GetDesirableServiceFlags(services) & (~services));
bool has_all_desirable_service_flags(uint64_t services) {
    auto const desired = get_desirable_service_flags(services);
    return (desired & (~services)) == 0;
}

// Helper to parse timestamp from epoch seconds
peer_database::clock::time_point parse_timestamp(int64_t epoch_secs) {
    return peer_database::clock::time_point(std::chrono::seconds(epoch_secs));
}

// Helper to format timestamp as epoch seconds
int64_t format_timestamp(peer_database::clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

// Escape string for JSON (handles quotes and backslashes)
std::string escape_json_string(std::string_view sv) {
    std::string result;
    result.reserve(sv.size());
    for (char c : sv) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

} // anonymous namespace

// =============================================================================
// Construction / Destruction
// =============================================================================

peer_database::peer_database(kth::path file_path, size_t capacity)
    : file_path_(std::move(file_path))
    , capacity_(capacity)
{}

peer_database::~peer_database() {
    (void)save();
}

// =============================================================================
// Basic Operations
// =============================================================================

size_t peer_database::size() const {
    return records_.size();
}

void peer_database::clear() {
    records_.clear();
    spdlog::info("[peer_database] Cleared all records");
}

bool peer_database::exists(infrastructure::config::authority const& auth) const {
    return records_.contains(normalized_address(auth.asio_ip()));
}

std::optional<peer_record> peer_database::get_by_ip(::asio::ip::address const& ip_param) const {
    normalized_address const ip(ip_param);
    std::optional<peer_record> result;

    records_.cvisit(ip, [&](auto const& entry) {
        result = entry.second;
    });

    return result;
}

std::optional<peer_record> peer_database::get(infrastructure::config::authority const& auth) const {
    return get_by_ip(auth.asio_ip());
}

peer_record peer_database::create_record(infrastructure::config::authority const& auth) {
    peer_record record;
    record.authority = auth;
    record.ip = normalized_address(auth);
    record.first_seen = clock::now();
    record.last_seen = record.first_seen;
    return record;
}

std::pair<peer_record, get_result> peer_database::get_or_create(infrastructure::config::authority const& auth) {
    auto const ip = normalized_address(auth.asio_ip());

    peer_record result;
    bool found = false;

    records_.cvisit(ip, [&](auto const& entry) {
        result = entry.second;
        found = true;
    });

    if (found) {
        return {result, get_result::existing};
    }

    result = create_record(auth);
    if (records_.size() < capacity_) {
        records_.insert_or_assign(ip, result);
        return {result, get_result::created};
    }

    return {result, get_result::created_not_stored};
}

bool peer_database::update(peer_record const& record) {
    bool updated = false;
    records_.visit(record.ip, [&](auto& entry) {
        entry.second = record;
        updated = true;
    });
    return updated;
}

bool peer_database::remove(infrastructure::config::authority const& auth) {
    return records_.erase(normalized_address(auth.asio_ip())) > 0;
}

std::pair<size_t, size_t> peer_database::count_by_status() const {
    size_t available = 0;
    size_t banned = 0;
    records_.cvisit_all([&](auto const& entry) {
        if (entry.second.is_banned()) {
            ++banned;
        } else {
            ++available;
        }
    });
    return {available, banned};
}

bool peer_database::store_address(domain::message::network_address const& addr) {
    auto const authority = infrastructure::config::authority(addr);

    // BCHN-compatible validations (see bchn/src/net_processing.cpp lines 2665-2691)

    // 1. Services check: We only bother storing full nodes
    // BCHN: if (!MayHaveUsefulAddressDB(addr.nServices) && !HasAllDesirableServiceFlags(addr.nServices)) continue;
    if (!may_have_useful_address_db(addr.services()) &&
        !has_all_desirable_service_flags(addr.services())) {
        spdlog::debug("[peer_database] Rejected address due to services: '{}' (services=0x{:x})",
            authority.to_string(), addr.services());
        return false;
    }

    // 2. Time validation/adjustment (BCHN: net_processing.cpp lines 2673-2674)
    // Adjust out-of-range timestamps to 5 days ago
    auto const now = clock::now();
    auto const now_epoch = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count());
    auto msg_timestamp = addr.timestamp();
    if (msg_timestamp <= 100000000 || msg_timestamp > now_epoch + 10 * 60) {
        msg_timestamp = now_epoch - 5 * 24 * 60 * 60;  // 5 days ago
    }
    auto const last_active = parse_timestamp(static_cast<int64_t>(msg_timestamp));

    // 3. Routable check: Do not store addresses outside our network
    // BCHN: if (fReachable) { vAddrOk.push_back(addr); }
    // We use is_routable() as a simplified check
    if (!addr.is_routable()) {
        spdlog::debug("[peer_database] Rejected non-routable address: '{}'", authority.to_string());
        return false;
    }

    auto const ip = normalized_address(authority.asio_ip());

    // 3. Banned check
    bool is_banned = false;
    records_.cvisit(ip, [&](auto const& entry) {
        if (entry.second.is_banned()) {
            is_banned = true;
        }
    });
    if (is_banned) {
        spdlog::debug("[peer_database] Rejected banned address: '{}'", authority.to_string());
        return false;
    }

    bool is_new = false;
    bool found = false;

    records_.visit(ip, [&](auto& entry) {
        // Update timestamps for existing record
        entry.second.last_seen = clock::now();
        // Update last_active if the new timestamp is more recent
        if (last_active > entry.second.last_active) {
            entry.second.last_active = last_active;
        }
        // Update address if we now have a port
        if (entry.second.authority.port() == 0 && authority.port() != 0) {
            entry.second.authority = authority;
        }
        // Update services if changed
        if (addr.services() != 0) {
            entry.second.services = addr.services();
        }
        found = true;
    });

    if (!found && records_.size() < capacity_) {
        peer_record record;
        record.authority = authority;
        record.ip = ip;
        record.services = addr.services();
        record.first_seen = clock::now();
        record.last_seen = record.first_seen;
        record.last_active = last_active;
        records_.insert_or_assign(ip, record);
        is_new = true;
    }

    return is_new;
}

code peer_database::fetch_address(domain::message::network_address& out) const {
    // Collect non-banned addresses
    std::vector<infrastructure::config::authority> available;
    available.reserve(256);  // Reasonable pre-allocation

    records_.cvisit_all([&](auto const& entry) {
        if (!entry.second.is_banned() && entry.second.authority.port() != 0) {
            available.push_back(entry.second.authority);
        }
    });

    if (available.empty()) {
        return error::not_found;
    }

    // Random selection
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, available.size() - 1);
    auto const& selected = available[dist(rng)];

    // Convert to network_address
    out = domain::message::network_address{
        0,  // timestamp
        0,  // services (unknown)
        selected.ip(),
        selected.port()
    };

    return error::success;
}

bool peer_database::remove_address(domain::message::network_address const& addr) {
    auto const authority = infrastructure::config::authority(addr);
    return remove(authority);
}

// =============================================================================
// Ban Operations
// =============================================================================

bool peer_database::is_banned(::asio::ip::address const& ip_param) const {
    auto const ip = normalized_address(ip_param);
    bool banned = false;

    records_.cvisit(ip, [&](auto const& entry) {
        banned = entry.second.is_banned();
    });

    return banned;
}

bool peer_database::is_banned(infrastructure::config::authority const& auth) const {
    return is_banned(auth.asio_ip());
}

void peer_database::ban(::asio::ip::address const& ip_param,
                        std::chrono::seconds duration,
                        ban_reason reason)
{
    auto const ip = normalized_address(ip_param);
    bool found = false;
    records_.visit(ip, [&](auto& entry) {
        entry.second.set_banned(duration, reason);
        found = true;
    });

    if (!found && records_.size() < capacity_) {
        peer_record record;
        record.authority = infrastructure::config::authority(ip, 0);
        record.ip = ip;
        record.first_seen = clock::now();
        record.last_seen = record.first_seen;
        record.set_banned(duration, reason);
        records_.insert_or_assign(ip, record);
    }

    auto const duration_secs = duration.count();
    if (duration_secs >= 365 * 24 * 60 * 60) {
        spdlog::debug("[peer_database] Banned {} permanently, reason: {}",
            ip.to_string(), to_string(reason));
    } else if (duration_secs >= 24 * 60 * 60) {
        spdlog::debug("[peer_database] Banned {} for {} days, reason: {}",
            ip.to_string(), duration_secs / (24 * 60 * 60), to_string(reason));
    } else if (duration_secs >= 60 * 60) {
        spdlog::debug("[peer_database] Banned {} for {} hours, reason: {}",
            ip.to_string(), duration_secs / (60 * 60), to_string(reason));
    } else {
        spdlog::debug("[peer_database] Banned {} for {}s, reason: {}",
            ip.to_string(), duration_secs, to_string(reason));
    }

    if (!save()) {
        spdlog::warn("[peer_database] Failed to persist ban for {}", ip.to_string());
    }
}

void peer_database::ban(infrastructure::config::authority const& auth,
                        std::chrono::seconds duration,
                        ban_reason reason)
{
    auto const ip = normalized_address(auth.asio_ip());
    bool found = false;

    records_.visit(ip, [&](auto& entry) {
        if (entry.second.authority.port() == 0 && auth.port() != 0) {
            entry.second.authority = auth;
        }
        entry.second.set_banned(duration, reason);
        found = true;
    });

    if (!found && records_.size() < capacity_) {
        peer_record record;
        record.authority = auth;
        record.ip = ip;
        record.first_seen = clock::now();
        record.last_seen = record.first_seen;
        record.set_banned(duration, reason);
        records_.insert_or_assign(ip, record);
    }

    auto const duration_secs = duration.count();
    if (duration_secs >= 365 * 24 * 60 * 60) {
        spdlog::debug("[peer_database] Banned {} permanently, reason: {}",
            auth.to_string(), to_string(reason));
    } else if (duration_secs >= 24 * 60 * 60) {
        spdlog::debug("[peer_database] Banned {} for {} days, reason: {}",
            auth.to_string(), duration_secs / (24 * 60 * 60), to_string(reason));
    } else if (duration_secs >= 60 * 60) {
        spdlog::debug("[peer_database] Banned {} for {} hours, reason: {}",
            auth.to_string(), duration_secs / (60 * 60), to_string(reason));
    } else {
        spdlog::debug("[peer_database] Banned {} for {}s, reason: {}",
            auth.to_string(), duration_secs, to_string(reason));
    }

    if (!save()) {
        spdlog::warn("[peer_database] Failed to persist ban for {}", auth.to_string());
    }
}

bool peer_database::unban(::asio::ip::address const& ip_param) {
    auto const ip = normalized_address(ip_param);
    bool found = false;

    records_.visit(ip, [&](auto& entry) {
        if (entry.second.ban.has_value()) {
            entry.second.clear_ban();
            found = true;
        }
    });

    if (found) {
        spdlog::info("[peer_database] Unbanned {}", ip.to_string());
        if (!save()) {
            spdlog::warn("[peer_database] Failed to persist unban for {}", ip.to_string());
        }
    }

    return found;
}

bool peer_database::unban(infrastructure::config::authority const& auth) {
    return unban(auth.asio_ip());
}

std::vector<peer_record> peer_database::get_banned() const {
    std::vector<peer_record> result;

    records_.cvisit_all([&](auto const& entry) {
        if (entry.second.is_banned()) {
            result.push_back(entry.second);
        }
    });

    return result;
}

void peer_database::sweep_expired_bans() {
    size_t cleared = 0;

    records_.visit_all([&](auto& entry) {
        if (entry.second.ban.has_value() && entry.second.ban->is_expired()) {
            entry.second.clear_ban();
            ++cleared;
        }
    });

    if (cleared > 0) {
        spdlog::debug("[peer_database] Cleared {} expired bans", cleared);
    }
}

// =============================================================================
// Reputation Operations
// =============================================================================

bool peer_database::add_misbehavior(infrastructure::config::authority const& auth,
                                    int score,
                                    int ban_threshold)
{
    auto const ip = normalized_address(auth.asio_ip());
    bool should_ban = false;
    bool already_banned = false;

    bool found = false;
    records_.visit(ip, [&](auto& entry) {
        already_banned = entry.second.is_banned();
        should_ban = entry.second.add_misbehavior(score, ban_threshold);
        found = true;
    });

    if (!found && records_.size() < capacity_) {
        peer_record record;
        record.authority = auth;
        record.ip = ip;
        record.first_seen = clock::now();
        record.last_seen = record.first_seen;
        should_ban = record.add_misbehavior(score, ban_threshold);
        records_.insert_or_assign(ip, record);
    }

    // Auto-ban when threshold is reached (only if not already banned)
    if (should_ban && !already_banned) {
        ban(auth, std::chrono::hours{24}, ban_reason::node_misbehaving);
        spdlog::info("[peer_database] Auto-banned {} for misbehavior (score >= {})",
            auth.to_string(), ban_threshold);
        return true;  // Was just banned
    }

    return false;  // Not banned (either below threshold or already banned)
}

void peer_database::decay_all_reputation(int amount) {
    records_.visit_all([amount](auto& entry) {
        entry.second.decay_reputation(amount);
    });
}

// =============================================================================
// Performance Operations
// =============================================================================

void peer_database::record_block_download(infrastructure::config::authority const& auth,
                                          uint32_t blocks,
                                          uint32_t time_ms)
{
    auto const ip = normalized_address(auth.asio_ip());

    bool found = false;
    records_.visit(ip, [&](auto& entry) {
        entry.second.performance.record(blocks, time_ms);
        entry.second.last_seen = clock::now();
        found = true;
    });

    if (!found && records_.size() < capacity_) {
        peer_record record;
        record.authority = auth;
        record.ip = ip;
        record.first_seen = clock::now();
        record.last_seen = record.first_seen;
        record.performance.record(blocks, time_ms);
        records_.insert_or_assign(ip, record);
    }
}

double peer_database::get_median_performance() const {
    std::vector<double> avgs;

    records_.cvisit_all([&](auto const& entry) {
        if (entry.second.performance.sample_count > 0) {
            avgs.push_back(entry.second.performance.avg_ms_per_block());
        }
    });

    if (avgs.empty()) {
        return 0.0;
    }

    std::sort(avgs.begin(), avgs.end());
    return avgs[avgs.size() / 2];
}

// =============================================================================
// Query Operations
// =============================================================================

std::vector<infrastructure::config::authority> peer_database::get_connectable(size_t max_count) const {
    std::vector<std::pair<infrastructure::config::authority, peer_database::clock::time_point>> candidates;

    records_.cvisit_all([&](auto const& entry) {
        auto const& record = entry.second;

        if (record.is_banned()) return;
        if (record.is_bad_for_sync()) return;
        if (record.authority.port() == 0) return;

        candidates.emplace_back(record.authority, record.last_seen);
    });

    std::sort(candidates.begin(), candidates.end(),
        [](auto const& a, auto const& b) { return a.second > b.second; });

    std::vector<infrastructure::config::authority> result;
    result.reserve(std::min(max_count, candidates.size()));

    for (size_t i = 0; i < max_count && i < candidates.size(); ++i) {
        result.push_back(candidates[i].first);
    }

    return result;
}

std::vector<infrastructure::config::authority> peer_database::get_bad_peers() const {
    std::vector<infrastructure::config::authority> result;

    records_.cvisit_all([&](auto const& entry) {
        if (entry.second.is_banned() || entry.second.is_bad_for_sync()) {
            result.push_back(entry.second.authority);
        }
    });

    return result;
}

void peer_database::visit_all(std::function<void(peer_record const&)> visitor) const {
    records_.cvisit_all([&](auto const& entry) {
        visitor(entry.second);
    });
}

// =============================================================================
// Persistence - JSON Lines format using simdjson (read) and fmt (write)
// =============================================================================

bool peer_database::load() {
    if (file_path_.empty()) {
        return true;
    }

    std::ifstream file(file_path_);
    if (!file.is_open()) {
        return true;  // File doesn't exist yet
    }

    records_.clear();

    simdjson::ondemand::parser parser;
    std::string line;
    size_t loaded = 0;
    size_t errors = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        auto record = parse_line(line, parser);
        if (record) {
            auto const ip = normalized_address(record->authority.asio_ip());
            records_.insert_or_assign(ip, *record);
            ++loaded;
        } else {
            ++errors;
        }
    }

    spdlog::info("[peer_database] Loaded {} peers from {} ({} errors)",
        loaded, file_path_.string(), errors);

    return true;
}

bool peer_database::save() const {
    if (file_path_.empty()) {
        return true;
    }

    std::ofstream file(file_path_);
    if ( ! file.is_open()) {
        spdlog::error("[peer_database] Failed to open {} for writing", file_path_.string());
        return false;
    }

    file << "# Knuth peer database - JSON Lines format\n";

    size_t saved = 0;

    records_.cvisit_all([&](auto const& entry) {
        file << format_record(entry.second) << '\n';
        ++saved;
    });

    spdlog::debug("[peer_database] Saved {} peers to {}", saved, file_path_.string());
    return true;
}

std::optional<peer_record> peer_database::parse_line(std::string_view line, simdjson::ondemand::parser& parser) const {
    // Need mutable padded string for simdjson
    simdjson::padded_string padded(line);

    auto doc = parser.iterate(padded);
    if (doc.error()) {
        return std::nullopt;
    }

    peer_record record;

    // Required: endpoint (IP:port) - also accept legacy "address" key
    auto endpoint_result = doc["endpoint"].get_string();
    if (endpoint_result.error()) {
        // Fallback to legacy "address" key
        endpoint_result = doc["address"].get_string();
        if (endpoint_result.error()) {
            return std::nullopt;
        }
    }
    record.authority = infrastructure::config::authority(std::string(endpoint_result.value()));
    if (record.authority.port() == 0) {
        return std::nullopt;
    }
    record.ip = normalized_address(record.authority);

    // Optional fields with defaults
    auto get_int64 = [&](const char* key, int64_t default_val) -> int64_t {
        auto result = doc[key].get_int64();
        return result.error() ? default_val : result.value();
    };

    auto get_uint64 = [&](const char* key, uint64_t default_val) -> uint64_t {
        auto result = doc[key].get_uint64();
        return result.error() ? default_val : result.value();
    };

    auto get_string = [&](const char* key) -> std::string {
        auto result = doc[key].get_string();
        return result.error() ? std::string{} : std::string(result.value());
    };

    record.user_agent = get_string("user_agent");
    record.services = get_uint64("services", 0);
    record.first_seen = parse_timestamp(get_int64("first_seen", 0));
    record.last_seen = parse_timestamp(get_int64("last_seen", 0));
    record.last_active = parse_timestamp(get_int64("last_active", 0));
    record.last_attempt = parse_timestamp(get_int64("last_attempt", 0));
    record.last_success = parse_timestamp(get_int64("last_success", 0));
    record.reputation_score = static_cast<int>(get_int64("reputation", 0));

    // Ban info
    auto ban_until = get_int64("ban_until", 0);
    if (ban_until > 0) {
        auto ban_reason_val = static_cast<int>(get_int64("ban_reason", 0));
        record.ban = ban_info{
            .banned_until = parse_timestamp(ban_until),
            .reason = static_cast<ban_reason>(ban_reason_val)
        };
    }

    // Performance
    record.performance.total_blocks = get_uint64("perf_blocks", 0);
    record.performance.total_time_ms = get_uint64("perf_time_ms", 0);
    record.performance.sample_count = static_cast<uint32_t>(get_uint64("perf_samples", 0));

    // Connection stats
    record.connection_attempts = static_cast<uint32_t>(get_uint64("conn_attempts", 0));
    record.connection_successes = static_cast<uint32_t>(get_uint64("conn_successes", 0));
    record.connection_failures = static_cast<uint32_t>(get_uint64("conn_failures", 0));

    return record;
}

std::string peer_database::format_record(peer_record const& record) const {
    return fmt::format(
        R"({{"endpoint":"{}","user_agent":"{}","services":{},"first_seen":{},"last_seen":{},"last_active":{},"last_attempt":{},"last_success":{},"reputation":{},"ban_until":{},"ban_reason":{},"perf_blocks":{},"perf_time_ms":{},"perf_samples":{},"conn_attempts":{},"conn_successes":{},"conn_failures":{}}})",
        record.authority.to_string(),
        escape_json_string(record.user_agent),
        record.services,
        format_timestamp(record.first_seen),
        format_timestamp(record.last_seen),
        format_timestamp(record.last_active),
        format_timestamp(record.last_attempt),
        format_timestamp(record.last_success),
        record.reputation_score,
        record.ban ? format_timestamp(record.ban->banned_until) : 0,
        record.ban ? static_cast<int>(record.ban->reason) : 0,
        record.performance.total_blocks,
        record.performance.total_time_ms,
        record.performance.sample_count,
        record.connection_attempts,
        record.connection_successes,
        record.connection_failures
    );
}

size_t peer_database::import_hosts_cache(kth::path const& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        spdlog::debug("[peer_database] No hosts.cache file found at {}", file_path.string());
        return 0;
    }

    std::string line;
    size_t imported = 0;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        try {
            infrastructure::config::authority address(line);
            if (address.port() == 0) continue;

            auto const ip = normalized_address(address.asio_ip());

            if (!records_.contains(ip) && records_.size() < capacity_) {
                peer_record record;
                record.authority = address;
                record.ip = ip;
                record.first_seen = clock::now();
                record.last_seen = record.first_seen;
                records_.insert_or_assign(ip, record);
                ++imported;
            }
        } catch (...) {
            // Invalid address format, skip
            continue;
        }
    }

    spdlog::info("[peer_database] Imported {} addresses from hosts.cache", imported);
    return imported;
}

size_t peer_database::import_banlist(kth::path const& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        spdlog::debug("[peer_database] No banlist.dat file found at {}", file_path.string());
        return 0;
    }

    simdjson::ondemand::parser parser;
    std::string line;
    size_t imported = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        // Try to parse as JSON first (new format)
        bool parsed_as_json = false;
        if (line[0] == '{') {
            simdjson::padded_string padded(line);
            auto doc = parser.iterate(padded);

            auto ip_result = doc["ip"].get_string();
            auto ban_until_result = doc["ban_until"].get_int64();
            auto reason_result = doc["reason"].get_int64();

            if (!ip_result.error() && !ban_until_result.error()) {
                std::error_code ec;
                auto ip_raw = ::asio::ip::make_address(std::string(ip_result.value()), ec);
                if (!ec) {
                    auto const ip = normalized_address(ip_raw);
                    auto ban_until = parse_timestamp(ban_until_result.value());
                    if (clock::now() < ban_until) {  // Not expired
                        peer_record record;
                        record.authority = infrastructure::config::authority(ip, 0);
                        record.ip = ip;
                        record.first_seen = clock::now();
                        record.last_seen = record.first_seen;
                        record.ban = ban_info{
                            .banned_until = ban_until,
                            .reason = static_cast<ban_reason>(reason_result.error() ? 0 : reason_result.value())
                        };

                        bool found = false;
                        records_.visit(ip, [&](auto& entry) {
                            entry.second.ban = record.ban;
                            found = true;
                        });
                        if (!found && records_.size() < capacity_) {
                            records_.insert_or_assign(ip, record);
                        }
                        ++imported;
                        parsed_as_json = true;
                    }
                }
            }
        }

        if (!parsed_as_json) {
            // Legacy text format: "IP create_time ban_until reason"
            // Simple parsing without streams
            size_t pos = 0;
            size_t space1 = line.find(' ', pos);
            if (space1 == std::string::npos) continue;

            std::string ip_str = line.substr(pos, space1 - pos);
            pos = space1 + 1;

            size_t space2 = line.find(' ', pos);
            if (space2 == std::string::npos) continue;
            // Skip create_time
            pos = space2 + 1;

            size_t space3 = line.find(' ', pos);
            if (space3 == std::string::npos) continue;

            std::string ban_until_str = line.substr(pos, space3 - pos);
            pos = space3 + 1;

            std::string reason_str = line.substr(pos);

            std::error_code ec;
            auto ip_raw = ::asio::ip::make_address(ip_str, ec);
            if (ec) continue;

            auto const ip = normalized_address(ip_raw);
            int64_t ban_until_epoch = 0;
            auto [ptr1, ec1] = std::from_chars(ban_until_str.data(), ban_until_str.data() + ban_until_str.size(), ban_until_epoch);
            if (ec1 != std::errc{}) continue;

            auto ban_until = parse_timestamp(ban_until_epoch);
            if (clock::now() >= ban_until) continue;

            int reason_int = 0;
            std::from_chars(reason_str.data(), reason_str.data() + reason_str.size(), reason_int);

            peer_record record;
            record.authority = infrastructure::config::authority(ip, 0);
            record.ip = ip;
            record.first_seen = clock::now();
            record.last_seen = record.first_seen;
            record.ban = ban_info{
                .banned_until = ban_until,
                .reason = static_cast<ban_reason>(reason_int)
            };

            bool found = false;
            records_.visit(ip, [&](auto& entry) {
                entry.second.ban = record.ban;
                found = true;
            });
            if (!found && records_.size() < capacity_) {
                records_.insert_or_assign(ip, record);
            }
            ++imported;
        }
    }

    spdlog::info("[peer_database] Imported {} bans from banlist.dat", imported);
    return imported;
}

} // namespace kth::network
