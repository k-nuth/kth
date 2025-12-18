// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_HASH_MEMOIZER_HPP
#define KTH_DOMAIN_CHAIN_HASH_MEMOIZER_HPP

#include <kth/infrastructure/utility/thread.hpp>

namespace kth::domain::chain {

template <typename T>
class hash_memoizer {
public:
    hash_digest hash() const {
#if ! defined(__EMSCRIPTEN__)
        ///////////////////////////////////////////////////////////////////////////
        // Critical Section
        mutex_.lock_upgrade();

        if ( ! hash_) {
            //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
            mutex_.unlock_upgrade_and_lock();
            hash_ = std::make_shared<hash_digest>(bitcoin_hash(derived().to_data()));
            mutex_.unlock_and_lock_upgrade();
            //---------------------------------------------------------------------
        }
        auto const hash = *hash_;
        mutex_.unlock_upgrade();
        ///////////////////////////////////////////////////////////////////////////
        return hash;
#else
        return bitcoin_hash(derived().to_data());
#endif
    }

    void invalidate() const {
#if ! defined(__EMSCRIPTEN__)
        ///////////////////////////////////////////////////////////////////////////
        // Critical Section
        mutex_.lock_upgrade();

        if (hash_) {
            mutex_.unlock_upgrade_and_lock();
            //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
            hash_.reset();
            //---------------------------------------------------------------------
            mutex_.unlock_and_lock_upgrade();
        }

        mutex_.unlock_upgrade();
        ///////////////////////////////////////////////////////////////////////////
#endif
    }

private:
    T& derived() {return *static_cast<T*>(this);}
    T const& derived() const {return *static_cast<T const*>(this);}

#if ! defined(__EMSCRIPTEN__)
    mutable upgrade_mutex mutex_;
    mutable std::shared_ptr<hash_digest> hash_;
#endif
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_HASH_MEMOIZER_HPP
