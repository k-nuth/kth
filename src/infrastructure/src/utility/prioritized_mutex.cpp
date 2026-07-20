// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/prioritized_mutex.hpp>

namespace kth {

prioritized_mutex::prioritized_mutex(bool prioritize)
  : prioritize_(prioritize)
{
}

void prioritized_mutex::lock_low_priority()
{
    if (prioritize_) {
        wait_mutex_.lock();
        next_mutex_.lock();
    }

    data_mutex_.lock();
}

void prioritized_mutex::unlock_low_priority()
{
    if (prioritize_) {
        next_mutex_.unlock();
}

    data_mutex_.unlock();

    if (prioritize_) {
        wait_mutex_.unlock();
}
}

void prioritized_mutex::lock_high_priority()
{
    if (prioritize_) {
        next_mutex_.lock();
}

    data_mutex_.lock();

    if (prioritize_) {
        next_mutex_.unlock();
}
}

void prioritized_mutex::unlock_high_priority()
{
    data_mutex_.unlock();
}

} // namespace kth
