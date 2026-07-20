// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_SCOPE_LOCK_HPP
#define KTH_INFRASTRUCTURE_SCOPE_LOCK_HPP

#include <memory>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

/// This class is thread safe.
/// Reserve exclusive access to a resource while this object is in scope.
class KI_API scope_lock
{
public:
    using ptr = std::shared_ptr<scope_lock>;

    /// Lock using the specified mutex reference.
    explicit
    scope_lock(shared_mutex& mutex);

    /// Unlock.
    ~scope_lock();

private:
    shared_mutex& mutex_;
};

} // namespace kth

#endif
