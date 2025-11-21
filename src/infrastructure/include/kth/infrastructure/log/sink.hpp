// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_SINK_HPP
#define KTH_INFRASTRUCTURE_LOG_SINK_HPP

#include <string>

#include <kth/infrastructure/define.hpp>

namespace kth::log {

void initialize(std::string const& debug_file, std::string const& error_file, bool stdout_enabled, bool verbose);

} // namespace kth::log

#endif
