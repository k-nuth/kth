// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see COPYING.

#ifndef KTH_INFRASTRUCTURE_UTILITY_BROWSER_RUNTIME_HPP
#define KTH_INFRASTRUCTURE_UTILITY_BROWSER_RUNTIME_HPP

#if !defined(__EMSCRIPTEN__)
#error "browser::runtime is Worker-only"
#endif

#include <asio.hpp>
#include <asio/any_completion_handler.hpp>
#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <chrono>
#include <cassert>
#include <utility>
#include <algorithm>
#include <list>
#include <memory>

namespace kth::browser {

// All access, including destruction, belongs to the browser Worker. Producers
// enter through browser callbacks and wake the runnable queue explicitly.
struct runtime {
    runtime() {
        assert(current_ == nullptr);
        current_ = this;
    }

    ~runtime() {
        if (scheduled_) {
            emscripten_clear_timeout(scheduled_);
        }
        current_ = nullptr;
    }

    runtime(runtime const&) = delete;
    runtime& operator=(runtime const&) = delete;

    static runtime& current() {
        assert(current_);
        return *current_;
    }

    ::asio::io_context& context() {
        return context_;
    }

    void stream_created() {
        ++streams_;
    }

    void stream_destroyed() {
        assert(streams_);
        --streams_;
    }

    void timer_started() {
        ++timers_;
    }

    void timer_completed() {
        assert(timers_);
        --timers_;
    }

    std::size_t streams() const {
        return streams_;
    }

    std::size_t timers() const {
        return timers_;
    }

    bool idle() const {
        return !draining_ && !scheduled_ && !streams_ && !timers_ && context_.stopped();
    }

    // One lifecycle observer, owned by this Worker. Notification happens after
    // the root completion returns and the scheduler has no outstanding work.
    void when_idle(void (*observer)()) {
        idle_observer_ = observer;
        wake();
    }

    void wake() {
        if (draining_ || scheduled_) {
            return;
        }

        scheduled_ = emscripten_set_timeout([](void* pointer) {
            auto& self = *static_cast<runtime*>(pointer);
            self.scheduled_ = 0;
            self.drain();
        }, 0, this);
    }

    void drain() {
        assert(!draining_);
        draining_ = true;
        context_.restart();

        unsigned ready = 0;
        while (ready != 64 && context_.poll_one()) {
            ++ready;
        }
        draining_ = false;

        // Continue only after known runnable work consumed the entire budget.
        if (ready == 64) {
            wake();
        } else if (idle() && idle_observer_) {
            auto observer = std::exchange(idle_observer_, nullptr);
            observer();
        }
    }

private:
    inline static runtime* current_ = nullptr;
    ::asio::io_context context_;
    long scheduled_ = 0;
    bool draining_ = false;
    std::size_t streams_ = 0;
    std::size_t timers_ = 0;
    void (*idle_observer_)() = nullptr;
};

// Browser deadlines replace reactor-backed timers. Cancellation completes the
// suspended operation on its associated executor, never from a foreign thread.
class timer {
public:
    using executor_type = ::asio::any_io_executor;
    using clock_type = std::chrono::steady_clock;
    using time_point = clock_type::time_point;

    explicit timer(executor_type executor)
        : executor_(std::move(executor))
    {}

    template<class ExecutionContext>
        requires requires(ExecutionContext& context) { context.get_executor(); }
    explicit timer(ExecutionContext& context)
        : timer(context.get_executor())
    {}

    template<class Rep, class Period>
    timer(executor_type executor, std::chrono::duration<Rep, Period> delay)
        : timer(std::move(executor))
    {
        expires_after(delay);
    }

    ~timer() {
        cancel();
    }

    executor_type get_executor() const {
        return executor_;
    }

    template<class Rep, class Period>
    std::size_t expires_after(std::chrono::duration<Rep, Period> delay) {
        return expires_at(clock_type::now() + std::chrono::duration_cast<clock_type::duration>(delay));
    }

    std::size_t expires_at(time_point deadline) {
        auto result = cancel();
        deadline_ = deadline;
        return result;
    }

    time_point expiry() const {
        return deadline_;
    }

    std::size_t cancel() {
        auto const count = waiters_.size();
        while (!waiters_.empty()) {
            complete(waiters_.front().get(), ::asio::error::operation_aborted);
        }
        return count;
    }

    std::size_t cancel(std::error_code& ec) {
        ec.clear();
        return cancel();
    }

    template<class CompletionToken>
    auto async_wait(CompletionToken&& token) {
        return ::asio::async_initiate<CompletionToken, void(std::error_code)>(
            [this](auto handler) {
                auto wait = std::make_unique<waiter>();
                wait->owner = this;
                wait->slot = ::asio::get_associated_cancellation_slot(handler);
                wait->executor = ::asio::get_associated_executor(handler, executor_);
                wait->handler = std::move(handler);

                auto* pending = wait.get();
                waiters_.push_back(std::move(wait));
                runtime::current().timer_started();

                if (pending->slot.is_connected()) {
                    pending->slot.assign([this, pending](::asio::cancellation_type type) {
                        if (type != ::asio::cancellation_type::none) {
                            complete(pending, ::asio::error::operation_aborted);
                        }
                    });
                }

                schedule(*pending);
            }, token);
    }

private:
    // Each wait owns its cancellation slot and browser callback. As with
    // steady_timer, one timer may have several independently cancelled waiters.
    struct waiter {
        timer* owner;
        long id = 0;
        executor_type executor;
        ::asio::cancellation_slot slot;
        ::asio::any_completion_handler<void(std::error_code)> handler;
    };

    void schedule(waiter& wait) {
        // max() means no deadline. It still has a pending, cancellable waiter.
        if (deadline_ == time_point::max()) {
            return;
        }

        auto const now = clock_type::now();
        auto const delay = deadline_ <= now ? 0.0 :
            std::chrono::duration<double, std::milli>(deadline_ - now).count();

        // Browser delays are signed 32-bit milliseconds. Long finite deadlines
        // are rearmed in bounded chunks, never truncated into an immediate fire.
        wait.id = emscripten_set_timeout([](void* pointer) {
            auto& pending = *static_cast<waiter*>(pointer);
            pending.id = 0;

            if (clock_type::now() < pending.owner->deadline_) {
                pending.owner->schedule(pending);
            } else {
                pending.owner->complete(&pending, {});
            }
        }, std::min(delay, 2147483647.0), &wait);
    }

    void complete(waiter* pending, std::error_code error) {
        auto at = std::find_if(waiters_.begin(), waiters_.end(),
            [pending](auto const& wait) { return wait.get() == pending; });
        assert(at != waiters_.end());

        auto wait = std::move(*at);
        waiters_.erase(at);

        if (wait->id) {
            emscripten_clear_timeout(wait->id);
        }
        wait->slot.clear();
        runtime::current().timer_completed();

        ::asio::post(wait->executor, [handler = std::move(wait->handler), error]() mutable {
            handler(error);
        });
        runtime::current().wake();
    }

    executor_type executor_;
    time_point deadline_ = clock_type::now();
    std::list<std::unique_ptr<waiter>> waiters_;
};

} // namespace kth::browser

#endif // KTH_INFRASTRUCTURE_UTILITY_BROWSER_RUNTIME_HPP
