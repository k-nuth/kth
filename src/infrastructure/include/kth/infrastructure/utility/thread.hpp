// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_THREAD_HPP
#define KTH_INFRASTRUCTURE_THREAD_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#if ! defined(__EMSCRIPTEN__)
#include <boost/thread.hpp>
#else
#include <shared_mutex>
#endif

#include <kth/infrastructure/define.hpp>

namespace kth {

////// Adapted from: stackoverflow.com/a/18298965/1172329
////#ifndef thread_local
////    #if (__STDC_VERSION__ >= 201112) && ( ! defined __STDC_NO_THREADS__)
////        #define thread_local _Thread_local
////    #elif (defined _MSC_VER)
////        #define thread_local __declspec(thread)
////    #elif (defined __GNUC__)
////        #define thread_local __thread
////    #else
////        #error "Cannot define thread_local"
////    #endif
////#endif

enum class thread_priority {
    high,
    normal,
    low,
    lowest
};

#if ! defined(__EMSCRIPTEN__)

using shared_mutex = boost::shared_mutex;
using upgrade_mutex = boost::upgrade_mutex;

using unique_lock = boost::unique_lock<shared_mutex>;
using shared_lock = boost::shared_lock<shared_mutex>;
using upgrade_mutex_ptr = std::shared_ptr<boost::upgrade_mutex>;

#else

using shared_mutex = std::shared_mutex;
using unique_lock = std::unique_lock<shared_mutex>;
using shared_lock = std::shared_lock<shared_mutex>;

#endif //! defined(__EMSCRIPTEN__)

using shared_mutex_ptr = std::shared_ptr<shared_mutex>;


KI_API void set_priority(thread_priority priority);
KI_API thread_priority priority(bool priority);
KI_API size_t thread_default(size_t configured);
KI_API size_t thread_ceiling(size_t configured);
KI_API size_t thread_floor(size_t configured);

} // namespace kth

#endif
