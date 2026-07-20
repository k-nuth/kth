// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ATOMIC_POINTER_HPP
#define KTH_INFRASTRUCTURE_ATOMIC_POINTER_HPP

#include <type_traits>
#include <utility>

#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

template <typename Type>
class atomic {
public:

    /// Create an atomically-accessible default instance of the type.
    atomic() {}

    /// Create an atomically-accessible copied instance of the type.
    atomic(const Type& instance)
        : instance_(instance) {}

    /// Create an atomically-accessible moved instance of the type.
    atomic(Type&& instance)
        : instance_(std::forward<Type>(instance)) {}

    Type load() const {
        // Critical Section
        ///////////////////////////////////////////////////////////////////////
        shared_lock lock(mutex_);

        return instance_;
        ///////////////////////////////////////////////////////////////////////
    }

    void store(const Type& instance) {
        // Critical Section
        ///////////////////////////////////////////////////////////////////////
        unique_lock lock(mutex_);

        instance_ = instance;
        ///////////////////////////////////////////////////////////////////////
    }

    void store(Type&& instance) {
        // Critical Section
        ///////////////////////////////////////////////////////////////////////
        unique_lock lock(mutex_);

        instance_ = std::forward<Type>(instance);
        ///////////////////////////////////////////////////////////////////////
    }

private:
    using decay_type = typename std::decay<Type>::type;

    decay_type instance_;
    mutable shared_mutex mutex_;
};

} // namespace kth

#endif
