// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_DEFINE_HPP
#define KTH_DATABASE_DEFINE_HPP

#include <cstdint>
#include <vector>
#include <kth/domain.hpp>

// Now we use the generic helper definitions to
// define KDB_API and KDB_INTERNAL.
// KDB_API is used for the public API symbols. It either DLL imports or
// DLL exports (or does nothing for static build)
// KDB_INTERNAL is used for non-api symbols.

#if defined KDB_STATIC
    #define KDB_API
    #define KDB_INTERNAL
#elif defined KDB_DLL
    #define KDB_API      KD_HELPER_DLL_EXPORT
    #define KDB_INTERNAL KD_HELPER_DLL_LOCAL
#else
    #define KDB_API      KD_HELPER_DLL_IMPORT
    #define KDB_INTERNAL KD_HELPER_DLL_LOCAL
#endif

// Log name.
#define LOG_DATABASE "[database] "

// Remap safety is required if the mmap file is not fully preallocated.
#define REMAP_SAFETY

// Allocate safety is required for support of concurrent write operations.
#define ALLOCATE_SAFETY

namespace kth::database {

using array_index = uint32_t;
using file_offset = uint64_t;

} // namespace kth::database

#endif
