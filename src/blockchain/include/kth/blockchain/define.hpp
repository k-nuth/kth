// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_DEFINE_HPP
#define KTH_BLOCKCHAIN_DEFINE_HPP

#include <kth/domain.hpp>

// Now we use the generic helper definitions to
// define KB_API and KB_INTERNAL.
// KB_API is used for the public API symbols. It either DLL imports or
// DLL exports (or does nothing for static build)
// KB_INTERNAL is used for non-api symbols.

#if defined KB_STATIC
    #define KB_API
    #define KB_INTERNAL
#elif defined KB_DLL
    #define KB_API      KD_HELPER_DLL_EXPORT
    #define KB_INTERNAL KD_HELPER_DLL_LOCAL
#else
    #define KB_API      KD_HELPER_DLL_IMPORT
    #define KB_INTERNAL KD_HELPER_DLL_LOCAL
#endif



#endif
