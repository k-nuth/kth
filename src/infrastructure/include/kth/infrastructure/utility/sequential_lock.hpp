// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_SEQUENTIAL_LOCK_HPP
#define KTH_INFRASTRUCTURE_SEQUENTIAL_LOCK_HPP

#include <atomic>
#include <cstddef>

#include <kth/infrastructure/define.hpp>

namespace kth {

/// This class is thread safe.
/// Encapsulation of sequential locking conditions.
struct KI_API sequential_lock {
    using handle = size_t;

    /// Determine if the given handle is a write-locked handle.
    static
    bool is_write_locked(handle value) {
        return (value % 2) == 1;
    }

    sequential_lock();

    handle begin_read() const;
    bool is_read_valid(handle value) const;

    bool begin_write();
    bool end_write();

private:
    std::atomic<size_t> sequence_;
};

} // namespace kth

#endif
