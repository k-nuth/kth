// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/scope_lock.hpp>

#include <memory>

#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

scope_lock::scope_lock(shared_mutex& mutex)
  : mutex_(mutex)
{
    mutex_.lock();
}

scope_lock::~scope_lock()
{
    mutex_.unlock();
}

} // namespace kth
