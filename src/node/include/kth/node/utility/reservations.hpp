// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RESERVATIONS_HPP
#define KTH_NODE_RESERVATIONS_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <kth/blockchain.hpp>
#include <kth/node/define.hpp>
#include <kth/node/settings.hpp>
#include <kth/node/utility/check_list.hpp>
#include <kth/node/utility/reservation.hpp>

namespace kth::node {

// Class to manage a set of reservation objects during sync, thread safe.
class KND_API reservations {
public:
    typedef struct {
        size_t active_count;
        double arithmentic_mean;
        double standard_deviation;
    } rate_statistics;

    using ptr = std::shared_ptr<reservations>;

    /// Construct a reservation table of reservations, allocating hashes evenly
    /// among the rows up to the limit of a single get headers p2p request.
    reservations(check_list& hashes, blockchain::fast_chain& chain, settings const& settings);

    /// Set the flush lock guard.
    bool start();

    /// Clear the flush lock guard.
    bool stop();

    /// The average and standard deviation of block import rates.
    rate_statistics rates() const;

    /// Return a copy of the reservation table.
    reservation::list table() const;

#if ! defined(KTH_DB_READONLY)
    /// Import the given block to the blockchain at the specified height.
    bool import(block_const_ptr block, size_t height);
#endif

    /// Populate a starved row by taking half of the hashes from a weak row.
    bool populate(reservation::ptr minimal);

    /// Remove the row from the reservation table if found.
    void remove(reservation::ptr row);

    /// The max size of a block request.
    size_t max_request() const;

    /// Set the max size of a block request (defaults to 50000).
    void set_max_request(size_t value);

private:
    bool inline flush(size_t height);

    // Create the specified number of reservations and distribute hashes.
    void initialize(size_t connections);

    // Find the reservation with the most hashes.
    reservation::ptr find_maximal();

    // Move half of the maximal reservation to the specified reservation.
    bool partition(reservation::ptr minimal);

    // Move the maximum unreserved hashes to the specified reservation.
    bool reserve(reservation::ptr minimal);

    // Thread safe.
    check_list& hashes_;
    std::atomic<size_t> max_request_;
    const uint32_t timeout_;

    // Protected by block exclusivity and limited call scope.
    blockchain::fast_chain& chain_;

    // Protected by mutex.
    reservation::list table_;
#if ! defined(__EMSCRIPTEN__)
    mutable upgrade_mutex mutex_;
#else
    mutable shared_mutex mutex_;
#endif

};

} // namespace kth::node

#endif

