// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_DEFINE_HPP
#define KTH_DATABASE_DEFINE_HPP

#include <kth/domain.hpp>

#include <cstdint>
#include <vector>

// Now we use the generic helper definitions to
// define KD_API and KD_INTERNAL.
// KD_API is used for the public API symbols. It either DLL imports or
// DLL exports (or does nothing for static build)
// KD_INTERNAL is used for non-api symbols.

#if defined KD_STATIC
#define KD_API
#define KD_INTERNAL
#elif defined KD_DLL
#define KD_API BC_HELPER_DLL_EXPORT
#define KD_INTERNAL BC_HELPER_DLL_LOCAL
#else
#define KD_API BC_HELPER_DLL_IMPORT
#define KD_INTERNAL BC_HELPER_DLL_LOCAL
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

}  // namespace kth::database

#endif
