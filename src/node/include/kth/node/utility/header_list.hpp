// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_HEADER_LIST_HPP
#define KTH_NODE_HEADER_LIST_HPP

#include <cstddef>
#include <memory>

#include <kth/domain.hpp>
#include <kth/node/define.hpp>
#include <kth/node/utility/check_list.hpp>

namespace kth::node {

/// A smart queue for chaining blockchain headers, thread safe.
/// The peer should be stopped if merge fails.
class KND_API header_list {
public:
    using ptr = std::shared_ptr<header_list>;

    /// Construct a list to fill the specified range of headers.
    header_list(size_t slot, infrastructure::config::checkpoint const& start, infrastructure::config::checkpoint const& stop);

    /// The list is fully populated.
    bool complete() const;

    /// The slot id of this instance.
    size_t slot() const;

    /// The height of the first header in the list.
    size_t first_height() const;

    /// The height of the last header in the list (or the start height).
    size_t previous_height() const;

    /// The hash of the last header in the list (or the start hash).
    hash_digest previous_hash() const;

    /// The hash of the stop checkpoint.
    hash_digest const& stop_hash() const;

    /// The ordered list of headers.
    /// This is not thread safe, call only after complete.
    domain::chain::header::list const& headers() const;

    /////// Generate a check list from a complete list of headers.
    ////infrastructure::config::checkpoint::list to_checkpoints() const;

    /// Merge the hashes in the message with those in the queue.
    /// Return true if linked all headers or complete.
    bool merge(headers_const_ptr message);

private:
    // The number of headers remaining until complete.
    size_t remaining() const;

    /// The hash of the last header in the list (or the start hash).
    hash_digest const& last() const;

    // Determine if the hash is linked to the preceding header.
    bool link(const domain::chain::header& header) const;

    // Determine if the header is valid (context free).
    bool check(const domain::chain::header& header) const;

    // Determine if the header is acceptable for the current height.
    bool accept(const domain::chain::header& header) const;

    // This is protected by mutex.
    domain::chain::header::list list_;

#if ! defined(__EMSCRIPTEN__)
    mutable upgrade_mutex mutex_;
#else
    mutable shared_mutex mutex_;
#endif

    size_t const height_;
    infrastructure::config::checkpoint const start_;
    infrastructure::config::checkpoint const stop_;
    size_t const slot_;
};

} // namespace kth::node

#endif

