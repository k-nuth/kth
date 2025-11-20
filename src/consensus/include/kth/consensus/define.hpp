// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONSENSUS_DEFINE_HPP
#define KTH_CONSENSUS_DEFINE_HPP

// See http://gcc.gnu.org/wiki/Visibility

// Generic helper definitions for shared library support
#if defined _MSC_VER || defined __CYGWIN__
    #define KC_HELPER_DLL_IMPORT __declspec(dllimport)
    #define KC_HELPER_DLL_EXPORT __declspec(dllexport)
    #define KC_HELPER_DLL_LOCAL
#else
    #if __GNUC__ >= 4
        #define KC_HELPER_DLL_IMPORT __attribute__ ((visibility ("default")))
        #define KC_HELPER_DLL_EXPORT __attribute__ ((visibility ("default")))
        #define KC_HELPER_DLL_LOCAL  __attribute__ ((visibility ("internal")))
    #else
        #define KC_HELPER_DLL_IMPORT
        #define KC_HELPER_DLL_EXPORT
        #define KC_HELPER_DLL_LOCAL
    #endif
#endif

// Now we use the generic helper definitions above to
// define KC_API and KC_INTERNAL.
// KC_API is used for the public API symbols. It either DLL imports or
// DLL exports (or does nothing for static build)
// KC_INTERNAL is used for non-api symbols.

#if defined KC_STATIC
    #define KC_API
    #define KC_INTERNAL
#elif defined KC_DLL
    #define KC_API      KC_HELPER_DLL_EXPORT
    #define KC_INTERNAL KC_HELPER_DLL_LOCAL
#else
    #define KC_API      KC_HELPER_DLL_IMPORT
    #define KC_INTERNAL KC_HELPER_DLL_LOCAL
#endif

#endif
