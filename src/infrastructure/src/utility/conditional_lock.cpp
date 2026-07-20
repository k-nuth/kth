// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/conditional_lock.hpp>

#include <memory>

#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

conditional_lock::conditional_lock(std::shared_ptr<shared_mutex> const& mutex_ptr)
    : mutex_ptr_(mutex_ptr)
{
    if (mutex_ptr_) {
        mutex_ptr->lock();
    }
}

conditional_lock::~conditional_lock() {
    if (mutex_ptr_) {
        mutex_ptr_->unlock();
    }
}

} // namespace kth
