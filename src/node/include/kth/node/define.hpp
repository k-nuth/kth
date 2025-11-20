// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DEFINE_HPP
#define KTH_NODE_DEFINE_HPP

#include <kth/domain.hpp>

// We use the generic helper definitions to define KND_API
// and KND_INTERNAL. KND_API is used for the public API symbols. It either DLL
// imports or DLL exports (or does nothing for static build) KND_INTERNAL is
// used for non-api symbols.

#if defined KND_STATIC
    #define KND_API
    #define KND_INTERNAL
#elif defined KND_DLL
    #define KND_API      KD_HELPER_DLL_EXPORT
    #define KND_INTERNAL KD_HELPER_DLL_LOCAL
#else
    #define KND_API      KD_HELPER_DLL_IMPORT
    #define KND_INTERNAL KD_HELPER_DLL_LOCAL
#endif

// Log name.
#define LOG_NODE "[node] "

#endif
