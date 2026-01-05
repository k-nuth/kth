// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/banlist.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

#include <spdlog/spdlog.h>

namespace kth::network {

// =============================================================================
// Construction
// =============================================================================

banlist::banlist(kth::path file_path)
    : file_path_(std::move(file_path))
{}

// =============================================================================
// Ban/Unban operations
// =============================================================================

void banlist::ban(
    ::asio::ip::address const& ip,
    std::chrono::seconds duration,
    ban_reason reason)
{
    ban_entry entry;
    entry.create_time = clock::now();
    entry.ban_until = entry.create_time + duration;
    entry.reason = reason;

    bans_.insert_or_assign(ip, entry);

    spdlog::info("[banlist] Banned {} for {}s, reason: {}",
        ip.to_string(), duration.count(), to_string(reason));
}

void banlist::ban(
    infrastructure::config::authority const& authority,
    std::chrono::seconds duration,
    ban_reason reason)
{
    ban(authority.asio_ip(), duration, reason);
}

bool banlist::unban(::asio::ip::address const& ip) {
    auto const erased = bans_.erase(ip);
    if (erased > 0) {
        spdlog::info("[banlist] Unbanned {}", ip.to_string());
        return true;
    }
    return false;
}

bool banlist::unban(infrastructure::config::authority const& authority) {
    return unban(authority.asio_ip());
}

// =============================================================================
// Query operations
// =============================================================================

bool banlist::is_banned(::asio::ip::address const& ip) const {
    bool banned = false;
    bans_.cvisit(ip, [&banned](auto const& entry) {
        banned = ! entry.second.is_expired();
    });
    return banned;
}

bool banlist::is_banned(infrastructure::config::authority const& authority) const {
    return is_banned(authority.asio_ip());
}

std::optional<ban_entry> banlist::get_ban(::asio::ip::address const& ip) const {
    std::optional<ban_entry> result;
    bans_.cvisit(ip, [&result](auto const& entry) {
        if ( ! entry.second.is_expired()) {
            result = entry.second;
        }
    });
    return result;
}

size_t banlist::size() const {
    return bans_.size();
}

void banlist::clear() {
    bans_.clear();
    spdlog::info("[banlist] Cleared all bans");
}

void banlist::sweep_expired() {
    size_t removed = 0;

    bans_.erase_if([&removed](auto const& entry) {
        if (entry.second.is_expired()) {
            spdlog::debug("[banlist] Expired ban removed for {}", entry.first.to_string());
            ++removed;
            return true;
        }
        return false;
    });

    if (removed > 0) {
        spdlog::debug("[banlist] Swept {} expired bans", removed);
    }
}

std::vector<std::pair<std::string, ban_entry>> banlist::get_all_bans() const {
    std::vector<std::pair<std::string, ban_entry>> result;

    bans_.cvisit_all([&result](auto const& entry) {
        if ( ! entry.second.is_expired()) {
            result.emplace_back(entry.first.to_string(), entry.second);
        }
    });

    return result;
}

// =============================================================================
// Persistence
// =============================================================================

bool banlist::load() {
    if (file_path_.empty()) {
        return true;  // No persistence configured
    }

    std::ifstream file(file_path_);
    if ( ! file.is_open()) {
        // File doesn't exist yet - that's OK
        return true;
    }

    bans_.clear();

    std::string line;
    size_t loaded = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;  // Skip empty lines and comments
        }

        std::istringstream iss(line);
        std::string ip_str;
        int64_t create_time_epoch;
        int64_t ban_until_epoch;
        int reason_int;

        if (iss >> ip_str >> create_time_epoch >> ban_until_epoch >> reason_int) {
            std::error_code ec;
            auto const ip = ::asio::ip::make_address(ip_str, ec);
            if (ec) {
                spdlog::warn("[banlist] Invalid IP address in banlist file: {}", ip_str);
                continue;
            }

            ban_entry entry;
            entry.create_time = clock::time_point(std::chrono::seconds(create_time_epoch));
            entry.ban_until = clock::time_point(std::chrono::seconds(ban_until_epoch));
            entry.reason = ban_reason(reason_int);

            // Only load non-expired bans
            if ( ! entry.is_expired()) {
                bans_.insert_or_assign(ip, entry);
                ++loaded;
            }
        }
    }

    spdlog::info("[banlist] Loaded {} bans from {}", loaded, file_path_.string());
    return true;
}

bool banlist::save() const {
    if (file_path_.empty()) {
        return true;  // No persistence configured
    }

    std::ofstream file(file_path_);
    if ( ! file.is_open()) {
        spdlog::error("[banlist] Failed to open {} for writing", file_path_.string());
        return false;
    }

    file << "# Knuth banlist - format: IP create_time ban_until reason\n";

    size_t saved = 0;
    bans_.cvisit_all([&file, &saved](auto const& entry) {
        if ( ! entry.second.is_expired()) {
            auto const create_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                entry.second.create_time.time_since_epoch()).count();
            auto const until_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                entry.second.ban_until.time_since_epoch()).count();

            file << entry.first.to_string() << ' '
                 << create_epoch << ' '
                 << until_epoch << ' '
                 << int(entry.second.reason) << '\n';
            ++saved;
        }
    });

    spdlog::debug("[banlist] Saved {} bans to {}", saved, file_path_.string());
    return true;
}

} // namespace kth::network
