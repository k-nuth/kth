// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/hosts.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include <kth/domain.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

using namespace kth::config;

#define NAME "hosts"

// TODO: change to network_address bimap hash table with services and age.
hosts::hosts(settings const& settings)
    : capacity_(std::min(max_address, static_cast<size_t>(settings.host_pool_capacity)))
    , buffer_(std::max(capacity_, static_cast<size_t>(1u)))
    , stopped_(true)
    , file_path_(settings.hosts_file)
    , disabled_(capacity_ == 0)
{}

// private
hosts::iterator hosts::find(address const& host) {
    auto const found = [&host](address const& entry) {
        return entry.port() == host.port() && entry.ip() == host.ip();
    };

    return std::find_if(buffer_.begin(), buffer_.end(), found);
}

size_t hosts::count() const {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock lock(mutex_);

    return buffer_.size();
    ///////////////////////////////////////////////////////////////////////////
}

code hosts::fetch(address& out) const {
    if (disabled_) {
        return error::not_found;
    }

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock lock(mutex_);

    if (stopped_) {
        return error::service_stopped;
    }

    if (buffer_.empty()) {
        return error::not_found;
    }

    // Randomly select an address from the buffer.
    auto const random = pseudo_random_broken_do_not_use::next(0, buffer_.size() - 1);
    auto const index = static_cast<size_t>(random);
    out = buffer_[index];
    return error::success;
    ///////////////////////////////////////////////////////////////////////////
}

code hosts::fetch(address::list& out) const {
    if (disabled_) {
        return error::not_found;
    }

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    {
        shared_lock lock(mutex_);

        if (stopped_) {
            return error::service_stopped;
        }

        if (buffer_.empty()) {
            return error::not_found;
        }

        auto const out_count = std::min(buffer_.size(), capacity_) / static_cast<size_t>(pseudo_random_broken_do_not_use::next(1, 20));

        if (out_count == 0) {
            return error::success;
        }

        out.reserve(out_count);
        for (size_t index = 0; index < out_count; ++index) {
            out.push_back(buffer_[index]);
        }
    }
    ///////////////////////////////////////////////////////////////////////////

    pseudo_random_broken_do_not_use::shuffle(out);
    return error::success;
}

// load
code hosts::start() {
    if (disabled_) {
        return error::success;
    }

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if ( ! stopped_) {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return error::operation_failed;
    }

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    stopped_ = false;
    kth::ifstream file(file_path_.string());
    auto const file_error = file.bad();

    if ( ! file_error) {
        std::string line;

        while (std::getline(file, line)) {
            // TODO: create full space-delimited network_address serialization.
            // Use to/from string format as opposed to wire serialization.
            infrastructure::config::authority host(line);

            if (host.port() != 0) {
                buffer_.push_back(host.to_network_address());
            }
        }
    }

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    if (file_error) {
        spdlog::debug("[network] Failed to save hosts file.");
        return error::file_system;
    }

    return error::success;
}

// load
code hosts::stop() {
    if (disabled_) {
        return error::success;
    }

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if (stopped_) {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return error::success;
    }

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    stopped_ = true;
    kth::ofstream file(file_path_.string());
    auto const file_error = file.bad();

    if ( ! file_error) {
        for (auto const& entry: buffer_) {
            // TODO: create full space-delimited network_address serialization.
            // Use to/from string format as opposed to wire serialization.
            file << infrastructure::config::authority(entry) << std::endl;
        }

        buffer_.clear();
    }

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    if (file_error) {
        spdlog::debug("[network] Failed to load hosts file.");
        return error::file_system;
    }

    return error::success;
}

code hosts::remove(address const& host) {
    if (disabled_) {
        return error::not_found;
    }

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if (stopped_) {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return error::service_stopped;
    }

    auto it = find(host);

    if (it != buffer_.end()) {
        mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        buffer_.erase(it);

        mutex_.unlock();
        //---------------------------------------------------------------------
        return error::success;
    }

    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    return error::not_found;
}

code hosts::store(address const& host) {
    if (disabled_) {
        return error::success;
    }

    if ( ! host.is_valid()) {
        // Do not treat invalid address as an error, just log it.
        spdlog::debug("[network] Invalid host address from peer.");
        return error::success;
    }

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if (stopped_) {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return error::service_stopped;
    }

    if (find(host) == buffer_.end()) {
        mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        buffer_.push_back(host);

        mutex_.unlock();
        //---------------------------------------------------------------------
        return error::success;
    }

    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    ////// We don't treat redundant address as an error, just log it.
    ////spdlog::debug("[network]
    ////] Redundant host address [{}] from peer.", authority(host));

    return error::success;
}

void hosts::store(address::list const& hosts, result_handler handler) {
    if (disabled_ || hosts.empty()) {
        handler(error::success);
        return;
    }

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if (stopped_) {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        handler(error::service_stopped);
        return;
    }

    // Accept between 1 and all of this peer's addresses up to capacity.
    auto const capacity = buffer_.capacity();
    auto const usable = std::min(hosts.size(), capacity);
    auto const random = static_cast<size_t>(pseudo_random_broken_do_not_use::next(1, usable));

    // But always accept at least the amount we are short if available.
    auto const gap = capacity - buffer_.size();
    auto const accept = std::max(gap, random);

    // Convert minimum desired to step for iteration, no less than 1.
    auto const step = std::max(usable / accept, size_t(1));
    size_t accepted = 0;

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    for (size_t index = 0; index < usable; index = ceiling_add(index, step)) {
        auto const& host = hosts[index];

        // Do not treat invalid address as an error, just log it.
        if ( ! host.is_valid())
        {
            spdlog::debug("[network] Invalid host address from peer.");
            continue;
        }

        // Do not allow duplicates in the host cache.
        if (find(host) == buffer_.end()) {
            ++accepted;
            buffer_.push_back(host);
        }
    }

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    spdlog::debug("[network] Accepted ({} of {}) host addresses from peer.", accepted, hosts.size());

    handler(error::success);
}

} // namespace kth::network