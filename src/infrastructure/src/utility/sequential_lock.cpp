// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/sequential_lock.hpp>

namespace kth {

sequential_lock::sequential_lock()
  : sequence_(0)
{
}

sequential_lock::handle sequential_lock::begin_read() const {
    // Start read lock.
    return sequence_.load();
}

bool sequential_lock::is_read_valid(handle value) const {
    // Test read lock.
    return value == sequence_.load();
}

// Failure does not prevent a subsequent begin or end resetting the lock state.
bool sequential_lock::begin_write()
{
    // Start write lock.
    return is_write_locked(++sequence_);
}

// Failure does not prevent a subsequent begin or end resetting the lock state.
bool sequential_lock::end_write()
{
    // End write lock.
    return !is_write_locked(++sequence_);
}

} // namespace kth
