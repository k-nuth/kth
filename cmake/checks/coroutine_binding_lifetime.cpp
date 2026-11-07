// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Configure-time probe: does the compiler destroy the object behind a by-value
// structured binding that is still in scope at a later suspension point?
// The language requires it. GCC 15 and 16.1 at -O1 and above do not, neither
// when the coroutine completes nor when it is destroyed while suspended, so a
// binding of a co_await result keeps what it owns alive. GCC 14 and 16.2, and
// Clang, destroy it. Exits 0 when every case destroys what it bound.

#include <coroutine>
#include <tuple>

namespace {

int live = 0;

struct tracer {
    bool owns = true;
    tracer() { ++live; }
    tracer(tracer&& other) noexcept { other.owns = false; }
    ~tracer() { if (owns) --live; }
};

struct task {
    struct promise_type {
        task get_return_object() { return {std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
    std::coroutine_handle<promise_type> handle;
};

// A suspension that resumes with a tuple owning a tracer, like a channel
// receive awaited with as_tuple.
struct receive {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    std::tuple<int, tracer> await_resume() const { return {0, tracer{}}; }
};

task straight() {
    auto [ec, value] = co_await receive{};
    (void)ec; (void)value;
    co_await std::suspend_always{};
}

task loop() {
    for (int i = 0; i < 4; ++i) {
        auto [ec, value] = co_await receive{};
        if (ec) break;
        co_await std::suspend_always{};
    }
}

task nested_block() {
    auto [ec, value] = co_await receive{};
    (void)ec; (void)value;
    { co_await std::suspend_always{}; }
}

template <typename Coroutine>
bool completes_clean(Coroutine coroutine) {
    live = 0;
    auto t = coroutine();
    while ( ! t.handle.done()) t.handle.resume();
    t.handle.destroy();
    return live == 0;
}

template <typename Coroutine>
bool destroyed_clean(Coroutine coroutine) {
    live = 0;
    auto t = coroutine();
    t.handle.resume();      // Bound, now suspended after the binding.
    t.handle.destroy();
    return live == 0;
}

} // namespace

int main() {
    bool const clean =
        completes_clean(straight) && completes_clean(loop) && completes_clean(nested_block) &&
        destroyed_clean(straight) && destroyed_clean(loop) && destroyed_clean(nested_block);
    return clean ? 0 : 1;
}
