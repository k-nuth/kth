// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_STATSD_SINK_HPP
#define KTH_INFRASTRUCTURE_LOG_STATSD_SINK_HPP

#include <kth/infrastructure/utility/asio_helper.hpp>

#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/log/rotable_file.hpp>

namespace kth::log {

void initialize_statsd(rotable_file const& file);

void initialize_statsd(threadpool& pool, const infrastructure::config::authority& server);

} // namespace kth::log

#endif
