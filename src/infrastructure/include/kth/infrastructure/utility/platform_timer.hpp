// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see COPYING.
#ifndef KTH_INFRASTRUCTURE_PLATFORM_TIMER_HPP
#define KTH_INFRASTRUCTURE_PLATFORM_TIMER_HPP
#if defined(KTH_BROWSER_NODE)
#include <kth/infrastructure/utility/browser_runtime.hpp>
namespace kth { using steady_timer = browser::timer; }
#else
#include <kth/infrastructure/utility/asio_helper.hpp>
namespace kth { using steady_timer = ::asio::steady_timer; }
#endif
#endif
