// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_MONITOR_HPP
#define KTH_INFRASTRUCTURE_MONITOR_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <kth/infrastructure/define.hpp>

// Defines the log and tracking but does not use them.
// These are defined in kth so that they can be used in network and blockchain.

namespace kth {

/// A reference counting wrapper for closures placed on the asio work heap.
struct KI_API monitor {
    using count = std::atomic<size_t>;
    using count_ptr = std::shared_ptr<count>;

    monitor(count_ptr counter, std::string&& name);
    ~monitor();

    template <typename Handler>
    void invoke(Handler handler) const {
        ////trace(*counter_, "*");
        handler();
    }

    void trace(size_t /*unused*/, std::string const& /*unused*/) const {
        ////#ifndef NDEBUG
        ////    LOG_DEBUG(LOG_SYSTEM)
        ////        << action << " " << name_ << " {" << count << "}";
        ////#endif
    }

private:
    count_ptr counter_;
    std::string const name_;
};

} // namespace kth

#endif
