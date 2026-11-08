// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Compiled only by scripts/check_template_cancellation.py. The normal library
// has no entry observer. Reuse the actual public API fixture in this one TU;
// the runner excludes all original test objects from its standalone link.
#include "template_prevout_refresh.cpp"
#include <thread>
#include <exception>

#if defined(KTH_CURRENCY_BCH)
namespace {
struct cancellation_entry {
    std::promise<void> entered;
    std::promise<void> emitted;
    std::shared_future<void> emission = emitted.get_future().share();
};
cancellation_entry* template_cancel_started = nullptr;
}
extern "C" void kth_template_cancel_started() {
    if (template_cancel_started) {
        template_cancel_started->entered.set_value();
        // Test-only synchronous worker boundary: production has no observer.
        // The caller owner releases it only after submitting cancellation.
        template_cancel_started->emission.wait();
    }
}
TEST_CASE("template child: cancellation crosses real refresh worker ownership", "[.template-cancel-owner-child]") {
    funding_chain built("template_cancel_owner");
    auto& chain = built.fixture.chain();
    // Synthetic missing prevouts exercise the actual snapshot indexing and
    // confirmed-read boundary, with no concurrent pool mutation.
    for (uint32_t n = 1; n <= 10000; ++n) {
        hash_digest absent{};
        absent[0] = 0xee;
        for (unsigned b = 0; b != 4; ++b) absent[b + 1] = uint8_t(n >> (8 * b));
        auto tx = synthetic_spend(domain::chain::output_point{absent, 0}, 10000);
        REQUIRE(chain.mempool_ref().add(entry(tx)));
    }
    watchdog guard(std::chrono::seconds(30), "real template refresh cancellation ownership");
    cancellation_entry checkpoint;
    auto entered = checkpoint.entered.get_future();
    template_cancel_started = &checkpoint;
    struct reset_observer {
        ~reset_observer() { template_cancel_started = nullptr; }
    } reset;
    ::asio::io_context io;
    ::asio::cancellation_signal cancellation;
    auto pending = ::asio::co_spawn(io, chain.fetch_template(),
        ::asio::bind_cancellation_slot(cancellation.slot(), ::asio::use_future));
    std::jthread runner([&] { io.run(); });
    // If submitting the owner event throws, unblock the worker before the
    // jthread joins during unwinding. Never leave its borrowed checkpoint live.
    struct unblock_on_exception {
        std::promise<void>& emitted;
        ~unblock_on_exception() {
            if (std::uncaught_exceptions() != 0) {
                try { emitted.set_value(); } catch (std::future_error const&) {}
            }
        }
    } unblock{checkpoint.emitted};
    // The test-only TU marker establishes child entry before cancellation. Only
    // this caller owner emits the signal; the test never races its own signal.
    entered.get();
    std::exception_ptr emit_failure;
    ::asio::post(io, [&] {
        try { cancellation.emit(::asio::cancellation_type::all); }
        catch (...) { emit_failure = std::current_exception(); }
        // Even a failed emit releases the test boundary. Its exception is
        // observed after joining, never allowed to escape this owner handler.
        checkpoint.emitted.set_value();
    });
    std::optional<std::expected<blockchain::block_template, code>> result;
    std::exception_ptr failure;
    try { result.emplace(pending.get()); } catch (...) { failure = std::current_exception(); }
    runner.join();
    template_cancel_started = nullptr;
    if (emit_failure) std::rethrow_exception(emit_failure);
    if (failure) {
        try { std::rethrow_exception(failure); }
        catch (std::system_error const& e) { CHECK(e.code() == ::asio::error::operation_aborted); }
    } else {
        REQUIRE(result.has_value());
        CHECK_FALSE(result->has_value());
        if (!*result) CHECK(result->error() == error::service_stopped);
    }
    announce_completion();
}
TEST_CASE("real template cancellation stays on its worker owner", "[template_cancel_owner]") {
    auto const child = run_child("template child: cancellation crosses real refresh worker ownership");
    // Inspect diagnostics even if a sanitizer was configured to return zero.
    CHECK(child.err.find("ThreadSanitizer:") == std::string::npos);
    require_clean_child(child);
}
#endif
