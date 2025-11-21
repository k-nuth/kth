// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_DELEGATES_HPP
#define KTH_INFRASTRUCTURE_DELEGATES_HPP

#include <functional>

#if ! defined(__EMSCRIPTEN__)
#include <kth/infrastructure/utility/work.hpp>
#endif

namespace kth {
namespace delegates {

/// Binding delegate (current thread).
template <typename Handler>
struct bound
{
    template <typename... Args>
    void operator()(Args&&... args) {
        work::bound(std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    Handler handler;
};

/// Asynchronous delegate.
template <typename Handler>
struct concurrent
{
    template <typename... Args>
    void operator()(Args&&... args) {
        heap->concurrent(std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    Handler handler;
    work::ptr heap;
};

/// Ordered synchronous delegate.
template <typename Handler>
struct ordered
{
    template <typename... Args>
    void operator()(Args&&... args) {
        heap->ordered(std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    Handler handler;
    work::ptr heap;
};

/// Unordered synchronous delegate.
template <typename Handler>
struct unordered
{
    template <typename... Args>
    void operator()(Args&&... args) {
        heap->unordered(std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    Handler handler;
    work::ptr heap;
};

/// Sequence ordering delegate.
template <typename Handler>
struct sequence
{
    template <typename... Args>
    void operator()(Args&&... args) {
        heap->lock(std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    Handler handler;
    work::ptr heap;
};

} // namespace delegates
} // namespace kth

#endif
