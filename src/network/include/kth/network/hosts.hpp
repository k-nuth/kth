// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_HOSTS_HPP
#define KTH_NETWORK_HOSTS_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <boost/unordered/concurrent_flat_set.hpp>

#include <kth/domain.hpp>
#include <kth/network/define.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

// Hash function for network_address (uses ip + port)
struct address_hash {
    using address = domain::message::network_address;

    size_t operator()(address const& addr) const noexcept {
        // Combine ip and port into a hash
        size_t seed = 0;
        auto const& ip = addr.ip();
        for (auto byte : ip) {
            seed ^= std::hash<uint8_t>{}(byte) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        seed ^= std::hash<uint16_t>{}(addr.port()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// Equality for network_address (compares ip + port only)
struct address_equal {
    using address = domain::message::network_address;

    bool operator()(address const& a, address const& b) const noexcept {
        return a.ip() == b.ip() && a.port() == b.port();
    }
};

/// Thread-safe host address store using lock-free concurrent set.
/// Manages network addresses for peer discovery.
/// File persistence is handled automatically (load in ctor, save in dtor).
class KN_API hosts : noncopyable {
public:
    using ptr = std::shared_ptr<hosts>;
    using address = domain::message::network_address;

    /// Construct an instance (loads from file if exists).
    explicit hosts(settings const& settings);

    /// Destructor (saves to file).
    ~hosts();

    /// Get current address count.
    [[nodiscard]]
    size_t count() const;

    /// Fetch a random address.
    [[nodiscard]]
    code fetch(address& out) const;

    /// Fetch multiple random addresses.
    [[nodiscard]]
    code fetch(address::list& out, size_t count) const;

    /// Remove an address.
    bool remove(address const& host);

    /// Store an address (filters non-routable).
    bool store(address const& host);

    /// Store multiple addresses (filters non-routable).
    size_t store(address::list const& hosts);

private:
    using address_set = boost::concurrent_flat_set<address, address_hash, address_equal>;

    void load_from_file();
    void save_to_file() const;

    size_t const capacity_;
    bool const disabled_;
    kth::path const file_path_;

    // Lock-free concurrent set for addresses
    address_set addresses_;
};

} // namespace kth::network

#endif
