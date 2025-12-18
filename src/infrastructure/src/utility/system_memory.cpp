// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/system_memory.hpp>

#include <cstdint>

#if defined(KTH_WITH_JEMALLOC)
#include <jemalloc/jemalloc.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>
#include <cstdio>
#include <sys/sysinfo.h>
#endif

namespace kth {

size_t get_total_system_memory() {
#if defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<size_t>(status.ullTotalPhys);
    }
    return 0;
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0) {
        return static_cast<size_t>(memsize);
    }
    return 0;
#elif defined(__FreeBSD__)
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctlbyname("hw.physmem", &memsize, &len, nullptr, 0) == 0) {
        return static_cast<size_t>(memsize);
    }
    return 0;
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.totalram * info.mem_unit;
    }
    return 0;
#else
    return 0;
#endif
}

size_t get_available_system_memory() {
#if defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<size_t>(status.ullAvailPhys);
    }
    return 0;
#elif defined(__APPLE__)
    // On macOS, get free + inactive + purgeable memory
    mach_port_t host = mach_host_self();
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
        // Page size
        vm_size_t page_size;
        host_page_size(host, &page_size);

        // Available = free + inactive + purgeable (can be reclaimed)
        uint64_t available = (uint64_t)(vm_stats.free_count + vm_stats.inactive_count + vm_stats.purgeable_count) * page_size;
        return static_cast<size_t>(available);
    }
    return 0;
#elif defined(__FreeBSD__)
    // On FreeBSD, use vm.stats.vm.v_free_count and page size
    unsigned int free_count = 0;
    size_t len = sizeof(free_count);
    if (sysctlbyname("vm.stats.vm.v_free_count", &free_count, &len, nullptr, 0) == 0) {
        int page_size = getpagesize();
        // Also get inactive pages as they can be reclaimed
        unsigned int inactive_count = 0;
        len = sizeof(inactive_count);
        sysctlbyname("vm.stats.vm.v_inactive_count", &inactive_count, &len, nullptr, 0);
        return static_cast<size_t>((free_count + inactive_count)) * page_size;
    }
    return 0;
#elif defined(__linux__)
    // On Linux, read MemAvailable from /proc/meminfo (most accurate)
    FILE* f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            unsigned long long mem_kb;
            if (sscanf(line, "MemAvailable: %llu kB", &mem_kb) == 1) {
                fclose(f);
                return static_cast<size_t>(mem_kb * 1024);
            }
        }
        fclose(f);
    }
    // Fallback to sysinfo
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (info.freeram + info.bufferram) * info.mem_unit;
    }
    return 0;
#else
    return 0;
#endif
}

size_t get_resident_memory() {
#if defined(KTH_WITH_JEMALLOC)
    // Force jemalloc to update its statistics by advancing the epoch
    // This is necessary because jemalloc uses thread-local caching
    uint64_t epoch = 1;
    size_t epoch_sz = sizeof(epoch);
    mallctl("epoch", &epoch, &epoch_sz, &epoch, epoch_sz);

    // Now read the stats
    size_t allocated = 0;
    size_t sz = sizeof(allocated);
    if (mallctl("stats.allocated", &allocated, &sz, nullptr, 0) == 0) {
        return allocated;
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<size_t>(pmc.WorkingSetSize);
    }
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#elif defined(__FreeBSD__)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // ru_maxrss is in kilobytes on FreeBSD
        return static_cast<size_t>(usage.ru_maxrss) * 1024;
    }
    return 0;
#elif defined(__linux__)
    // On Linux, read from /proc/self/statm
    FILE* f = fopen("/proc/self/statm", "r");
    if (f) {
        long pages = 0;
        if (fscanf(f, "%*d %ld", &pages) == 1) {
            fclose(f);
            return pages * sysconf(_SC_PAGESIZE);
        }
        fclose(f);
    }
    return 0;
#else
    return 0;
#endif
}

bool is_jemalloc_active() {
#if defined(KTH_WITH_JEMALLOC)
    // Try to get jemalloc version - if this succeeds, jemalloc is active
    char const* version = nullptr;
    size_t version_len = sizeof(version);
    if (mallctl("version", &version, &version_len, nullptr, 0) == 0 && version != nullptr) {
        return true;
    }
    return false;
#else
    return false;
#endif
}

std::string get_jemalloc_version() {
#if defined(KTH_WITH_JEMALLOC)
    char const* version = nullptr;
    size_t version_len = sizeof(version);
    if (mallctl("version", &version, &version_len, nullptr, 0) == 0 && version != nullptr) {
        return std::string(version);
    }
    return "unknown";
#else
    return "not compiled";
#endif
}

} // namespace kth
