// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_HOSTS_HPP
#define KTH_NETWORK_HOSTS_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <kth/domain.hpp>
#include <kth/network/define.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

/// This class is thread safe.
/// The hosts class manages a thread-safe dynamic store of network addresses.
/// The store can be loaded and saved from/to the specified file path.
/// The file is a line-oriented set of infrastructure::config::authority serializations.
/// Duplicate addresses and those with zero-valued ports are disacarded.
class KN_API hosts : noncopyable {
public:
    using ptr = std::shared_ptr<hosts>;
    using address = domain::message::network_address;
    using result_handler = handle0;

    /// Construct an instance.
    hosts(settings const& settings);

    /// Load hosts file if found.
    virtual code start();

    // Save hosts to file.
    virtual code stop();

    virtual size_t count() const;
    virtual code fetch(address& out) const;
    virtual code fetch(address::list& out) const;
    virtual code remove(address const& host);
    virtual code store(address const& host);
    virtual void store(address::list const& hosts, result_handler handler);

private:
    using list = boost::circular_buffer<address>;
    using iterator = list::iterator;

    iterator find(address const& host);

    size_t const capacity_;

    // These are protected by a mutex.
    list buffer_;
    std::atomic<bool> stopped_;
    mutable upgrade_mutex mutex_;

    // HACK: we use this because the buffer capacity cannot be set to zero.
    bool const disabled_;
    kth::path const file_path_;
};

} // namespace kth::network

#endif

