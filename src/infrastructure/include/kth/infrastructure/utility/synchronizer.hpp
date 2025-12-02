// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_SYNCHRONIZER_HPP
#define KTH_INFRASTRUCTURE_SYNCHRONIZER_HPP

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

enum class synchronizer_terminate
{
    /// Terminate on first error or count.
    /// Return code on first error, otherwise success.
    on_error,

    /// Terminate first success (priority) or count.
    /// Return success on first success, otherwise operation_failed.
    on_success,

    /// Terminate on count only.
    /// Return success once count is reached (always).
    on_count
};

template <typename Handler>
class synchronizer {
public:
    synchronizer(Handler&& handler, size_t clearance_count, std::string const& name, synchronizer_terminate mode)
        : handler_(std::forward<Handler>(handler))
        , name_(name)
        , clearance_count_(clearance_count)
        , counter_(std::make_shared<size_t>(0))
#if ! defined(__EMSCRIPTEN__)
        , mutex_(std::make_shared<upgrade_mutex>())
#else
        , mutex_(std::make_shared<shared_mutex>())
#endif
        , terminate_(mode)
    {}

    // Determine if the code is cause for termination.
    bool complete(code const& ec) {
        switch (terminate_) {
            case synchronizer_terminate::on_error:
                return !!ec;
            case synchronizer_terminate::on_success:
                return !ec;
            case synchronizer_terminate::on_count:
                return false;
            default:
                throw std::invalid_argument("mode");
        }
    }

    // Assuming we are terminating, generate the proper result code.
    code result(code const& ec) {
        switch (terminate_) {
            case synchronizer_terminate::on_error:
                return ec ? ec : error::success;
            case synchronizer_terminate::on_success:
                return !ec ? error::success : ec;
            case synchronizer_terminate::on_count:
                return error::success;
            default:
                throw std::invalid_argument("mode");
        }
    }

    template <typename... Args>
    void operator()(code const& ec, Args&&... args) {
#if ! defined(__EMSCRIPTEN__)
        // Critical Section
        ///////////////////////////////////////////////////////////////////////
        mutex_->lock_upgrade();

        auto const initial_count = *counter_;
        KTH_ASSERT(initial_count <= clearance_count_);

        // Another handler cleared this and shortcircuited the count, ignore.
        if (initial_count == clearance_count_) {
            mutex_->unlock_upgrade();
            //-----------------------------------------------------------------
            return;
        }

        auto const count = complete(ec) ? clearance_count_ : initial_count + 1;
        auto const cleared = count == clearance_count_;

        mutex_->unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        (*counter_) = count;

        mutex_->unlock();
        ///////////////////////////////////////////////////////////////////////
    #else
        size_t initial_count;
        size_t count;
        bool cleared;

        {
            std::shared_lock<std::shared_mutex> lock(*mutex_);
            initial_count = *counter_;
            KTH_ASSERT(initial_count <= clearance_count_);

            if (initial_count == clearance_count_) {
                return;
            }

            count = complete(ec) ? clearance_count_ : initial_count + 1;
            cleared = count == clearance_count_;
        }

        {
            std::unique_lock<std::shared_mutex> lock(*mutex_);
            (*counter_) = count;
        }
    #endif

        if (cleared) {
            handler_(result(ec), std::forward<Args>(args)...);
        }
    }

private:
    using decay_handler = typename std::decay<Handler>::type;

    decay_handler handler_;
    std::string const name_;
    size_t const clearance_count_;
    synchronizer_terminate const terminate_;

    // We use pointer to reference the same value/mutex across instance copies.
    std::shared_ptr<size_t> counter_;


#if ! defined(__EMSCRIPTEN__)
    upgrade_mutex_ptr mutex_;
#else
    shared_mutex_ptr mutex_;
#endif
};

template <typename Handler>
synchronizer<Handler> synchronize(Handler&& handler, size_t clearance_count,
    std::string const& name, synchronizer_terminate mode= synchronizer_terminate::on_error) {
    return synchronizer<Handler>(std::forward<Handler>(handler), clearance_count, name, mode);
}

} // namespace kth

#endif
