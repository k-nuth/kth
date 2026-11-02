// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see COPYING.
#ifndef KTH_INFRASTRUCTURE_BROWSER_RUNTIME_HPP
#define KTH_INFRASTRUCTURE_BROWSER_RUNTIME_HPP
#if !defined(__EMSCRIPTEN__)
#error "browser_runtime is Worker-only"
#endif
#include <asio.hpp>
#include <asio/any_completion_handler.hpp>
#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <chrono>
#include <cassert>
#include <utility>

namespace kth::browser {
// All access, including destruction, belongs to the browser Worker. Producers
// enter through browser callbacks and wake the runnable queue explicitly.
class runtime {
public:
    runtime() { assert(current_ == nullptr); current_ = this; }
    ~runtime() { if (scheduled_) emscripten_clear_timeout(scheduled_); current_ = nullptr; }
    runtime(runtime const&) = delete;
    runtime& operator=(runtime const&) = delete;
    static runtime& current() { assert(current_); return *current_; }
    ::asio::io_context& context() { return context_; }
    void stream_created() { ++streams_; }
    void stream_destroyed() { assert(streams_); --streams_; }
    void timer_started() { ++timers_; }
    void timer_completed() { assert(timers_); --timers_; }
    std::size_t streams() const { return streams_; }
    std::size_t timers() const { return timers_; }
    bool idle() const { return !draining_ && !scheduled_ && !streams_ && !timers_ && context_.stopped(); }
    // One lifecycle observer, owned by this Worker. Notification happens after
    // the root completion returns and the scheduler has no outstanding work.
    void when_idle(void (*observer)()) { idle_observer_=observer; wake(); }
    void wake() {
        if (draining_ || scheduled_) return;
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
        while (ready != 64 && context_.poll_one()) ++ready;
        draining_ = false;
        // Continue only after known runnable work consumed the entire budget.
        if (ready == 64) wake();
        else if (idle() && idle_observer_) {
            auto observer=std::exchange(idle_observer_,nullptr);
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
    explicit timer(executor_type executor) : executor_(std::move(executor)) {}
    template<class ExecutionContext>
        requires requires(ExecutionContext& context) { context.get_executor(); }
    explicit timer(ExecutionContext& context) : timer(context.get_executor()) {}
    template<class Rep, class Period>
    timer(executor_type executor, std::chrono::duration<Rep, Period> delay) : timer(std::move(executor)) { expires_after(delay); }
    ~timer() { cancel(); }
    executor_type get_executor() const { return executor_; }
    template<class Rep, class Period>
    std::size_t expires_after(std::chrono::duration<Rep, Period> delay) {
        return expires_at(clock_type::now() + std::chrono::duration_cast<clock_type::duration>(delay));
    }
    std::size_t expires_at(time_point deadline) {
        auto result = cancel(); deadline_ = deadline; return result;
    }
    time_point expiry() const { return deadline_; }
    std::size_t cancel() {
        if (!handler_) return 0;
        if (id_) emscripten_clear_timeout(id_);
        id_ = 0;
        complete(::asio::error::operation_aborted);
        return 1;
    }
    std::size_t cancel(std::error_code& ec) { ec.clear(); return cancel(); }
    template<class CompletionToken>
    auto async_wait(CompletionToken&& token) {
        return ::asio::async_initiate<CompletionToken, void(std::error_code)>(
            [this](auto handler) {
                assert(!handler_);
                slot_ = ::asio::get_associated_cancellation_slot(handler);
                completion_executor_ = ::asio::get_associated_executor(handler, executor_);
                handler_ = std::move(handler);
                runtime::current().timer_started();
                if (slot_.is_connected()) slot_.assign([this](::asio::cancellation_type type) {
                    if (type != ::asio::cancellation_type::none) cancel();
                });
                auto delay = std::chrono::duration<double, std::milli>(deadline_ - clock_type::now()).count();
                id_ = emscripten_set_timeout([](void* pointer) {
                    auto& self = *static_cast<timer*>(pointer);
                    self.id_ = 0; self.complete({});
                }, std::max(0.0, delay), this);
            }, token);
    }
private:
    void complete(std::error_code error) {
        slot_.clear();
        auto handler = std::move(handler_);
        runtime::current().timer_completed();
        ::asio::post(completion_executor_, [handler = std::move(handler), error]() mutable { handler(error); });
        runtime::current().wake();
    }
    executor_type executor_;
    executor_type completion_executor_;
    time_point deadline_ = clock_type::now();
    long id_ = 0;
    ::asio::cancellation_slot slot_;
    ::asio::any_completion_handler<void(std::error_code)> handler_;
};
}
#endif
