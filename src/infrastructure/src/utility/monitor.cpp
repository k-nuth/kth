// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/monitor.hpp>

#include <cstddef>
#include <string>
#include <utility>
////#include <kth/infrastructure/log/sources.hpp>

// Defines the log and tracking but does not use them.
// These are defined in kth so that they can be used in network and blockchain.

namespace kth {

monitor::monitor(count_ptr counter, std::string&& name)
  : counter_(std::move(counter)), name_(std::move(name))
{
    trace(++(*counter_), "+");
}

monitor::~monitor() {
    trace(--(*counter_), "-");
}

} // namespace kth
