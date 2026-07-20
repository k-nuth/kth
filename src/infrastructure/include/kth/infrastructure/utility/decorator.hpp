// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_DECORATOR_HPP
#define KTH_INFRASTRUCTURE_DECORATOR_HPP

#include <functional>

namespace kth {

/**
 * Defines a function decorator ala Python
 *
 *   void foo(int x, int y);
 *   function<void ()> wrapper(function<void (int)> f);
 *
 *   auto f = decorator(wrapper, bind(foo, 110, _1));
 *   f();
 */

template <typename Wrapper, typename Handler>
struct decorator_dispatch
{
    Wrapper wrapper;
    Handler handler;

    template <typename... Args>
    auto operator()(Args&&... args)
        -> decltype(wrapper(handler)(std::forward<Args>(args)...)) {
        return wrapper(handler)(std::forward<Args>(args)...);
    }
};

template <typename Wrapper, typename Handler>
decorator_dispatch<Wrapper, typename std::decay<Handler>::type>
decorator(Wrapper&& wrapper, Handler&& handler)
{
    return { wrapper, handler };
}

} // namespace kth

#endif

