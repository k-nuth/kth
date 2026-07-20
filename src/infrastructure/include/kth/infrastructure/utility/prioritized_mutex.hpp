// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PRIORITIZED_MUTEX_HPP
#define KTH_INFRASTRUCTURE_PRIORITIZED_MUTEX_HPP

#include <memory>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

/// This class is thread safe.
/// Encapsulation of prioritized locking conditions.
/// This is unconcerned with thread priority and is instead explicit.
class KI_API prioritized_mutex
{
public:
    using ptr = std::shared_ptr<prioritized_mutex>;

    explicit
    prioritized_mutex(bool prioritize=true);

    void lock_low_priority();
    void unlock_low_priority();

    void lock_high_priority();
    void unlock_high_priority();

private:
    bool const prioritize_;
    shared_mutex data_mutex_;
    shared_mutex next_mutex_;
    shared_mutex wait_mutex_;
};

} // namespace kth

#endif
