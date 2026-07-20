// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PENDING_IPP
#define KTH_INFRASTRUCTURE_PENDING_IPP

#include <algorithm>
#include <cstddef>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

template <typename Element>
pending<Element>::pending(size_t initial_capacity)
  : stopped_(false)
{
    elements_.reserve(initial_capacity);
}

template <typename Element>
pending<Element>::~pending()
{
    ////KTH_ASSERT_MSG(elements_.empty(), "Pending collection not cleared.");
}

template <typename Element>
typename pending<Element>::elements pending<Element>::collection() const {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock lock(mutex_);

    return elements_;
    ///////////////////////////////////////////////////////////////////////////
};

template <typename Element>
size_t pending<Element>::size() const {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock lock(mutex_);

    return elements_.size();
    ///////////////////////////////////////////////////////////////////////////
};

template <typename Element>
bool pending<Element>::exists(finder match) const {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock lock(mutex_);

    return std::find_if(elements_.begin(), elements_.end(), match) !=
        elements_.end();
    ///////////////////////////////////////////////////////////////////////////
}

template <typename Element>
code pending<Element>::store(element_ptr element)
{
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if (stopped_) {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return error::service_stopped;
    }

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    elements_.push_back(element);

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    return error::success;
}

template <typename Element>
code pending<Element>::store(element_ptr element, finder match)
{
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    auto const stopped = stopped_.load();

    if ( ! stopped) {
        if (std::find_if(elements_.begin(), elements_.end(), match) ==
            elements_.end())
        {
            mutex_.unlock_upgrade_and_lock();
            //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
            elements_.push_back(element);
            //-----------------------------------------------------------------
            mutex_.unlock();
            return error::success;
        }
    }

    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    // Stopped and found are the only ways to get here.
    return stopped ? error::service_stopped : error::address_in_use;
}

template <typename Element>
void pending<Element>::remove(element_ptr element)
{
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    auto it = std::find(elements_.begin(), elements_.end(), element);

    if (it != elements_.end()) {
        mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        elements_.erase(it);
        //---------------------------------------------------------------------
        mutex_.unlock();
        return;
    }

    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
}

// This is idempotent.
template <typename Element>
void pending<Element>::stop(code const& ec)
{
    elements copy;

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if ( ! stopped_) {
        mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        stopped_ = true;
        //---------------------------------------------------------------------
        mutex_.unlock_and_lock_upgrade();

        // Once stopped list cannot increase, but copy to escape lock.
        copy = elements_;
    }

    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    for (auto const element: copy)
        element->stop(ec);
}

template <typename Element>
void pending<Element>::close()
{
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    unique_lock lock(mutex_);

    // Close should block until element has freed all resources.
    for (auto element: elements_)
        element->close();

    elements_.clear();
    ///////////////////////////////////////////////////////////////////////////
};

} // namespace kth

#endif
