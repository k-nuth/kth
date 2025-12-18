// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ASIO_HELPER_HPP_
#define KTH_INFRASTRUCTURE_ASIO_HELPER_HPP_

#if defined(KTH_ASIO_STANDALONE)
#include <asio.hpp>
#include <asio/thread_pool.hpp>
#include <asio/experimental/channel.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#else

#if ! defined(__EMSCRIPTEN__)
#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#else
#include <boost/asio/error.hpp>
#endif

namespace asio = boost::asio;
#endif

#endif // KTH_INFRASTRUCTURE_ASIO_HELPER_HPP_
