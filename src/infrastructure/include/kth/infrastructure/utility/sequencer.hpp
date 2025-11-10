// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_SEQUENCER_HPP
#define KTH_INFRASTRUCTURE_SEQUENCER_HPP

#include <functional>
#include <memory>
#include <queue>

#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/enable_shared_from_base.hpp>
#include <kth/infrastructure/utility/thread.hpp>
////#include <kth/infrastructure/utility/track.hpp>

namespace kth {

class sequencer
    : public enable_shared_from_base<sequencer>
    /*, track<sequencer>*/
{
public:
    using ptr = std::shared_ptr<sequencer>;
    using action = std::function<void ()>;

    explicit
    sequencer(asio::context& service);

    ~sequencer();

    void lock(action&& handler);
    void unlock();

private:
    // This is thread safe.
    asio::context& service_;

    // These are protected by mutex.
    bool executing_;
    std::queue<action> actions_;
    mutable shared_mutex mutex_;
};

} // namespace kth

#endif
