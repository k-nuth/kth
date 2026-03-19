// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UTILITY_SYSTEM_MEMORY_HPP
#define KTH_INFRASTRUCTURE_UTILITY_SYSTEM_MEMORY_HPP

#include <cstddef>
#include <string>

#include <kth/infrastructure/define.hpp>

namespace kth {

/// Get total physical memory in the system (bytes).
/// Returns 0 on unsupported platforms or error.
[[nodiscard]]
KI_API size_t get_total_system_memory();

/// Get available (free + reclaimable) memory in the system (bytes).
/// On macOS: free + inactive + purgeable pages.
/// On Linux: MemAvailable from /proc/meminfo.
/// Returns 0 on unsupported platforms or error.
[[nodiscard]]
KI_API size_t get_available_system_memory();

/// Get current process memory allocation (bytes).
/// When compiled with jemalloc (KTH_WITH_JEMALLOC), uses jemalloc's
/// stats.allocated for accurate tracking.
/// Otherwise falls back to platform-specific APIs (mach on macOS,
/// /proc/self/statm on Linux).
/// Returns 0 on unsupported platforms or error.
[[nodiscard]]
KI_API size_t get_resident_memory();

/// Check if jemalloc is the active memory allocator at runtime.
/// Returns true only if KTH_WITH_JEMALLOC is defined and jemalloc
/// is successfully responding to mallctl queries.
[[nodiscard]]
KI_API bool is_jemalloc_active();

/// Get jemalloc version string.
/// Returns the version string if jemalloc is active,
/// "unknown" if active but version query fails,
/// "not compiled" if KTH_WITH_JEMALLOC is not defined.
[[nodiscard]]
KI_API std::string get_jemalloc_version();

} // namespace kth

#endif // KTH_INFRASTRUCTURE_UTILITY_SYSTEM_MEMORY_HPP
