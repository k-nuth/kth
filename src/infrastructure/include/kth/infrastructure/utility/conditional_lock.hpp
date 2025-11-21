// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_CONDITIONAL_LOCK_HPP_
#define KTH_INFRASTRUCTURE_CONDITIONAL_LOCK_HPP_

#include <memory>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

struct KI_API conditional_lock {
    /// Conditional lock using specified mutex pointer.
    explicit
    conditional_lock(std::shared_ptr<shared_mutex> const& mutex_ptr);

    /// Unlock.
    ~conditional_lock();

private:
    std::shared_ptr<shared_mutex> const mutex_ptr_;
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_CONDITIONAL_LOCK_HPP_
