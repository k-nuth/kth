// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_ATTRIBUTES_HPP
#define KTH_INFRASTRUCTURE_LOG_ATTRIBUTES_HPP

#include <string>

#include <boost/log/attributes/clock.hpp>
#include <boost/log/expressions/keyword.hpp>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/log/severity.hpp>

namespace kth::log {
namespace attributes {

// severity/channel/timestamp/message log entries
BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "Timestamp", boost::posix_time::ptime)
BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", kth::log::severity)
BOOST_LOG_ATTRIBUTE_KEYWORD(channel, "Channel", std::string)

} // namespace attributes
} // namespace kth::log

#endif
