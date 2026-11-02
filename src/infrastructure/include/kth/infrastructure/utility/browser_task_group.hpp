// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see COPYING.
#ifndef KTH_INFRASTRUCTURE_BROWSER_TASK_GROUP_HPP
#define KTH_INFRASTRUCTURE_BROWSER_TASK_GROUP_HPP
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <asio/experimental/channel.hpp>
#include <cassert>
#include <exception>
#include <functional>
#include <utility>
#include <memory>
#include <string_view>
#include <type_traits>
namespace kth {
// Worker-affine nursery. The one-slot doorbell coalesces completion events;
// the owner retains the active count, so coalescing cannot lose join debt.
class task_group {
    struct state {
        explicit state(::asio::any_io_executor executor) : executor(executor), completed(executor, 1) {}
        ::asio::any_io_executor executor;
        ::asio::experimental::channel<void(std::error_code)> completed;
        std::size_t active = 0;
        std::size_t total = 0;
        std::exception_ptr failure;
    };
public:
    task_group(std::string_view, ::asio::any_io_executor executor) : state_(std::make_shared<state>(executor)) {}
    task_group(task_group const&) = delete;
    ~task_group() { assert(!state_->active); }
    template<class Coro>
    void spawn(std::string_view, Coro&& coroutine) {
        auto shared = state_;
        ++shared->active; ++shared->total;
        ::asio::co_spawn(shared->executor,
            [work=std::forward<Coro>(coroutine)]() mutable -> ::asio::awaitable<void> {
                if constexpr (std::is_invocable_v<Coro>) co_await std::invoke(std::move(work));
                else co_await std::move(work);
            }, [shared](std::exception_ptr error) {
                if (error && !shared->failure) shared->failure = error;
                if (--shared->active == 0) shared->completed.try_send(std::error_code{});
            });
    }
    ::asio::awaitable<void> join() {
        // Cancellation is routed to child owners by the containing lifecycle;
        // join itself must finish before borrowed child state can be destroyed.
        co_await ::asio::this_coro::reset_cancellation_state(::asio::disable_cancellation());
        while (state_->active) co_await state_->completed.async_receive(::asio::use_awaitable);
        auto error = std::exchange(state_->failure, {});
        if (error) std::rethrow_exception(error);
    }
    bool has_active_tasks() const noexcept { return state_->active != 0; }
    std::size_t active_count() const noexcept { return state_->active; }
    std::size_t total_spawned() const noexcept { return state_->total; }
private:
    std::shared_ptr<state> state_;
};
class scoped_task_group : public task_group {
public:
    using task_group::task_group;
    // Browser destruction cannot block. Explicit co_await join() is required.
};
}
#endif
