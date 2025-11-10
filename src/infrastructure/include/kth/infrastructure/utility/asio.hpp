// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ASIO_HPP
#define KTH_INFRASTRUCTURE_ASIO_HPP

#include <chrono>
#include <memory>

#if ! defined(__EMSCRIPTEN__)
#include <boost/thread.hpp>
#endif

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/utility/asio_helper.hpp>

// Convenience namespace for commonly used boost asio aliases.
namespace kth::asio {

namespace error = ::asio::error;

using hours = std::chrono::hours;
using minutes = std::chrono::minutes;
using seconds = std::chrono::seconds;
using milliseconds = std::chrono::milliseconds;
using microseconds = std::chrono::microseconds;

// Steady clock: use for continuity, not time of day determinations.
using steady_clock = std::chrono::steady_clock;
using duration = steady_clock::duration;
using time_point = steady_clock::time_point;

#if ! defined(__EMSCRIPTEN__)
using timer = ::asio::basic_waitable_timer<steady_clock>;

using context = ::asio::io_context;
using address = ::asio::ip::address;
using ipv4 = ::asio::ip::address_v4;
using ipv6 = ::asio::ip::address_v6;
using tcp = ::asio::ip::tcp;
using endpoint = ::asio::ip::tcp::endpoint;

using socket = tcp::socket;
using acceptor = tcp::acceptor;
using resolver = tcp::resolver;
using results_type = tcp::resolver::results_type;

using work_guard = ::asio::executor_work_guard<context::executor_type>;

// Maximum listen backlog - use system constant directly
// (boost::asio::socket_base::max_connections was removed in Boost 1.87)
constexpr int max_connections = SOMAXCONN;

// Boost thread is used because of thread_specific_ptr limitation:
// stackoverflow.com/q/22448022/1172329
using thread = boost::thread;

#else

// Note: dummy types

using timer = int;
using service = int;
using address = int;
using ipv4 = int;
using ipv6 = int;
using tcp = int;
using endpoint = int;
using socket = int;
using acceptor = int;
using resolver = int;
using query = int;
using iterator = int;

constexpr int max_connections = 0;

using thread = int;

#endif // ! defined(__EMSCRIPTEN__)

using socket_ptr = std::shared_ptr<socket>;


} // namespace kth::asio

#define FMT_HEADER_ONLY 1
#include <fmt/format.h>

#if ! defined(__EMSCRIPTEN__)
// template <>
// struct fmt::formatter<kth::asio::ipv6> {
//     constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

//     template <typename FormatContext>
//     auto format(kth::asio::ipv6 const& addr, FormatContext& ctx) {
//         return format_to(ctx.out(), "{}", addr.to_string());
//     }
// };

template <>
struct fmt::formatter<kth::asio::ipv6> : formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::asio::ipv6 const& addr, FormatContext& ctx) const {
        return formatter<std::string>::format(addr.to_string(), ctx);
    }
};
#endif // ! defined(__EMSCRIPTEN__)

#endif // KTH_INFRASTRUCTURE_ASIO_HPP
