// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/hosts.hpp>

#include <algorithm>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include <kth/domain.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

namespace {

// Check if address is IPv4-mapped in IPv6 (::ffff:x.x.x.x).
// Bitcoin protocol stores all IPs as 16-byte IPv6, with IPv4 mapped as ::ffff:x.x.x.x.
inline bool is_ipv4_mapped(infrastructure::message::ip_address const& ip) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0 &&
           ip[4] == 0 && ip[5] == 0 && ip[6] == 0 && ip[7] == 0 &&
           ip[8] == 0 && ip[9] == 0 && ip[10] == 0xff && ip[11] == 0xff;
}

} // anonymous namespace

hosts::hosts(settings const& settings)
    : capacity_(std::min(max_address, size_t(settings.host_pool_capacity)))
    , disabled_(capacity_ == 0)
    , use_ipv6_(settings.use_ipv6)
    , file_path_(settings.hosts_file)
    , addresses_(capacity_ > 0 ? capacity_ : 1)
{
    if ( ! disabled_) {
        load_from_file();
    }
}

hosts::~hosts() {
    if ( ! disabled_) {
        save_to_file();
    }
}

void hosts::load_from_file() {
    kth::ifstream file(file_path_.string());
    if (file.bad()) {
        spdlog::debug("[hosts] No hosts file found or error reading.");
        return;
    }

    std::string line;
    size_t loaded = 0;

    while (std::getline(file, line)) {
        auto parsed = infrastructure::config::authority::parse_from(line);
        if ( ! parsed || parsed->port() == 0) {
            continue;
        }
        auto const addr = parsed->to_network_address();
        // Filter: routable + (IPv4 always, IPv6 when enabled)
        if (addr.is_routable() && (use_ipv6_ || is_ipv4_mapped(addr.ip()))) {
            if (addresses_.insert(addr)) {
                ++loaded;
            }
        }
    }

    spdlog::debug("[hosts] Loaded {} addresses from file.", loaded);
}

void hosts::save_to_file() const {
    kth::ofstream file(file_path_.string());
    if (file.bad()) {
        spdlog::debug("[hosts] Failed to save hosts file.");
        return;
    }

    size_t saved = 0;
    addresses_.cvisit_all([&](address const& addr) {
        file << infrastructure::config::authority(addr).to_string() << std::endl;
        ++saved;
    });

    spdlog::debug("[hosts] Saved {} addresses to file.", saved);
}

size_t hosts::count() const {
    return addresses_.size();
}

code hosts::fetch(address& out) const {
    if (disabled_) {
        return error::not_found;
    }

    auto const size = addresses_.size();
    if (size == 0) {
        return error::not_found;
    }

    // Select a random index
    auto const random_index = pseudo_random::generate<size_t>(0, size - 1);

    // Visit elements to find the one at random_index
    size_t current = 0;
    bool found = false;

    addresses_.cvisit_all([&](address const& addr) {
        if (found) return;
        if (current == size_t(random_index)) {
            out = addr;
            found = true;
        }
        ++current;
    });

    return found ? error::success : error::not_found;
}

code hosts::fetch(address::list& out, size_t requested_count) const {
    if (disabled_) {
        return error::not_found;
    }

    auto const size = addresses_.size();
    if (size == 0) {
        return error::not_found;
    }

    // Collect all addresses first
    address::list all;
    all.reserve(size);

    addresses_.cvisit_all([&](address const& addr) {
        all.push_back(addr);
    });

    // Shuffle and take requested count
    pseudo_random::shuffle(all);

    auto const take_count = std::min(requested_count, all.size());
    out.reserve(take_count);
    for (size_t i = 0; i < take_count; ++i) {
        out.push_back(all[i]);
    }

    return error::success;
}

bool hosts::remove(address const& host) {
    if (disabled_) {
        return false;
    }

    return addresses_.erase(host) > 0;
}

bool hosts::store(address const& host) {
    if (disabled_) {
        return false;
    }

    // Filter non-routable addresses
    if ( ! host.is_routable()) {
        return false;
    }

    // Drop IPv6 when disabled (settings.use_ipv6 = false).
    if ( ! use_ipv6_ && ! is_ipv4_mapped(host.ip())) {
        return false;
    }

    // Check capacity
    if (addresses_.size() >= capacity_) {
        return false;
    }

    return addresses_.insert(host);
}

size_t hosts::store(address::list const& hosts_list) {
    if (disabled_ || hosts_list.empty()) {
        return 0;
    }

    size_t accepted = 0;

    for (auto const& host : hosts_list) {
        // Filter non-routable addresses
        if ( ! host.is_routable()) {
            continue;
        }

        // Drop IPv6 when disabled (settings.use_ipv6 = false).
        if ( ! use_ipv6_ && ! is_ipv4_mapped(host.ip())) {
            continue;
        }

        // Check capacity
        if (addresses_.size() >= capacity_) {
            break;
        }

        if (addresses_.insert(host)) {
            ++accepted;
        }
    }

    if (accepted > 0) {
        spdlog::debug("[hosts] Accepted {} of {} addresses.", accepted, hosts_list.size());
    }

    return accepted;
}

} // namespace kth::network
