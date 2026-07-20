// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_SEVERITY_HPP
#define KTH_INFRASTRUCTURE_LOG_SEVERITY_HPP

#include <kth/infrastructure/define.hpp>

namespace kth::log {

enum class severity {
    verbose,
    debug,
    info,
    warning,
    error,
    fatal
};

} // namespace kth::log

#endif
