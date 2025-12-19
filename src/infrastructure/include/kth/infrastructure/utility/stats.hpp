// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_STATS_HPP
#define KTH_INFRASTRUCTURE_STATS_HPP

#include <atomic>
#include <chrono>
#include <cstdint>

#include <kth/infrastructure/define.hpp>

namespace kth {

// =============================================================================
// Compile-time stats toggle
// =============================================================================
//
// When KTH_WITH_STATS is defined, statistics collection is enabled.
// When disabled, all stats macros compile to no-ops with zero overhead.
//
// Usage:
//   KTH_STATS_DECLARE(my_component)           // In header, declares stats struct
//   KTH_STATS_TIME_START(my_op)               // Start timing
//   KTH_STATS_TIME_END(stats, my_op)          // End timing and record
//   KTH_STATS_INCREMENT(stats, my_counter)    // Increment counter
//
// =============================================================================

#ifdef KTH_WITH_STATS

// =============================================================================
// Stats enabled - full implementation
// =============================================================================

/// Timing scope helper - RAII-based timing
struct stats_timer {
    using clock = std::chrono::steady_clock;
    clock::time_point start{clock::now()};

    [[nodiscard]]
    uint64_t elapsed_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock::now() - start).count();
    }
};

/// Base stats structure with common operations
struct stats_base {
    void reset_all() {}  // Override in derived
};

// Macro to start timing
#define KTH_STATS_TIME_START(name) \
    auto const _kth_stats_timer_##name = kth::stats_timer{}

// Macro to end timing and accumulate (also increments count)
#define KTH_STATS_TIME_END(stats_obj, name, time_field, count_field) \
    do { \
        (stats_obj).time_field.fetch_add( \
            _kth_stats_timer_##name.elapsed_ns(), \
            std::memory_order_relaxed); \
        (stats_obj).count_field.fetch_add(1, std::memory_order_relaxed); \
    } while(0)

// Macro to end timing WITHOUT incrementing count (for batch timing)
#define KTH_STATS_TIME_ADD(stats_obj, name, time_field) \
    (stats_obj).time_field.fetch_add( \
        _kth_stats_timer_##name.elapsed_ns(), \
        std::memory_order_relaxed)

// Macro to increment a counter
#define KTH_STATS_INCREMENT(stats_obj, field) \
    (stats_obj).field.fetch_add(1, std::memory_order_relaxed)

// Macro to add to a counter
#define KTH_STATS_ADD(stats_obj, field, value) \
    (stats_obj).field.fetch_add(value, std::memory_order_relaxed)

// Macro to get average time in microseconds
#define KTH_STATS_AVG_US(stats_obj, time_field, count_field) \
    ((stats_obj).count_field.load(std::memory_order_relaxed) > 0 \
        ? (stats_obj).time_field.load(std::memory_order_relaxed) / 1000 / \
          (stats_obj).count_field.load(std::memory_order_relaxed) \
        : 0)

// Macro to get average time in milliseconds
#define KTH_STATS_AVG_MS(stats_obj, time_field, count_field) \
    ((stats_obj).count_field.load(std::memory_order_relaxed) > 0 \
        ? (stats_obj).time_field.load(std::memory_order_relaxed) / 1000000 / \
          (stats_obj).count_field.load(std::memory_order_relaxed) \
        : 0)

// Conditional code block
#define KTH_STATS_ENABLED 1
#define KTH_IF_STATS(code) code

#else // !KTH_WITH_STATS

// =============================================================================
// Stats disabled - zero overhead no-ops
// =============================================================================

#define KTH_STATS_TIME_START(name) ((void)0)
#define KTH_STATS_TIME_END(stats_obj, name, time_field, count_field) ((void)0)
#define KTH_STATS_TIME_ADD(stats_obj, name, time_field) ((void)0)
#define KTH_STATS_INCREMENT(stats_obj, field) ((void)0)
#define KTH_STATS_ADD(stats_obj, field, value) ((void)0)
#define KTH_STATS_AVG_US(stats_obj, time_field, count_field) (0)
#define KTH_STATS_AVG_MS(stats_obj, time_field, count_field) (0)

#define KTH_STATS_ENABLED 0
#define KTH_IF_STATS(code) ((void)0)

#endif // KTH_WITH_STATS

// =============================================================================
// Sync stats structure - used by header sync and block sync
// =============================================================================

struct sync_stats {
    // Header organizer stats
    std::atomic<uint64_t> validate_calls{0};
    std::atomic<uint64_t> validate_time_ns{0};
    std::atomic<uint64_t> hash_calls{0};
    std::atomic<uint64_t> hash_time_ns{0};
    std::atomic<uint64_t> index_add_calls{0};
    std::atomic<uint64_t> index_add_time_ns{0};

    // Header index internal stats
    std::atomic<uint64_t> find_parent_calls{0};
    std::atomic<uint64_t> find_parent_time_ns{0};
    std::atomic<uint64_t> build_skip_calls{0};
    std::atomic<uint64_t> build_skip_time_ns{0};
    std::atomic<uint64_t> insert_calls{0};
    std::atomic<uint64_t> insert_time_ns{0};

    // Batch-level stats (sync_session)
    std::atomic<uint64_t> batch_count{0};
    std::atomic<uint64_t> batch_locator_ns{0};
    std::atomic<uint64_t> batch_network_ns{0};
    std::atomic<uint64_t> batch_process_ns{0};

    void reset() {
        validate_calls = 0;
        validate_time_ns = 0;
        hash_calls = 0;
        hash_time_ns = 0;
        index_add_calls = 0;
        index_add_time_ns = 0;
        find_parent_calls = 0;
        find_parent_time_ns = 0;
        build_skip_calls = 0;
        build_skip_time_ns = 0;
        insert_calls = 0;
        insert_time_ns = 0;
        batch_count = 0;
        batch_locator_ns = 0;
        batch_network_ns = 0;
        batch_process_ns = 0;
    }
};

// =============================================================================
// Global stats instance (when enabled)
// =============================================================================

#ifdef KTH_WITH_STATS
KI_API sync_stats& global_sync_stats();
#else
// When disabled, provide inline stub that does nothing
inline sync_stats& global_sync_stats() {
    static sync_stats dummy;
    return dummy;
}
#endif

} // namespace kth

#endif // KTH_INFRASTRUCTURE_STATS_HPP
