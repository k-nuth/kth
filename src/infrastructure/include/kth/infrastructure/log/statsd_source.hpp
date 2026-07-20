// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_STATSD_SOURCE_HPP
#define KTH_INFRASTRUCTURE_LOG_STATSD_SOURCE_HPP

#include <chrono>

#include <boost/log/sources/basic_logger.hpp>
#include <boost/log/sources/features.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/threading_models.hpp>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/log/features/counter.hpp>
#include <kth/infrastructure/log/features/gauge.hpp>
#include <kth/infrastructure/log/features/metric.hpp>
#include <kth/infrastructure/log/features/rate.hpp>
#include <kth/infrastructure/log/features/timer.hpp>

namespace kth::log {

class KI_API statsd_source
  : public boost::log::sources::basic_composite_logger<char, statsd_source,
        boost::log::sources::multi_thread_model<boost::log::aux::light_rw_mutex>,
        boost::log::sources::features<features::metric, features::counter,
            features::gauge, features::timer, features::rate>>
{
    BOOST_LOG_FORWARD_LOGGER_MEMBERS(statsd_source)
};

BOOST_LOG_INLINE_GLOBAL_LOGGER_DEFAULT(stats, statsd_source);

#define KI_STATS_SIMPLE(name, sequence) \
    BOOST_LOG_WITH_PARAMS(kth::log::stats::get(), \
        (kth::log::keywords::metric = (name))sequence)

#define KI_STATS_WITH_RATE(name, rate, sequence) \
    BOOST_LOG_WITH_PARAMS(kth::log::stats::get(), \
        (kth::log::keywords::metric = (name)) \
        (kth::log::keywords::rate = (rate))sequence)

#define KI_STATS_COUNTER(name, value) \
    KI_STATS_SIMPLE(name, (kth::log::keywords::counter = (value)))

#define KI_STATS_COUNTER_RATE(name, value, rate) \
    KI_STATS_WITH_RATE(name, rate, (kth::log::keywords::counter = (value)))

#define KI_STATS_GAUGE(name, value) \
    KI_STATS_SIMPLE(name, (kth::log::keywords::gauge = (value)))

#define KI_STATS_GAUGE_RATE(name, value, rate) \
    KI_STATS_WITH_RATE(name, rate, (kth::log::keywords::gauge = (value)))

#define KI_STATS_TIMER(name, value) \
    KI_STATS_SIMPLE(name, (kth::log::keywords::timer = (value)))

#define KI_STATS_TIMER_RATE(name, value, rate) \
    KI_STATS_WITH_RATE(name, rate, (kth::log::keywords::timer = (value)))

} // namespace kth::log

#endif
